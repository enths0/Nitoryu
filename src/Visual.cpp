// Threads:
//   No hook of its own: the main loop's tick (attach, follow, sheathe). The setWeapon hook queues draws and
//   sheathes (noteDraw, noteSheathe), the combat threads ask bladeReady().
//   g_lock guards g_ready, g_pending and g_pendingSheathe; g_entries and g_lefties are the main loop's alone.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include <kenshi/Character.h>
#include <kenshi/Appearance.h>
#include <kenshi/Inventory.h>
#include <kenshi/Gear.h>
#include <kenshi/GameData.h>
#include <kenshi/util/hand.h>
#include <kenshi/combat/CombatClass.h>
#include <kenshi/combat/CombatTechniqueData.h>
#include <kenshi/Animation/AnimationClass.h>
#include <kenshi/CharMovement.h>
#include <kenshi/AI/AITaskSystem.h>

#include <OgreVector3.h>
#include <OgreQuaternion.h>
#include <OgreOldSkeletonInstance.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Visual.h"
#include "Feedback.h"
#include "Arms.h"
#include "Moves.h"
#include "Defense.h"
#include "Combat.h"
#include "Ambidex.h"
#include "Stance.h"
#include "Lefty.h"
#include "Foreign.h"
#include "Diagnostics.h"
#include "Settings.h"
#include "kit/diag.h"
#include "kit/fcs.h"

namespace
{
    // The left blade's slot. With the pose engine (Arms.cpp) it hangs on Bip01
    // Prop1, which the engine re-poses every frame as the mirror of Bip01 Prop2
    // (the right weapon's mount, keyed in every sword animation), with no
    // offset, exactly as `hands` hangs on Prop2. Without it (mirror_pose=0, or a
    // rig the engine cannot mirror) Prop1 is the two-hand grip point at the
    // right hand, so the blade goes on Bip01 L Hand with a fixed grip offset.
    const char* SLOT_PROP = "nitoryu_offhand";
    const char* SLOT_HAND = "nitoryu_offhand_hand";
    const DWORD TICK_MS = 100;
    // setWeapon runs twice per draw, alternating the pair members: act only once the draws have settled.
    const DWORD SETTLE_MS = 300;
    const DWORD CHECK_HANDS_MS = 1000;  // log which member ended in the hands
    const DWORD VERIFY_MS = 1000;
    // The game re-hangs weapons when its own draw ends (~0.7-1 s): attach fresh once more after that.
    const DWORD REFRESH_LATE_MS = 1200;       // how often the left blade's slot is checked
    const DWORD RESTORE_WATCH_MS = 2000; // after a sheathe, keep fixing the back

    // AttachmentData (0x98 bytes), read by AppearanceBase::getAttachmentPosition.
    struct SlotData
    {
        std::string name;           // 0x00
        std::string bone;           // 0x28
        Ogre::Vector3 pos;          // 0x50  offset in the bone's frame
        Ogre::Quaternion rot;       // 0x5c  (Ogre order w, x, y, z)
        std::string mesh;           // 0x70  editor preview mesh, unused in game
    };
    static_assert(sizeof(std::string) == 0x28, "the game's std::string is 0x28 bytes (VS2010 x64)");
    static_assert(offsetof(SlotData, bone) == 0x28, "AttachmentData::bone");
    static_assert(offsetof(SlotData, pos) == 0x50, "AttachmentData::pos");
    static_assert(offsetof(SlotData, rot) == 0x5c, "AttachmentData::rot");
    static_assert(offsetof(SlotData, mesh) == 0x70, "AttachmentData::mesh");
    static_assert(sizeof(SlotData) == 0x98, "AttachmentData size");

    typedef boost::unordered::unordered_map<std::string, AttachmentData*, boost::hash<std::string>,
        std::equal_to<std::string>, Ogre::STLAllocator<std::pair<std::string const, AttachmentData*>,
        Ogre::GeneralAllocPolicy> > SlotMap;

    // [OffHand] in Nitoryu.ini, for SLOT_HAND; defaults = the mirror of Bip01
    // Prop2's bind pose in the right hand, expressed in Bip01 L Hand.
    std::string g_bone = "Bip01 L Hand";
    Ogre::Vector3 g_pos(0.01f, 0.0f, -0.275f);
    Ogre::Quaternion g_rot(0.7053f, -0.0007f, 0.0007f, -0.7089f);
    bool g_enabled = true;
    bool g_pose = true;         // [OffHand] mirror_pose: left arm mirrors the right (Arms.cpp)
    // False when the feature is turned off at run time (the core hooks failed): the next tick takes
    // every blade away and stops.
    volatile bool g_live = true;

    struct Entry
    {
        hand who;
        Weapon* off;                // attached off weapon, or NULL
        const char* slot;           // SLOT_PROP or SLOT_HAND while attached
        GameData* offData;          // its GameData (outlives the item; safe to compare)
        Weapon* lastOff;            // after a detach: the weapon to restore (pointer-checked before use)
        Weapon* lastMain;           // the main weapon while dual (pointer-checked before use)
        // The pair member whose whole weapon we took off each body slot (BODY_SLOTS order; NULL: none),
        // and its GameData. restoreSheaths gives it back.
        Weapon* hidden[3];
        GameData* hiddenData[3];
        DWORD settleAt;             // no attach/detach before this (draw debounce)
        DWORD checkHandsAt;         // log the weapon in the hands at this time (0 = done)
        DWORD restoreUntil;         // after a detach, re-check the back until this time
        DWORD verifyAt;                 // next check that our slot shows the blade
        bool verifiedOk;                // the check has seen our slot right (it can read it)
        bool refresh;                   // a draw happened while attached: re-attach fresh
        DWORD refreshLateAt;            // a second fresh attach after the game's draw ends
        Weapon* lastHands;              // the weapon in the hands at the last real draw
        GameData* lastHandsData;        // its GameData (compared, never dereferenced)
        bool seenHands;                 // lastHands is known
        CombatTechniqueData* dashTech;  // the tailed clip's technique playing (kept until its track ends)
        float dashProg;                 // its progress, never going back
        int dashStall;                  // frames without progress
        bool dashDone;                  // finished; not restarted until the attack ends
        int dashClip;                   // the tailed clip (CLIP_DASH, ...), -1: none
        // the dual sheathe
        bool offHip;                    // the off blade was drawn from the hip (stance::Info::offFromHip while dual)
        bool sheathe;                   // a sheathe hold is on: the clip plays, the left blade is still in the hand
        bool sheatheHip;                // ... with the hip clip
        double sheatheStart;            // defense::clockMs() of its start
        bool sheatheMain;               // we hung the main weapon's bare blade on `hands` for it
        bool sheatheOffHome;            // the left blade went to its sheath already
        int sheatheClip;                // the clip the hold plays (CLIP_SHEATHE, _HIP, CLIP_CHIBURI or _CHIBURI_HIP)
        // the dual draw's hold (below sheatheCut): both blades stay on the back until the clip's grip frame
        bool drawHide;                  // a draw was noted with no left blade attached: the hold may start after the settle
        bool drawHold;                  // the hold is on: the clip plays, the hands are empty
        bool drawHeld;                  // it ended at this tick: the attach that follows does not start the clip again
        bool drawHidden;                // we took the main weapon out of `hands` (and hung it whole on its slot)
        arms::Clip drawClip;            // the clip the hold plays (CLIP_DRAW or CLIP_DRAW_HIP)
        double drawStart;               // defense::clockMs() of the hold's start
        // the chiburi: the last frame this fighter was in combat mode with an attack target
        // (defense::clockMs(), 0: never), and that target
        double foughtAt;
        hand foe;
        foreign::Watch fg;              // another plugin drives this fighter (Foreign.h)
    };

    CRITICAL_SECTION g_lock;
    std::set<Character*> g_ready;           // under g_lock: left blade attached (bladeReady)

    void setReady(Character* c, bool on)
    {
        EnterCriticalSection(&g_lock);
        if (on)
            g_ready.insert(c);
        else
            g_ready.erase(c);
        LeaveCriticalSection(&g_lock);
    }
    std::vector<hand> g_pending;            // noted by combat hooks
    std::vector<hand> g_pendingSheathe;     // sheathes, kept only for tracked characters
    std::map<Character*, Entry> g_entries;  // main loop only
    DWORD g_lastTick = 0;

    std::string nameOf(RootObjectBase* o) { return o != NULL ? o->displayName : std::string("<null>"); }

    // Time comparisons that survive the GetTickCount wrap.
    bool reached(DWORD now, DWORD at) { return (LONG)(now - at) >= 0; }

    std::string iniPath()
    {
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&iniPath), &self);
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) + "\\Nitoryu.ini";
    }

    int readFloats(const std::string& path, const char* key, float* out, int n)
    {
        char buf[128] = { 0 };
        GetPrivateProfileStringA("OffHand", key, "", buf, sizeof(buf), path.c_str());
        int got = 0;
        const char* p = buf;
        while (*p && got < n)
        {
            char* end = NULL;
            double v = strtod(p, &end);
            if (end == p) { ++p; continue; }
            out[got++] = (float)v;
            p = end;
        }
        return got;
    }

    // Adds the left-hand slot to this race's slot map once. The map is shared by
    // every character of the race and read by the game; one insert without a
    // rehash only links a node, so skip if it would rehash.
    bool ensureSlot(AppearanceBase* app, const char* slotName, const std::string& bone,
                    const Ogre::Vector3& pos, const Ogre::Quaternion& rot)
    {
        SlotMap* m = const_cast<SlotMap*>(app->slots);
        if (m == NULL)
        {
            diag::count("visual_no_slot_map");
            return false;
        }
        if (m->find(slotName) != m->end())
            return true;
        if ((float)(m->size() + 1) > m->max_load_factor() * (float)m->bucket_count())
        {
            diag::count("visual_slot_would_rehash");
            return false;
        }
        SlotData* d = new SlotData();
        d->name = slotName;
        d->bone = bone;
        d->pos = pos;
        d->rot = rot;
        m->insert(std::make_pair(std::string(slotName), reinterpret_cast<AttachmentData*>(d)));
        diag::count("visual_slot_added");
        kit::Diag("visual_slot").kv("slot", slotName).kv("bone", bone).kv("slots", (int)m->size()).emit();
        return true;
    }

    // Index of `w` among the weapons of section `name`, or -1. Compares
    // pointers only, so a stale `w` (sold, dropped) is never dereferenced.
    int indexInSection(Inventory* inv, const char* name, Weapon* w)
    {
        InventorySection* sec = inv->getSection(std::string(name));
        if (sec == NULL || w == NULL)
            return -1;
        const Ogre::vector<InventorySection::SectionItem>::type& items = sec->getItems();
        int weaponIndex = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            Weapon* x = items[i].item != NULL ? items[i].item->isWeapon() : NULL;
            if (x == NULL)
                continue;
            if (x == w)
                return weaponIndex;
            ++weaponIndex;
        }
        return -1;
    }

    // True if `w` still sits in the back or hip section (it hangs on the body).
    bool onBody(Character* c, Weapon* w)
    {
        Inventory* inv = c->getInventory();
        return inv != NULL && (indexInSection(inv, "back", w) >= 0 || indexInSection(inv, "hip", w) >= 0);
    }

    // AttachedEntity (opaque in the headers; AppearanceBase::attachItem/detachItem, 1.0.65):
    // +0x08 int kind (0 = item), +0x78 the item's GameData,
    // +0xC0 std::string mesh override (empty = the item's full `mesh`).
    struct EntityView
    {
        int kind;
        GameData* data;
        size_t meshLen;
        unsigned long long itemKey; // the attached item's handle (index | serial << 32), 0 when unknown
    };

    // The offset of the attached item inside AttachedEntity, found once at run time from the left
    // blade's own entity (we attached that item there) and confirmed on the main weapon's `hands`
    // entity. -2: not probed yet; -1: not found, so items are matched by GameData (two identical
    // katanas then look the same). The entity holds no Item pointer but a `hand` of its item, so the
    // item is matched by its handle's index and serial, read as one 64-bit key.
    int g_itemOff = -2;

    unsigned long long keyOf(const Item* it)
    {
        if (it == NULL)
            return 0;
        const hand& h = const_cast<Item*>(it)->getHandle();
        return (unsigned long long)h.index | ((unsigned long long)h.serial << 32);
    }
    int g_itemProbeMisses = 0;      // a fresh entity may not carry its item yet: 3 tries

    void readEntity(void* ctx)
    {
        void** io = static_cast<void**>(ctx);
        const char* ae = static_cast<const char*>(io[0]);
        EntityView* v = static_cast<EntityView*>(io[1]);
        v->kind = *reinterpret_cast<const int*>(ae + 0x08);
        v->data = *reinterpret_cast<GameData* const*>(ae + 0x78);
        v->meshLen = *reinterpret_cast<const size_t*>(ae + 0xC0 + 0x10);
        v->itemKey = g_itemOff >= 0 ? *reinterpret_cast<const unsigned long long*>(ae + g_itemOff) : 0;
    }

    bool guardedRead(void (*fn)(void*), void* ctx)
    {
        __try { fn(ctx); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct ItemProbe
    {
        const char* ae;
        unsigned long long want;    // index | serial << 32
        int found;
    };

    // The first 4-aligned field pair that holds `want`, skipping the mesh string at 0xC0-0xE7.
    const int PROBE_END = 0x180;

    void probeItem(void* ctx)
    {
        ItemProbe* p = static_cast<ItemProbe*>(ctx);
        p->found = -1;
        for (int o = 0x08; o + 8 <= PROBE_END; o += 4)
        {
            if (o + 8 > 0xC0 && o < 0xE8)
                continue;
            if (*reinterpret_cast<const unsigned long long*>(p->ae + o) == p->want)
            {
                p->found = o;
                return;
            }
        }
    }

    struct EntityDump
    {
        const char* ae;
        unsigned char bytes[PROBE_END];
    };

    void copyEntity(void* ctx)
    {
        EntityDump* d = static_cast<EntityDump*>(ctx);
        memcpy(d->bytes, d->ae, PROBE_END);
    }

    // Once, when the item is not found: the entity's first bytes and the item's identity, for the log.
    void dumpEntity(const char* ae, Weapon* off)
    {
        EntityDump d;
        d.ae = ae;
        if (!guardedRead(copyEntity, &d))
            return;
        std::string hex;
        char b[4];
        for (int i = 0; i < PROBE_END; ++i)
        {
            sprintf_s(b, "%02x", d.bytes[i]);
            hex += b;
        }
        const hand& h = off->getHandle();
        char item[160];
        sprintf_s(item, "item=%p weapon=%p data=%p hand_type=%d container=%u container_serial=%u index=%u serial=%u",
                  static_cast<const void*>(static_cast<const Item*>(off)), static_cast<const void*>(off),
                  static_cast<const void*>(off->getGameData()), (int)h.type, h.container, h.containerSerial, h.index, h.serial);
        kit::Diag("visual_entity_dump").kv("who", item).kv("bytes", hex).emit();
    }

    void readItemAt(void* ctx)
    {
        ItemProbe* p = static_cast<ItemProbe*>(ctx);
        p->want = *reinterpret_cast<const unsigned long long*>(p->ae + p->found);
    }

    const char* BODY_SLOTS[3] = { "back", "back2", "hip" };

    void probeItemOffset(Character* c, AppearanceBase* app, const char* slot, Weapon* off)
    {
        if (g_itemOff != -2)
            return;
        AttachedEntity* ae = app->getAttachedEntity(std::string(slot));
        if (ae == NULL)
            return;
        ItemProbe p;
        p.ae = reinterpret_cast<const char*>(ae);
        p.want = keyOf(off);
        if (p.want == 0)
            return;
        p.found = -1;
        if (!guardedRead(probeItem, &p) || p.found < 0)
        {
            diag::count("visual_item_offset_miss");
            if (++g_itemProbeMisses < 3)
                return;
            dumpEntity(p.ae, off);
            g_itemOff = -1;
            diag::count("visual_item_offset_not_found");
            kit::Diag("visual_item_offset").kv("found", false).kv("match", "gamedata").emit();
            return;
        }
        // Confirm on the main weapon's own entity: -1 unknown, 0 differs, 1 same.
        int confirmed = -1;
        Weapon* hands = c->getCurrentWeapon();
        AttachedEntity* he = hands != NULL ? app->getAttachedEntity(std::string("hands")) : NULL;
        if (he != NULL)
        {
            ItemProbe q;
            q.ae = reinterpret_cast<const char*>(he);
            q.want = 0;
            q.found = p.found;
            if (guardedRead(readItemAt, &q))
                confirmed = q.want == keyOf(hands) ? 1 : 0;
        }
        g_itemOff = confirmed == 0 ? -1 : p.found;
        diag::count(g_itemOff >= 0 ? "visual_item_offset_found" : "visual_item_offset_unconfirmed");
        kit::Diag("visual_item_offset").kv("found", true).kv("offset", p.found).kv("confirmed", confirmed)
            .kv("match", g_itemOff >= 0 ? "item" : "gamedata").emit();
    }

    // What a slot shows, for one item: nothing, something else, the item whole (no mesh
    // override), or the item with a mesh override (its empty sheath, or our bare blade).
    // Without the item offset the item is matched by `gd`.
    enum Shows { SHOWS_NONE, SHOWS_OTHER, SHOWS_FULL, SHOWS_OVERRIDE };

    Shows slotState(AppearanceBase* app, const std::string& slot, const Item* item, GameData* gd)
    {
        AttachedEntity* ae = app->getAttachedEntity(slot);
        if (ae == NULL)
            return SHOWS_NONE;
        EntityView v;
        void* io[2] = { ae, &v };
        if (!guardedRead(readEntity, io))
        {
            diag::count("visual_entity_fault");
            return SHOWS_OTHER;
        }
        bool mine = v.kind == 0 && (g_itemOff >= 0 ? (item != NULL && v.itemKey == keyOf(item))
                                                   : (gd != NULL && v.data == gd));
        if (!mine)
            return SHOWS_OTHER;
        return v.meshLen != 0 ? SHOWS_OVERRIDE : SHOWS_FULL;
    }

    // Weapons of this type hanging in the back and hip sections, the one in the hands excepted.
    int carriedOfType(Character* c, GameData* gd)
    {
        Inventory* inv = c->getInventory();
        Weapon* hands = c->getCurrentWeapon();
        const char* sections[2] = { "back", "hip" };
        int n = 0;
        for (int k = 0; inv != NULL && k < 2; ++k)
        {
            InventorySection* sec = inv->getSection(std::string(sections[k]));
            if (sec == NULL)
                continue;
            const Ogre::vector<InventorySection::SectionItem>::type& items = sec->getItems();
            for (size_t i = 0; i < items.size(); ++i)
            {
                Weapon* x = items[i].item != NULL ? items[i].item->isWeapon() : NULL;
                if (x != NULL && x != hands && x->getGameData() == gd)
                    ++n;
            }
        }
        return n;
    }

    // Does the body still lack this weapon? By item: no body slot shows it whole. Without the
    // item offset: fewer body slots show its type whole than the body carries.
    bool bodyLacks(Character* c, AppearanceBase* app, const Item* w, GameData* gd)
    {
        int shown = 0;
        for (int i = 0; i < 3; ++i)
            if (slotState(app, std::string(BODY_SLOTS[i]), w, gd) == SHOWS_FULL)
                ++shown;
        return g_itemOff >= 0 ? shown == 0 : shown < carriedOfType(c, gd);
    }

    std::string hiddenText(const Entry& e)
    {
        std::string t;
        for (int i = 0; i < 3; ++i)
            if (e.hidden[i] != NULL)
                t += (t.empty() ? "" : ",") + std::string(BODY_SLOTS[i]) + (e.hidden[i] == e.lastOff ? ":off" : ":main");
        return t.empty() ? std::string("-") : t;
    }

    // While the stance is drawn neither pair member hangs whole on the body: a body slot that
    // shows one of them without a mesh override (the blade in its sheath) gets only that item's
    // `sheath`, as CharacterHuman::leaveSheathEquipped does for the drawn weapon. The drawn main
    // weapon's slot already carries its sheath override, so it only matches when the game
    // re-hung a duplicate. Matched by item, so a third weapon of the same type keeps its blade;
    // each slot remembers its item for restoreSheaths. Without the item offset: by the off
    // weapon's GameData.
    void fixSheaths(Character* c, AppearanceBase* app, Weapon* main, Weapon* off, Entry& e)
    {
        Weapon* items[2] = { off, main };
        for (int i = 0; i < 3; ++i)
        {
            std::string slot(BODY_SLOTS[i]);
            for (int k = 0; k < 2; ++k)
            {
                Weapon* w = items[k];
                if (w == NULL || (k == 1 && (g_itemOff < 0 || main == off)))
                    continue;
                GameData* gd = w->getGameData();
                if (slotState(app, slot, w, gd) != SHOWS_FULL)
                    continue;
                std::string sheath = kit::fcsFile(w->data, "sheath", "");
                app->detachItem(slot);
                if (!sheath.empty())
                    app->attachItem(w, sheath, slot);
                e.hidden[i] = w;
                e.hiddenData[i] = gd;
                diag::count(k == 0 ? "visual_sheath_fixed" : "visual_sheath_fixed_main");
                if (sheath.empty())
                    diag::count("visual_sheath_none");      // a weapon without a sheath mesh
                if (diag::takeDetail())
                    kit::Diag("visual_sheath").kv("who", nameOf(c)).kv("slot", slot).kv("item", k == 0 ? "off" : "main")
                        .kv("sheath", sheath).emit();
                break;
            }
        }
    }

    // Undo fixSheaths once the stance is not drawn: each slot we emptied gets its whole weapon
    // back when that weapon still hangs on the body and is not in the hands; an empty sheath
    // of a weapon that is gone is just removed. A slot we left empty (a weapon without a `sheath`
    // mesh) is filled again too. Returns the number of slots fixed.
    // `only` != NULL restores that weapon alone (a sheathe hold: each blade goes home at its own frame).
    int restoreSheaths(Character* c, AppearanceBase* app, Entry& e, Weapon* only = NULL)
    {
        Weapon* hands = c->getCurrentWeapon();
        int fixed = 0;
        Weapon* put[3] = { NULL, NULL, NULL };      // attached whole in this pass: never twice
        for (int i = 0; i < 3; ++i)
        {
            Weapon* w = e.hidden[i];
            if (w == NULL || (only != NULL && w != only))
                continue;
            std::string slot(BODY_SLOTS[i]);
            if (w == hands)
            {
                e.hidden[i] = NULL;         // drawn again (toggled off with it in the hands): its sheath stays, as the game shows it
                diag::count("visual_restore_in_hands");
                continue;
            }
            bool carried = onBody(c, w);    // pointer compares only: w may be gone
            Shows st = slotState(app, slot, w, e.hiddenData[i]);
            if (st == SHOWS_OVERRIDE)
            {
                app->detachItem(slot);
                if (carried)
                    app->attachItem(w, slot);
                diag::count(carried ? "visual_restored" : "visual_restore_weapon_gone");
                ++fixed;
            }
            else if (st == SHOWS_NONE && carried && w != put[0] && w != put[1] && w != put[2]
                     && bodyLacks(c, app, w, e.hiddenData[i]))
            {
                bool ok = app->attachItem(w, slot);
                diag::count(ok ? "visual_restored_empty_slot" : "visual_restore_attach_failed");
                if (!ok)
                    continue;               // retried while the watch lasts
                put[i] = w;
                ++fixed;
            }
            else
                diag::count(st == SHOWS_FULL ? "visual_restore_game_did" : st == SHOWS_OTHER ? "visual_restore_slot_taken"
                            : "visual_restore_not_carried");
            if (diag::takeDetail())
                kit::Diag("visual_restore").kv("who", nameOf(c)).kv("slot", slot).kv("item", w == e.lastOff ? "off" : "main")
                    .kv("state", (int)st).kv("carried", carried).emit();
            e.hidden[i] = NULL;
        }
        // The game re-hangs the sheathed weapons on its own schedule and can put a pair
        // member's empty sheath back later: while the watch lasts, give it its weapon. Only with
        // nothing in the hands (a drawn main weapon keeps its own sheath).
        Weapon* pair[2] = { e.lastOff, e.lastMain };
        for (int k = 0; k < 2 && hands == NULL; ++k)
        {
            Weapon* w = pair[k];
            if (w == NULL || w == hands || (k == 1 && (g_itemOff < 0 || w == e.lastOff)) || !onBody(c, w)
                || (only != NULL && w != only))
                continue;
            GameData* gd = w->getGameData();
            for (int i = 0; i < 3; ++i)
            {
                std::string slot(BODY_SLOTS[i]);
                if (slotState(app, slot, w, gd) != SHOWS_OVERRIDE)
                    continue;
                app->detachItem(slot);
                app->attachItem(w, slot);
                diag::count("visual_restored_late");
                ++fixed;
                break;                      // one weapon, one slot
            }
        }
        return fixed;
    }

    bool attach(Character* c, AppearanceBase* app, Weapon* main, Weapon* off, Entry& e)
    {
        // The pose engine first: it makes Prop1 the left blade's mount. A race whose record says
        // `nitoryu rig` = 1 is not posed (the blade in the fixed left-hand grip, as for a rig the mirror
        // cannot build).
        bool fixedGrip = stance::rigOf(c) == stance::RIG_FIXED_GRIP;
        if (fixedGrip)
            diag::count("visual_race_fixed_grip");
        bool posed = g_pose && !fixedGrip && arms::track(c, app->getSkeleton(), arms::OWNER_DUAL);
        const char* slot = posed ? SLOT_PROP : SLOT_HAND;
        bool slotOk = posed ? ensureSlot(app, SLOT_PROP, "Bip01 Prop1", Ogre::Vector3::ZERO, Ogre::Quaternion::IDENTITY)
                            : ensureSlot(app, SLOT_HAND, g_bone, g_pos, g_rot);
        if (!slotOk)
        {
            arms::release(c, arms::OWNER_DUAL);
            return false;
        }
        if (g_pose && !posed && !fixedGrip)
            diag::count("visual_rig_not_mirrored");
        std::string bare = kit::fcsFile(off->data, "bare sword", "");
        if (bare.empty())
        {
            diag::count("visual_no_bare_mesh");     // the stance refuses such a pair
            arms::release(c, arms::OWNER_DUAL);
            return false;
        }
        bool ok = app->attachItem(off, bare, std::string(slot));
        diag::count(ok ? "visual_attached" : "visual_attach_failed");
        if (diag::takeDetail())
            kit::Diag("visual_attach").kv("who", nameOf(c)).kv("off", nameOf(off)).kv("ok", ok).kv("slot", slot)
                .kv("bare", bare).emit();
        if (!ok)
        {
            arms::release(c, arms::OWNER_DUAL);
            return false;
        }
        e.off = off;
        e.refresh = false;          // attached fresh just now
        setReady(c, true);
        e.slot = slot;
        e.offData = off->getGameData();
        e.lastOff = off;
        e.lastMain = main;
        e.restoreUntil = 0;
        probeItemOffset(c, app, slot, off);
        // As leaveSheathEquipped does for the drawn weapon: only the sheath stays behind.
        fixSheaths(c, app, main, off, e);
        return true;
    }

    // The two members of an identical pair traded hands (some mods re-draw all the time): the left
    // blade is re-bound to the new off item in place, with no draw clip and no settle, so the entity
    // always belongs to the item that is really off.
    void rebind(Character* c, AppearanceBase* app, Weapon* main, Weapon* off, Entry& e)
    {
        std::string bare = kit::fcsFile(off->data, "bare sword", "");
        app->detachItem(std::string(e.slot));
        bool ok = !bare.empty() && app->attachItem(off, bare, std::string(e.slot));
        diag::count(ok ? "visual_pair_swap_rebound" : "visual_pair_swap_failed");
        if (!ok)
        {
            e.off = NULL;
            setReady(c, false);
            arms::release(c, arms::OWNER_DUAL);
            return;
        }
        e.off = off;
        e.offData = off->getGameData();
        e.lastOff = off;
        e.lastMain = main;
        fixSheaths(c, app, main, off, e);
    }

    void sheatheDropMain(Character* c, AppearanceBase* app, Entry& e);     // below

    void detach(Character* c, AppearanceBase* app, Entry& e, const char* why)
    {
        if (e.off != NULL)
        {
            e.off = NULL;               // never dereference it again: it may be gone
            setReady(c, false);
            if (app != NULL)
            {
                sheatheDropMain(c, app, e);         // our bare main blade of a sheathe hold
                // a sheathe hold took the left blade off at its home frame already
                if (!e.sheatheOffHome || app->getAttachedEntity(std::string(e.slot)) != NULL)
                    app->detachItem(std::string(e.slot));
                if (restoreSheaths(c, app, e) == 0)
                    diag::count("visual_restore_none_yet");
            }
            e.restoreUntil = GetTickCount() + RESTORE_WATCH_MS;
            e.checkHandsAt = 0;
            diag::count(why);
        }
        e.sheathe = false;
        e.sheatheMain = false;
        e.sheatheOffHome = false;
        arms::release(c, arms::OWNER_DUAL);     // also when nothing was attached
    }

    // ---- the dual sheathe ----
    // The game's sheathe is one call with no animation (CharacterHuman::sheatheWeapon: the blade leaves `hands`,
    // the stance goes unarmed, the weapon hangs whole on its slot). Nothing of it is delayed: after it, for the
    // length of our clip, the left blade stays in the hand, the main weapon's bare blade is shown in the right
    // hand again, and each goes to its sheath at its own frame. Visual only.
    bool g_sheatheMain = true;          // [Animations] dual_sheathe_main_blade

    bool upright(Character* c)
    {
        return !c->isDown() && !c->isUnconcious() && !c->isRagdoll() && !c->isBeingCarried();
    }

    // The game sheathes for a goal that needs the hands (first aid, a splint, looting, lifting a body) and
    // starts it at once, so our flick and sheathe would play over it. They play only under a goal that
    // leaves the arms free: a fight, a move, standing. Any other goal, a job of another mod included, gets
    // the vanilla sheathe.
    bool g_sheatheYields = true;        // [Animations] sheathe_yields_to_jobs

    bool goalLeavesArmsFree(TaskType t)
    {
        switch (t)
        {
        case NULL_TASK: case IDLE: case UNEQUIP_WEAPON:
        case MELEE_ATTACK: case FOCUSED_MELEE_ATTACK: case UNPROVOKED_FOCUSED_MELEE_ATTACK:
        case CHOOSE_ENEMY_AND_ATTACK: case CHOOSE_ATTACKER_OF_ALLY: case ATTACK_CHARACTERS_ATTACKER:
        case ATTACK_ATTACKERS_OF: case PROTECT_ALLIES: case ATTACK_ENEMIES: case PROTECTION:
        case ATTACK_ENEMIES_AND_NEUTRALS: case SELF_PRESERVATION: case QUELL_AGGRESSION:
        case ATTACK_TROUBLE_MAKERS: case ATTACK_ENEMIES_OF_MY_SLAVEMASTER: case BODYGUARD: case CHASE:
        case GET_READY_FOR_ACTION: case MOVE_ON_FREE_WILL: case MOVE_CUS_ORDERED: case HOLD_POSITION:
        case STAY_CLOSE_TO_TARGET: case GET_NEAR_TO: case FOLLOW_PLAYER_ORDER: case STAND_STILL:
        case SQUAD_WAIT_FOR_ME:
            return true;
        default:
            return false;
        }
    }

    // The game plays a reaction on the body, so our pose steps aside (Arms.h Reacting): a stagger, down,
    // getting up, unconscious, playing dead, ragdoll, carried, dead. The setting nitoryu yield to reactions
    // ([Animations] yield_to_reactions) = 0 turns it off.
    int reactingOf(Character* c, CombatClass* cc)
    {
        if (!cfg::b(cfg::YIELD_TO_REACTIONS))
            return arms::REACT_NONE;
        if (c->isDead() || c->isDown() || c->isUnconcious() || c->isRagdoll() || c->isBeingCarried()
            || c->isCurrentlyGettingUp)
            return arms::REACT_DOWN;
        ProneState prone = c->getProneState();
        if (prone == PS_KO || prone == PS_PLAYING_DEAD)
            return arms::REACT_DOWN;
        if (cc != NULL && cc->combatState == STUMBLE)
            return arms::REACT_STAGGER;
        return arms::REACT_NONE;
    }

    // The goal that takes the hands, or NULL_TASK when the clip may play.
    TaskType handsGoal(Character* c)
    {
        if (!g_sheatheYields)
            return NULL_TASK;
        OrdersReceiver* o = c->getOrdersReciever();
        TaskType goal = o != NULL ? o->getCurrentGoal().key() : NULL_TASK;
        return goalLeavesArmsFree(goal) ? NULL_TASK : goal;
    }

    // Our bare main blade leaves `hands`. A weapon drawn meanwhile keeps the slot: the same item shows the same
    // entity the game would; another weapon gets its own blade there.
    void sheatheDropMain(Character* c, AppearanceBase* app, Entry& e)
    {
        if (!e.sheatheMain)
            return;
        e.sheatheMain = false;
        if (app == NULL || app->getAttachedEntity(std::string("hands")) == NULL)
            return;
        Weapon* hands = c->getCurrentWeapon();
        if (hands != NULL && hands == e.lastMain)
            return;
        // lastMain is only read while it still hangs on the body (it may be gone otherwise)
        if (hands != NULL && !(onBody(c, e.lastMain)
                               && slotState(app, std::string("hands"), e.lastMain, NULL) == SHOWS_OVERRIDE))
            return;                     // the game's own entity of the drawn weapon
        app->detachItem(std::string("hands"));
        if (hands != NULL)
        {
            std::string bare = kit::fcsFile(hands->data, "bare sword", "");
            if (!bare.empty())
                app->attachItem(hands, bare, std::string("hands"));
            diag::count("sheathe_main_replaced");
        }
    }

    // The chiburi: a back sheathe right after a fight plays the flick, a pause, then the same sheathe,
    // instead of the plain sheathe. "Right after a fight": the fighter had an attack target in combat mode
    // within the window (updateModes notes it every frame) and that target is dead, down or unconscious now.
    // The hip sheathe has its own chiburi (CLIP_CHIBURI_HIP), same rule.
    const double CHIBURI_WINDOW_MS = 8000.0;    // game time between the last fighting frame and the sheathe

    bool chiburiNow(Character* c, Entry& e, double t)
    {
        if (!arms::clipOn(e.offHip ? arms::CLIP_CHIBURI_HIP : arms::CLIP_CHIBURI))
            return false;
        const char* why = NULL;
        Character* foe = e.foughtAt > 0.0 ? e.foe.getCharacter() : NULL;
        double gap = t - e.foughtAt;
        if (e.foughtAt <= 0.0)
            why = "chiburi_skipped_no_fight";           // drawn with no enemy (the button, attack-move at nothing)
        else if (gap < 0.0 || gap > CHIBURI_WINDOW_MS)
            why = "chiburi_skipped_late";
        else if (foe == NULL)
            why = "chiburi_skipped_foe_gone";
        else if (!foe->isDead() && !foe->isDown() && !foe->isUnconcious())
            why = "chiburi_skipped_foe_up";             // he ran, or the player called the fighter off
        if (diag::takeDetail())
            kit::Diag("chiburi").kv("who", nameOf(c)).kv("foe", nameOf(foe)).kv("gap_ms", gap)
                .kv("plays", why == NULL ? "yes" : why).emit();
        e.foughtAt = 0.0;               // one fight, one flick
        if (why != NULL)
        {
            diag::count(why);
            return false;
        }
        return true;
    }

    // The tick after the game sheathed a dual fighter: start the hold. False: no hold (the caller detaches now).
    bool sheatheBegin(Character* c, AppearanceBase* app, Entry& e)
    {
        if (e.off == NULL || e.sheathe || !arms::clipOn(arms::CLIP_SHEATHE))
            return false;
        const char* why = NULL;
        if (e.slot != SLOT_PROP)
            why = "sheathe_hold_skipped_not_posed";
        else if (c->getCurrentWeapon() != NULL)
            why = "sheathe_hold_skipped_weapon_in_hands";       // the stance ended with a blade out: no sheathe
        else if (e.lastMain == NULL || !onBody(c, e.off) || !onBody(c, e.lastMain))
            why = "sheathe_hold_skipped_pair_not_on_body";
        else if (!upright(c))
            why = "sheathe_hold_skipped_down";
        else if (foreign::playing(e.fg))
            why = "sheathe_hold_skipped_foreign";     // the other plugin's swing plays: the blades go home now
        TaskType job = why == NULL ? handsGoal(c) : NULL_TASK;
        if (job != NULL_TASK)
        {
            why = "sheathe_hold_skipped_job";           // first aid, a splint, loot: the hands are taken
            e.foughtAt = 0.0;                           // the fight's flick is spent, as if it had played
            if (diag::takeDetail())
                kit::Diag("sheathe_job").kv("who", nameOf(c)).kv("goal", (int)job).kv("at", "begin").emit();
        }
        if (why != NULL)
        {
            diag::count(why);
            return false;
        }
        double t = defense::clockMs();
        arms::Clip k = chiburiNow(c, e, t) ? (e.offHip ? arms::CLIP_CHIBURI_HIP : arms::CLIP_CHIBURI)
            : e.offHip ? arms::CLIP_SHEATHE_HIP : arms::CLIP_SHEATHE;
        arms::playClip(c, k, t);
        if (!arms::clipPlaying(c, k))
        {
            diag::count("sheathe_hold_skipped_clip_busy");      // another clip plays (a draw just started, a hit)
            return false;
        }
        if (k == arms::CLIP_CHIBURI || k == arms::CLIP_CHIBURI_HIP)
            diag::count(k == arms::CLIP_CHIBURI_HIP ? "chiburi_hip_start" : "chiburi_start");
        e.sheathe = true;
        e.sheatheClip = k;
        e.sheatheHip = e.offHip;
        e.sheatheStart = t;
        e.sheatheOffHome = false;
        e.sheatheMain = false;
        setReady(c, false);             // no second blade for the combat code: the fighter is sheathed
        diag::count("sheathe_hold_start");
        if (e.sheatheHip)
            diag::count("sheathe_hold_hip");
        // the right blade: the game took it out of the hand already
        if (g_sheatheMain && g_itemOff >= 0 && e.lastMain != e.off
            && app->getAttachedEntity(std::string("hands")) == NULL)
        {
            std::string bare = kit::fcsFile(e.lastMain->data, "bare sword", "");
            if (!bare.empty() && app->attachItem(e.lastMain, bare, std::string("hands")))
            {
                e.sheatheMain = true;
                diag::count("sheathe_main_blade_shown");
            }
        }
        fixSheaths(c, app, e.sheatheMain ? e.lastMain : NULL, e.off, e);
        if (diag::takeDetail())
            kit::Diag("sheathe_hold").kv("who", nameOf(c)).kv("hip", e.sheatheHip).kv("main_blade", e.sheatheMain).emit();
        return true;
    }

    // Every tick of a hold. False: it is over (the caller detaches).
    bool sheatheHold(Character* c, AppearanceBase* app, Entry& e)
    {
        arms::Clip k = (arms::Clip)e.sheatheClip;
        moves::SheatheDef d = moves::sheathe(e.sheatheClip);
        float age = (float)((defense::clockMs() - e.sheatheStart) / 1000.0);
        const char* end = NULL;
        if (c->getCurrentWeapon() != NULL)
            end = "sheathe_hold_cut_redraw";
        else if (!upright(c))
            end = "sheathe_hold_cut_down";
        else if (!arms::clipPlaying(c, k))
            end = age >= d.lengthS - 0.15f ? "sheathe_hold_end" : "sheathe_hold_cut_clip";     // a swing, a block, a hit
        else if (age > d.lengthS + 0.4f || age < -0.5f)
            end = "sheathe_hold_timeout";               // not rendered: the clip never advanced
        TaskType job = end == NULL ? handsGoal(c) : NULL_TASK;
        if (job != NULL_TASK)
        {
            // a job that needs the hands started during the clip: it gives way, the blades go home now
            end = "sheathe_hold_cut_job";
            arms::stopClip(c, k);
            if (diag::takeDetail())
                kit::Diag("sheathe_job").kv("who", nameOf(c)).kv("goal", (int)job).kv("at", "hold").kv("age", age).emit();
        }
        if (end != NULL)
        {
            diag::count(end);
            return false;
        }
        if (e.sheatheMain && age >= d.mainHomeS)
        {
            Weapon* home = onBody(c, e.lastMain) ? e.lastMain : NULL;
            sheatheDropMain(c, app, e);
            if (home != NULL)
                restoreSheaths(c, app, e, home);
            diag::count("sheathe_main_home");
        }
        if (!e.sheatheOffHome && age >= d.offHomeS)
        {
            e.sheatheOffHome = true;
            if (app->getAttachedEntity(std::string(e.slot)) != NULL)
                app->detachItem(std::string(e.slot));
            if (onBody(c, e.off))
                restoreSheaths(c, app, e, e.off);
            diag::count("sheathe_off_home");
        }
        // the game hangs the sheathed weapons whole again on its own schedule: a blade still in a hand keeps
        // only its sheath on the body
        Weapon* main = e.sheatheMain && onBody(c, e.lastMain) ? e.lastMain : NULL;
        Weapon* off = !e.sheatheOffHome && onBody(c, e.off) ? e.off : NULL;
        if (main != NULL || off != NULL)
            fixSheaths(c, app, main, off, e);
        return true;
    }

    // The blades were drawn again as a dual pair during a hold: the clip gives way, the left blade stays (or is
    // attached again by update() when it had gone home).
    void sheatheCut(Character* c, AppearanceBase* app, Entry& e)
    {
        arms::stopClip(c, (arms::Clip)e.sheatheClip);
        sheatheDropMain(c, app, e);
        e.sheathe = false;
        if (e.sheatheOffHome)
            e.off = NULL;               // update() attaches it fresh (with the draw clip)
        else
            setReady(c, true);
        e.sheatheOffHome = false;
        diag::count("sheathe_hold_cut_redraw_dual");
    }

    // ---- the dual draw's hold ----
    // The game's draw is one call with no animation, like its sheathe: the main weapon is in `hands` at once and its
    // body slot keeps only the sheath. The draw clip starts standing with the arms down and takes the hilts on the
    // grip frame (moves::drawGripS), so until then both weapons hang whole on the back and the hands are empty: the
    // sheathe hold's calls in the other order. Visual only: the combat code gets its second blade at the grip, as it
    // did at the attach. A swing, a block or a hit that takes the clip ends the hold at once (the blades appear).
    bool g_drawHold = true;             // [Animations] dual_draw_hold

    bool drawHoldWanted(Character* c, const stance::Info& s, Entry& e)
    {
        // the item offset: fixSheaths gives the main weapon's slot its sheath back at the grip only with it.
        // The hip draw too: its clip starts with the arms down as well, the off blade stays on the hip
        // until the attach at the grip, as the back one stays on the back.
        return g_drawHold && g_pose && g_itemOff >= 0 && s.dual && s.main != NULL && s.off != NULL
            && s.main != s.off && arms::clipOn(s.offFromHip ? arms::CLIP_DRAW_HIP : arms::CLIP_DRAW)
            && stance::rigOf(c) != stance::RIG_FIXED_GRIP
            && upright(c) && !foreign::playing(e.fg) && !kit::fcsFile(s.main->data, "bare sword", "").empty();
    }

    // The main weapon out of `hands` and whole on its body slot (the game re-hangs it on its own schedule: every tick).
    void drawHideMain(AppearanceBase* app, Weapon* main, Entry& e)
    {
        if (app->getAttachedEntity(std::string("hands")) != NULL)
            app->detachItem(std::string("hands"));
        e.drawHidden = true;
        GameData* gd = main->getGameData();
        for (int i = 0; i < 3; ++i)
        {
            std::string slot(BODY_SLOTS[i]);
            if (slotState(app, slot, main, gd) != SHOWS_OVERRIDE)
                continue;
            app->detachItem(slot);
            app->attachItem(main, slot);
            diag::count("draw_hold_main_on_back");
            break;
        }
    }

    // The weapon in the hands shows in `hands` again (as sheatheDropMain puts a drawn weapon there). `sheaths`: its
    // body slot goes back to the sheath alone here (no attach follows that would do it).
    void drawShowMain(Character* c, AppearanceBase* app, Entry& e, bool sheaths)
    {
        if (!e.drawHidden)
            return;
        e.drawHidden = false;
        Weapon* hands = c->getCurrentWeapon();
        if (hands == NULL)
            return;                     // sheathed meanwhile: the game hung it whole itself
        if (app->getAttachedEntity(std::string("hands")) == NULL)
        {
            std::string bare = kit::fcsFile(hands->data, "bare sword", "");
            if (!bare.empty())
                app->attachItem(hands, bare, std::string("hands"));
        }
        if (sheaths)
            fixSheaths(c, app, hands, NULL, e);
    }

    // A draw was noted and has not settled: keep the hand empty from the first tick, or give the hold up.
    void drawHideTick(Character* c, AppearanceBase* app, Entry& e)
    {
        stance::Info s = stance::evaluate(c, NULL);
        if (drawHoldWanted(c, s, e))
            drawHideMain(app, s.main, e);
        else
        {
            e.drawHide = false;
            drawShowMain(c, app, e, true);
        }
    }

    // The settle is over and the left blade is not attached yet. False: no hold, the plain draw follows.
    bool drawHoldBegin(Character* c, AppearanceBase* app, const stance::Info& s, Entry& e)
    {
        const char* why = NULL;
        if (!arms::track(c, app->getSkeleton(), arms::OWNER_DUAL))
            why = "draw_hold_skipped_not_posed";
        double t = defense::clockMs();
        if (why == NULL)
        {
            e.drawClip = s.offFromHip ? arms::CLIP_DRAW_HIP : arms::CLIP_DRAW;
            arms::playClip(c, e.drawClip, t);
            if (!arms::clipPlaying(c, e.drawClip))
                why = "draw_hold_skipped_clip_busy";    // a swing, a block or a hit plays already
        }
        if (why != NULL)
        {
            diag::count(why);
            return false;
        }
        e.drawHold = true;
        e.drawStart = t;
        drawHideMain(app, s.main, e);
        diag::count("draw_hold_start");
        if (diag::takeDetail())
            kit::Diag("draw_hold").kv("who", nameOf(c)).kv("main", nameOf(s.main)).kv("off", nameOf(s.off)).emit();
        return true;
    }

    // Every tick of a hold. False: it is over, the blades go into the hands now (the caller attaches).
    bool drawHoldTick(Character* c, AppearanceBase* app, const stance::Info& s, Entry& e)
    {
        float age = (float)((defense::clockMs() - e.drawStart) / 1000.0);
        const char* end = NULL;
        // half a tick early: a blade that appears a frame before the fist closes reads better than one after the pull
        if (age >= moves::drawGripS() - 0.05f)
            end = "draw_hold_grip";
        else if (!upright(c))
            end = "draw_hold_cut_down";
        else if (!arms::clipPlaying(c, e.drawClip))
            end = "draw_hold_cut_clip";                 // a swing, a block, a hit
        else if (foreign::playing(e.fg))
            end = "draw_hold_cut_foreign";
        else if (age < -0.5f)
            end = "draw_hold_timeout";
        if (end == NULL)
        {
            drawHideMain(app, s.main, e);
            return true;
        }
        diag::count(end);
        e.drawHold = false;
        e.drawHeld = true;              // the clip plays already: the attach does not start it again
        drawShowMain(c, app, e, false);
        return false;
    }

    // The stance ended (or the entry is taken down) before the grip: nothing of the hold stays.
    void drawHoldDrop(Character* c, AppearanceBase* app, Entry& e)
    {
        if (e.drawHold)
        {
            arms::stopClip(c, e.drawClip);
            diag::count("draw_hold_cut_not_dual");
        }
        e.drawHold = false;
        e.drawHide = false;
        e.drawHeld = false;
        if (app != NULL)
            drawShowMain(c, app, e, true);
        e.drawHidden = false;
    }

    // One second after a draw: which pair member is in the hands, and where each hangs.
    void logHands(Character* c, const stance::Info& s, const Entry& e)
    {
        Weapon* hands = c->getCurrentWeapon();
        Inventory* inv = c->getInventory();
        diag::count(hands == s.main ? "visual_hands_main" : (hands == e.off ? "visual_hands_off" : "visual_hands_other"));
        if (inv == NULL || !diag::takeDetail())
            return;
        kit::Diag("visual_hands").kv("who", nameOf(c)).kv("in_hands", nameOf(hands))
            .kv("hands_back", indexInSection(inv, "back", hands)).kv("hands_hip", indexInSection(inv, "hip", hands))
            .kv("off", nameOf(e.off)).kv("off_back", indexInSection(inv, "back", e.off))
            .kv("off_hip", indexInSection(inv, "hip", e.off)).kv("hidden", hiddenText(e)).emit();
    }

    void update(Character* c, Entry& e, DWORD now)
    {
        AppearanceBase* app = c->getAppearance();
        if (app == NULL)
            return;
        if (!reached(now, e.settleAt))
        {
            if (e.drawHide)
                drawHideTick(c, app, e);        // the dual draw's hold: the hand is empty before the settle ends
            return;
        }
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual)
        {
            if (e.drawHide || e.drawHold || e.drawHidden)
                drawHoldDrop(c, app, e);
            // a sheathe: our clip plays first, the blades go home at their frames, then the detach
            if (e.sheathe ? sheatheHold(c, app, e) : sheatheBegin(c, app, e))
                return;
            detach(c, app, e, "visual_detached_not_dual");
            // The game re-hangs the sheathed weapons on its own schedule: until
            // the watch ends, with nothing in the hands, no body slot should
            // show only the off weapon's sheath.
            if (e.off == NULL && e.offData != NULL && e.restoreUntil != 0 && !reached(now, e.restoreUntil)
                && c->getCurrentWeapon() == NULL)
                restoreSheaths(c, app, e);
            return;
        }
        if (e.sheathe)
            sheatheCut(c, app, e);
        if (e.off != NULL && e.off != s.off)
        {
            if (e.off == s.main && s.off->data == s.main->data)
                rebind(c, app, s.main, s.off, e);   // identical pair members traded hands
            else
                detach(c, app, e, "visual_detached_pair_changed");
            if (e.off != NULL)
                return;
        }
        e.lastMain = s.main;
        e.offHip = s.offFromHip;
        if (e.off == NULL)
        {
            // the dual draw's hold: the clip plays from here, the blades come into the hands at the grip frame
            if (e.drawHold)
            {
                if (drawHoldTick(c, app, s, e))
                    return;
            }
            else if (e.drawHide)
            {
                e.drawHide = false;
                if (drawHoldWanted(c, s, e) && drawHoldBegin(c, app, s, e))
                    return;
                drawShowMain(c, app, e, false);         // no hold: the blade back in the hand, the plain draw
            }
            bool held = e.drawHeld;
            e.drawHeld = false;
            if (attach(c, app, s.main, s.off, e))
            {
                e.checkHandsAt = now + CHECK_HANDS_MS;
                // the dual draw; from the hip when the second blade is worn there. Not under another
                // plugin's swing (its switch is instant, mid-swing too).
                if (foreign::playing(e.fg))
                    diag::count("foreign_draw_clip_skipped");
                else if (held)
                    diag::count("draw_hold_blades_in_hands");
                else
                    arms::playClip(c, s.offFromHip ? arms::CLIP_DRAW_HIP : arms::CLIP_DRAW, defense::clockMs());
            }
            return;
        }
        // A reload of the character's attachments drops ours: put it back. The slot can also end up
        // showing something else (two identical weapons without a sheath mesh). Once a second, check it
        // shows the blade's own mesh.
        bool wrong = false;
        // The game re-draws the weapon mid-fight, which hides our entity while the slot still looks
        // taken. After every draw noted while the blade is on, attach it fresh.
        if (e.refresh)
        {
            e.refresh = false;
            wrong = true;
            diag::count("visual_refresh_after_draw");
        }
        else if (e.refreshLateAt != 0 && reached(now, e.refreshLateAt))
        {
            e.refreshLateAt = 0;
            wrong = true;
            diag::count("visual_refresh_late");
        }
        if (reached(now, e.verifyAt))
        {
            e.verifyAt = now + VERIFY_MS;
            bool present = app->getAttachedEntity(std::string(e.slot)) != NULL;
            bool right = present && slotState(app, std::string(e.slot), e.off, e.offData) == SHOWS_OVERRIDE;
            if (right)
                e.verifiedOk = true;
            // only once the check has read our slot right at least once (no re-attach loop if
            // the entity cannot be read on some build)
            wrong = wrong || (present && !right && e.verifiedOk);
            if (wrong)
                diag::count("visual_reattach_wrong_entity");
            else if (present && !right)
                diag::count("visual_verify_unreadable");
        }
        if (wrong || app->getAttachedEntity(std::string(e.slot)) == NULL)
        {
            if (wrong && app->getAttachedEntity(std::string(e.slot)) != NULL)
                app->detachItem(std::string(e.slot));
            diag::count("visual_reattach");
            Weapon* off = e.off;
            e.off = NULL;
            setReady(c, false);
            attach(c, app, s.main, off, e);
            return;
        }
        // A rebuilt appearance has a new skeleton: pose it too (a no-op
        // otherwise); if it cannot be posed, re-attach on the hand slot.
        if (e.slot == SLOT_PROP && !arms::track(c, app->getSkeleton(), arms::OWNER_DUAL))
        {
            detach(c, app, e, "visual_detached_rig_changed");
            return;
        }
        fixSheaths(c, app, s.main, e.off, e);
        if (e.checkHandsAt != 0 && reached(now, e.checkHandsAt))
        {
            e.checkHandsAt = 0;
            logHands(c, s, e);
        }
    }

    // ---- left-hand fighting ------------------------------------------------------
    // The drawn weapon hangs on `hands` (Prop2, the crippled right hand). While
    // the fighter is a left-hander, its bare blade hangs on our Prop1 slot, the
    // `hands` entity is removed, and the pose engine mirrors the whole body.
    struct LeftyEntry
    {
        hand who;
        Weapon* w;                  // shown in the left hand, or NULL
        GameData* data;
    };
    std::map<Character*, LeftyEntry> g_lefties;     // main loop only

    void noteLefty(Character* c)
    {
        if (g_lefties.find(c) != g_lefties.end())
            return;
        LeftyEntry e;
        e.who = c->getHandle();
        e.w = NULL;
        e.data = NULL;
        g_lefties[c] = e;
        diag::count("lefty_noted");
    }

    void leftyEnd(Character* c, AppearanceBase* app, LeftyEntry& e)
    {
        if (e.w == NULL)
        {
            arms::release(c, arms::OWNER_LEFTY);    // tracked before the attach failed
            return;
        }
        if (app != NULL)
        {
            app->detachItem(std::string(SLOT_PROP));
            Weapon* hands = c->getCurrentWeapon();
            // Still drawn (the arm healed): give the right hand its blade back.
            if (hands != NULL && hands->getGameData() == e.data && app->getAttachedEntity("hands") == NULL)
            {
                std::string bare = kit::fcsFile(hands->data, "bare sword", "");
                if (!bare.empty())
                    app->attachItem(hands, bare, "hands");
            }
        }
        e.w = NULL;
        e.data = NULL;
        arms::release(c, arms::OWNER_LEFTY);
        diag::count("lefty_visual_end");
    }

    // True while the entry is still wanted.
    bool leftyUpdate(Character* c, LeftyEntry& e)
    {
        AppearanceBase* app = c->getAppearance();
        Weapon* w = c->getCurrentWeapon();
        if (app == NULL || !lefty::active(c) || w == NULL)
        {
            leftyEnd(c, app, e);
            return lefty::qualifies(c);
        }
        if (e.w != NULL && e.w != w)
            leftyEnd(c, app, e);
        if (!arms::track(c, app->getSkeleton(), arms::OWNER_LEFTY)
            || !ensureSlot(app, SLOT_PROP, "Bip01 Prop1", Ogre::Vector3::ZERO, Ogre::Quaternion::IDENTITY))
        {
            diag::count("lefty_visual_unsupported");
            leftyEnd(c, app, e);                    // no mirrored body without the blade
            return true;
        }
        if (e.w == NULL || app->getAttachedEntity(std::string(SLOT_PROP)) == NULL)
        {
            std::string bare = kit::fcsFile(w->data, "bare sword", "");
            if (bare.empty() || !app->attachItem(w, bare, std::string(SLOT_PROP)))
            {
                diag::count("lefty_visual_attach_failed");
                if (e.w != NULL)
                    app->detachItem(std::string(SLOT_PROP));
                e.w = NULL;
                e.data = NULL;
                arms::release(c, arms::OWNER_LEFTY);
                return true;
            }
            e.w = w;
            e.data = w->getGameData();
            diag::count("lefty_visual_attached");
            if (diag::takeDetail())
                kit::Diag("lefty_visual").kv("who", nameOf(c)).kv("weapon", nameOf(w)).kv("bare", bare).emit();
        }
        // The game re-hangs the blade on the crippled hand now and then.
        if (app->getAttachedEntity("hands") != NULL)
        {
            app->detachItem(std::string("hands"));
            diag::count("lefty_hands_hidden");
        }
        return true;
    }

    // Every frame: the pose mode follows the fighter's combat state. A technique
    // is playing while combatState is CHOP_WEAPON; a Nitoryu one (a right-hand
    // sword track) is mirrored into a left-hand swing, a main-hand one holds the
    // left blade in guard. Game pause freezes the state, so the pose freezes too.
    void updateModes()
    {
        for (std::map<Character*, Entry>::iterator it = g_entries.begin(); it != g_entries.end(); ++it)
        {
            if (it->second.off == NULL)
            {
                // the watch goes on while the second blade is away, so the next draw knows whether
                // another plugin's swing plays under it
                Character* away = foreign::active() ? it->second.who.getCharacter() : NULL;
                if (away != NULL && away == it->first)
                    foreign::actionNow(away, away->getCombatClass(), it->second.fg, defense::clockMs());
                continue;
            }
            Character* c = it->second.who.getCharacter();
            if (c == NULL || c != it->first)
            {
                // Gone. Its State goes now, not at the next slow tick: the freed skeleton can be
                // re-allocated for another character within frames.
                arms::untrack(it->first);
                setReady(it->first, false);
                it->second.off = NULL;
                diag::count("arms_released_dead");
                continue;
            }
            // Dual Wielding also grows from weapon XP that another plugin gave (throttled inside)
            ambidex::watchWeaponXp(c);
            // A rebuilt appearance has a new skeleton; pose that one from this frame (a no-op when it
            // is the tracked one), or fall back to the hand slot.
            if (it->second.slot == SLOT_PROP)
            {
                AppearanceBase* app = c->getAppearance();
                Ogre::Skeleton* sk = app != NULL ? app->getSkeleton() : NULL;
                if (sk != arms::skeletonOf(c))
                    diag::count("arms_skeleton_changed");
                if (sk == NULL || !arms::track(c, sk, arms::OWNER_DUAL))
                {
                    detach(c, app, it->second, "visual_detached_rig_changed");
                    continue;
                }
            }
            combat::enforceSpeed(c);        // the dual speed stays in the stored field
            ambidex::noteRank(c);           // the rank-up moment
            CombatClass* cc = c->getCombatClass();
            arms::Mode m = arms::MIRROR;
            if (cc != NULL && cc->combatState == CHOP_WEAPON && cc->currentTechnique != NULL)
            {
                CombatTechniqueData* t = cc->currentTechnique;
                // A Nitoryu move's mode is its record's (`nitoryu arms`): a strike mirrors the body
                // (SWAP), a twin, finisher or opener plays on both blades (TWIN), unless the record says else.
                int own = combat::armsModeOf(t);
                if (own >= 0)
                    m = (arms::Mode)own;
                else if (combat::twinsForeignDash(c, t))
                {
                    m = arms::TWIN;         // another mod's dash, both blades
                    if (diag::stage())
                        diag::count("foreign_dash_twin_frame");
                }
                else
                    m = arms::HOLD;
            }
            else if (cc != NULL && (cc->combatState == BLOCK || cc->combatState == REACTION_BLOCK))
                m = arms::BLOCK;        // a block is one-handed: the left arm keeps its guard
            // The combat state can leave CHOP_WEAPON while a tailed clip's base track still plays, and
            // the base's own ending would show. And the progress can repeat or step back for a frame
            // (the hit, a frame with no animation update), which would flash the base's pose. So the
            // clip's progress only goes forward, holds through stalls, and the clip stays on until the
            // track ends (or ~0.5 s without progress).
            Entry& ent = it->second;
            // The game sets the technique and starts its track at once (AttackState::initialiseAttack),
            // but combatState turns CHOP_WEAPON later. So a tailed clip is detected by its technique and
            // its track's progress, whatever the state: the base's first frames never show. Its
            // follow-through plays to the track's end; then the mode blend brings the blade back to the
            // stance.
            int curClip = cc != NULL ? combat::tailClip(cc->currentTechnique) : -1;
            CombatTechniqueData* cur = curClip >= 0 ? cc->currentTechnique : NULL;
            float curProg = cur != NULL && cc->animation != NULL ? cc->animation->getAnimationProgress(cur) : -1.0f;
            bool dashNow = cur != NULL && curProg > 0.001f && curProg < 0.999f;     // 0 = not playing
            arms::Mode curMode = moves::twinClip(curClip) ? arms::TWIN : arms::SWAP;
            if (dashNow && m != curMode && (moves::longTail(curClip) || (m != arms::BLOCK && !ent.dashDone)))
            {
                if (curClip == arms::CLIP_DASH)
                    diag::count(cc->combatState == CHOP_WEAPON ? "dash_mode_forced" : "dash_before_chop_state");
                else
                    diag::count(cc->combatState == CHOP_WEAPON ? "attack_tail_mode_forced" : "attack_tail_before_chop_state");
                m = curMode;
            }
            float dashTail = -1.0f;
            if (!dashNow)
                ent.dashDone = false;
            if (dashNow && ent.dashDone && curProg < 0.3f && curProg < ent.dashProg - 0.3f)
                ent.dashDone = false;           // a new Dash with the same technique pointer
            if (dashNow && ent.dashDone)
                dashNow = false;                // this Dash finished: the game's pose until it ends
            if (dashNow && cur != ent.dashTech)
            {
                ent.dashTech = cur;
                ent.dashProg = 0.0f;
                ent.dashStall = 0;
                ent.dashClip = curClip;
            }
            // A long tail (the Dash, the spin) wins over anything but a block; another tailed clip only
            // over the plain guard or its own mode: a new swing of the other blade takes the arms at once.
            bool tailDash = moves::longTail(ent.dashClip);
            arms::Mode tailMode = moves::twinClip(ent.dashClip) ? arms::TWIN : arms::SWAP;
            bool tailFree = tailDash ? m != arms::BLOCK : (m == arms::MIRROR || m == tailMode);
            if (ent.dashTech != NULL && cc != NULL && cc->animation != NULL && tailFree)
            {
                float p = cc->animation->getAnimationProgress(ent.dashTech);
                if (p > ent.dashProg + 0.0001f && p <= 1.05f)
                {
                    ent.dashProg = p;
                    ent.dashStall = 0;
                }
                else if (dashNow && p < ent.dashProg - 0.3f)
                {
                    ent.dashProg = p;           // a new Dash with the same technique
                    ent.dashStall = 0;
                }
                else
                {
                    ++ent.dashStall;
                    if (tailDash)
                        diag::count(p < ent.dashProg - 0.0001f ? "dash_progress_back" : "dash_progress_still");
                }
                if (ent.dashProg >= 0.999f || ent.dashStall > 30)
                {
                    if (tailDash)
                        diag::count(ent.dashProg >= 0.999f ? "dash_clip_end" : "dash_clip_stalled");
                    else
                        diag::count(ent.dashProg >= 0.999f ? "attack_tail_end" : "attack_tail_stalled");
                    ent.dashTech = NULL;
                    ent.dashDone = dashNow;
                }
                else
                {
                    dashTail = ent.dashProg;
                    if (!dashNow)
                        diag::count(tailDash ? "dash_tail_kept" : "attack_tail_kept");
                    m = tailMode;
                }
            }
            else if (!dashNow)
                ent.dashTech = NULL;
            else if (ent.dashTech != NULL && !tailFree && !tailDash)
            {
                diag::count("attack_tail_cut");        // a new swing or a block took the arms
                ent.dashTech = NULL;
                ent.dashDone = true;
            }
            // Down, knocked out, ragdolled, carried, getting up: no attack clip or tail goes on over the
            // fall; the pose engine keeps only the left arm's mirror (Arms.cpp).
            const int react = reactingOf(c, cc);
            if (react == arms::REACT_DOWN)
            {
                if (ent.dashTech != NULL || dashTail >= 0.0f)
                {
                    diag::count("attack_tail_cut_down");
                    ent.dashTech = NULL;
                    ent.dashDone = dashNow;
                    dashTail = -1.0f;
                }
                m = arms::MIRROR;
            }
            // Mode, running, attack clip and parry go to the pose engine in one call.
            arms::FrameInput fin;
            fin.mode = m;
            fin.reacting = react;
            fin.inCombat = cc != NULL && cc->_isInCombatMode();
            // Another plugin swings or blocks for this fighter outside the game's combat: the clips give
            // way (Arms.cpp), and for hold_ms no draw or sheathe clip starts.
            double fgNow = defense::clockMs();
            fin.foreign = foreign::actionNow(c, cc, ent.fg, fgNow);
            // the chiburi: fighting someone this frame; the sheathe asks how long ago and whether he fell
            if (fin.inCombat)
            {
                hand foe = cc->_getAttackTarget();
                if (foe.getCharacter() != NULL)
                {
                    ent.foughtAt = defense::clockMs();
                    ent.foe = foe;
                }
            }
            // Which way the blow comes while blocking: the block technique's impact direction, else the
            // attacker's swing (picks the overhead / angled / scissor guard).
            fin.blockDir = -1;
            if (m == arms::BLOCK && cc != NULL)
            {
                CombatTechniqueData* bt = cc->currentTechnique;
                if (bt != NULL && bt->numImpactPoints() > 0 && bt->impactPoint(0) != NULL)
                    fin.blockDir = (int)bt->impactPoint(0)->direction;
                if (fin.blockDir <= 0)
                {
                    Character* foe = cc->_getAttackTarget().getCharacter();
                    CombatClass* fc = foe != NULL ? foe->getCombatClass() : NULL;
                    CombatTechniqueData* ft = fc != NULL ? fc->currentTechnique : NULL;
                    if (ft != NULL && ft->numImpactPoints() > 0 && ft->impactPoint(0) != NULL)
                        fin.blockDir = (int)ft->impactPoint(0)->direction;
                }
            }
            // Frames in which this dual fighter's target is staggering (the Cross's trigger).
            // Per-frame counters only in stage (tests), not in players' games.
            bool stage = diag::stage();
            if (stage && cc != NULL && cc->_isInCombatMode())
            {
                Character* t = cc->_getAttackTarget().getCharacter();
                CombatClass* tc = t != NULL ? t->getCombatClass() : NULL;
                if (tc != NULL)
                {
                    diag::count(tc->combatState == STUMBLE ? "target_stumble_frame" : "target_steady_frame");
                    // every state the target passes through (swordStateEnum as a number)
                    char key[40];
                    sprintf_s(key, "target_state_%d", (int)tc->combatState);
                    diag::count(key);
                }
                // Mid-swing frames whose game heading points away from the target, for this dual fighter
                // and for its opponent (a baseline: if one-handers do it as often, it is the game or the
                // modpack, not the pose engine).
                if (t != NULL && cc->combatState == CHOP_WEAPON)
                {
                    float d = defense::headingDot(c, t);
                    if (d <= 1.5f)
                    {
                        diag::count("swing_frame_dual");
                        if (d < 0.0f)
                        {
                            diag::count("swing_heading_away_dual");
                            if (diag::takeDetail())
                                kit::Diag("swing_heading_away").kv("who", c->displayName).kv("dual", 1)
                                    .kv("tech", cc->currentTechnique != NULL ? cc->currentTechnique->animation : std::string("-"))
                                    .kv("dot", d).kv("target", t->displayName).emit();
                        }
                    }
                }
                if (tc != NULL && tc->combatState == CHOP_WEAPON)
                {
                    Character* tt = tc->_getAttackTarget().getCharacter();
                    float d = tt != NULL ? defense::headingDot(t, tt) : 2.0f;
                    if (d <= 1.5f)
                    {
                        diag::count("swing_frame_other");
                        if (d < 0.0f)
                            diag::count("swing_heading_away_other");
                    }
                }
            }
            // The run holds: isRunning = moving faster than walking right now.
            CharMovement* mv = c->getMovement();
            fin.running = foreign::running(mv != NULL && mv->isRunning(), ent.fg, fgNow);     // a steady run
            // walking up to the enemy keeps the idle stance's arms over the game's legs
            fin.moving = c->getMovementSpeed() > 1.0f;
            // the rank picks the idle stance (the cached evaluation, the tier the moves use)
            fin.rank = fin.inCombat ? stance::evaluate(c, NULL).tier : 0;
            // Attack clips follow the technique's own progress (Arms.h).
            int attack = -1;
            float progress = 0.0f;
            if (dashTail >= 0.0f)
            {
                attack = ent.dashClip;
                progress = dashTail;
                ent.dashProg = dashTail;
            }
            else if (m == arms::TWIN && cc->animation != NULL)
            {
                attack = combat::attackClip(cc->currentTechnique);
                if (attack >= 0)
                {
                    progress = cc->animation->getAnimationProgress(cc->currentTechnique);
                    // Expected 0-1 (_iHitYouAreYouHit compares it with 0.5); count what we see.
                    if (stage)
                        diag::count(progress > 1.05f ? "attack_progress_over1" : progress < 0.5f ? "attack_progress_first_half"
                                    : "attack_progress_second_half");
                }
            }
            fin.attackClip = attack;
            fin.attackProgress = progress;
            fin.nowMs = defense::clockMs();
            double parry = defense::lastParryMs(c);
            fin.parryStart = parry >= 0.0 && fin.nowMs - parry < 1000.0 ? parry : -1.0;
            double blocked = defense::lastMainBlockMs(c);
            fin.blockHitStart = blocked >= 0.0 && fin.nowMs - blocked < 1000.0 ? blocked : -1.0;
            fin.parryBack = fin.parryStart >= 0.0 && defense::lastParryBack(c);
            fin.blockHitBack = fin.blockHitStart >= 0.0 && defense::lastMainBlockBack(c);
            arms::setFrame(c, fin);
        }
        for (std::map<Character*, LeftyEntry>::iterator it = g_lefties.begin(); it != g_lefties.end(); ++it)
        {
            if (it->second.w == NULL)
                continue;
            Character* c = it->second.who.getCharacter();
            if (c == NULL || c != it->first)
            {
                arms::untrack(it->first);
                it->second.w = NULL;
                diag::count("arms_released_dead");
                continue;
            }
            AppearanceBase* app = c->getAppearance();
            Ogre::Skeleton* sk = app != NULL ? app->getSkeleton() : NULL;
            if (sk == NULL || !arms::track(c, sk, arms::OWNER_LEFTY))
            {
                leftyEnd(c, app, it->second);
                continue;
            }
            CombatClass* cc = c->getCombatClass();
            arms::setMode(c, arms::LEFTY, cc != NULL && cc->_isInCombatMode());
            arms::setReacting(c, reactingOf(c, cc));        // no whole-body mirror over a fall
        }
        arms::emitClipLog();
    }

    // Every counted change of the weapon in the hands, with which pair member it was and is.
    int g_drawLogLeft = 60;

    const char* roleOf(const Weapon* w, const Entry& e)
    {
        return w == NULL ? "none" : w == e.lastMain ? "main" : w == e.lastOff ? "off" : "other";
    }

    std::string ptrText(const void* p)
    {
        char b[24];
        sprintf_s(b, "%p", p);
        return b;
    }

    void logDraw(Character* c, const Entry& e, Weapon* hands, bool sameKind)
    {
        const char* kind = hands == NULL ? "sheathe"
                         : !e.seenHands || e.lastHands == NULL ? "draw"
                         : !sameKind ? "changed"
                         : hands == e.lastOff && e.lastHands == e.lastMain ? "pair_swap" : "same_kind";
        char key[48];
        sprintf_s(key, "visual_draw_kind_%s", kind);
        diag::count(key);
        if (g_drawLogLeft <= 0)
            return;
        --g_drawLogLeft;
        kit::Diag("visual_draw").kv("who", nameOf(c)).kv("t", defense::clockMs()).kv("kind", kind)
            .kv("hands", roleOf(hands, e)).kv("was", roleOf(e.lastHands, e)).kv("attached", e.off != NULL)
            .kv("hands_ptr", ptrText(hands)).kv("was_ptr", ptrText(e.lastHands)).emit();
    }

    // The feature was turned off at run time: every blade goes back, every pose-engine State is
    // released.
    void shutdownAll()
    {
        for (std::map<Character*, Entry>::iterator it = g_entries.begin(); it != g_entries.end(); ++it)
        {
            Character* c = it->second.who.getCharacter();
            if (c != NULL && c == it->first)
            {
                drawHoldDrop(c, c->getAppearance(), it->second);
                detach(c, c->getAppearance(), it->second, "visual_detached_disabled");
            }
            else
                arms::untrack(it->first);
            setReady(it->first, false);
        }
        g_entries.clear();
        for (std::map<Character*, LeftyEntry>::iterator it = g_lefties.begin(); it != g_lefties.end(); ++it)
        {
            Character* c = it->second.who.getCharacter();
            if (c != NULL && c == it->first)
                leftyEnd(c, c->getAppearance(), it->second);
            else
                arms::untrack(it->first);
        }
        g_lefties.clear();
        EnterCriticalSection(&g_lock);
        g_pending.clear();
        LeaveCriticalSection(&g_lock);
        diag::count("visual_shutdown");
    }

    // [Animations] <key>, else the older [Test] <legacy>, else on.
    bool animSwitch(const std::string& path, const char* key, const char* legacy)
    {
        int v = GetPrivateProfileIntA("Animations", key, -1, path.c_str());
        // the Cross finisher's clip is the X cut; a player's old `cross_finisher=` still counts
        if (v < 0 && strcmp(key, "x_cut") == 0)
            v = GetPrivateProfileIntA("Animations", "cross_finisher", -1, path.c_str());
        if (v < 0)
            v = GetPrivateProfileIntA("Test", legacy, 1, path.c_str());
        return v != 0;
    }
}

namespace visual
{
    bool bladeReady(Character* c)
    {
        EnterCriticalSection(&g_lock);
        bool r = g_ready.find(c) != g_ready.end();
        LeaveCriticalSection(&g_lock);
        return r;
    }

    void init()
    {
        InitializeCriticalSection(&g_lock);
        std::string path = iniPath();
        g_enabled = GetPrivateProfileIntA("OffHand", "show_blade", 1, path.c_str()) != 0;
        char bone[64] = { 0 };
        GetPrivateProfileStringA("OffHand", "bone", "", bone, sizeof(bone), path.c_str());
        if (bone[0] != '\0')
            g_bone = bone;
        float v[4];
        if (readFloats(path, "pos", v, 3) == 3)
            g_pos = Ogre::Vector3(v[0], v[1], v[2]);
        if (readFloats(path, "rot", v, 4) == 4)
            g_rot = Ogre::Quaternion(v[0], v[1], v[2], v[3]);
        g_pose = GetPrivateProfileIntA("OffHand", "mirror_pose", 1, path.c_str()) != 0;
        char square[32] = { 0 };
        GetPrivateProfileStringA("OffHand", "square_torso", "0.8", square, sizeof(square), path.c_str());
        char uncross[32] = { 0 };
        GetPrivateProfileStringA("OffHand", "uncross", "0.7", uncross, sizeof(uncross), path.c_str());
        char pose[32] = { 0 };
        GetPrivateProfileStringA("Guard", "parry_pose", "1.0", pose, sizeof(pose), path.c_str());
        arms::initParry((unsigned)GetPrivateProfileIntA("Guard", "parry_ms", 650, path.c_str()), (float)atof(pose));
        arms::init((float)atof(square), (float)atof(uncross),
                   (unsigned)GetPrivateProfileIntA("OffHand", "blend_ms", 80, path.c_str()),
                   GetPrivateProfileIntA("OffHand", "swap_strikes", 1, path.c_str()) != 0);
        // [Animations], on by default: one switch per row of Moves.h CLIPS; an older ini may still
        // have [Test] l3_*.
        for (int k = 0; k < arms::CLIP_COUNT; ++k)
            arms::setClipOn(moves::CLIPS[k].clip, animSwitch(path, moves::CLIPS[k].iniKey, moves::CLIPS[k].legacyKey));
        // the angled blocks' sides, if they show mirrored in game
        arms::setAngledSwap(GetPrivateProfileIntA("Animations", "angled_block_swap", 0, path.c_str()) != 0);
        // the crippled arm of a left-hand fighter swings (0: the fixed pose, straight down)
        arms::setLimpSwing(GetPrivateProfileIntA("Animations", "limp_arm_swing", 1, path.c_str()) != 0);
        // the dual sheathe shows the main blade in the right hand until it is home
        g_sheatheMain = GetPrivateProfileIntA("Animations", "dual_sheathe_main_blade", 1, path.c_str()) != 0;
        g_sheatheYields = GetPrivateProfileIntA("Animations", "sheathe_yields_to_jobs", 1, path.c_str()) != 0;
        // the dual draw: both blades stay on the back until the hands take the hilts (0: in the hands from the start)
        g_drawHold = GetPrivateProfileIntA("Animations", "dual_draw_hold", 1, path.c_str()) != 0;
        // The moves themselves (not only their look): the dash changes how far the AI engages.
        combat::setDashOn(GetPrivateProfileIntA("Moves", "dash_slash", 1, path.c_str()) != 0);
    }

    void setEnabled(bool on) { g_live = on; }
    void setPoseEngine(bool on) { g_pose = on; }
    bool poseEngine() { return g_pose; }

    void noteDraw(Character* c)
    {
        if (!g_enabled || !g_live || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        g_pending.push_back(c->getHandle());
        LeaveCriticalSection(&g_lock);
    }

    void noteSheathe(Character* c)
    {
        if (!g_enabled || !g_live || c == NULL)
            return;
        EnterCriticalSection(&g_lock);
        if (g_pendingSheathe.size() < 4096)
            g_pendingSheathe.push_back(c->getHandle());
        LeaveCriticalSection(&g_lock);
    }

    void tick()
    {
        if (!g_enabled)
            return;
        if (!g_live)
        {
            if (!g_entries.empty() || !g_lefties.empty())
                shutdownAll();
            return;
        }
        if (g_pose)
            updateModes();
        DWORD now = GetTickCount();
        if (now - g_lastTick < TICK_MS)
            return;
        g_lastTick = now;
        std::vector<hand> pending, sheathes;
        EnterCriticalSection(&g_lock);
        pending.swap(g_pending);
        sheathes.swap(g_pendingSheathe);
        LeaveCriticalSection(&g_lock);
        // A sheathe matters only to a character we already track; every other NPC's sheathe
        // (hundreds a session) is dropped here.
        for (size_t i = 0; i < sheathes.size(); ++i)
        {
            Character* c = sheathes[i].getCharacter();
            if (c != NULL && g_entries.find(c) != g_entries.end())
                pending.push_back(sheathes[i]);
            else
                diag::count("visual_sheathe_untracked");
        }
        for (size_t i = 0; i < pending.size(); ++i)
        {
            Character* c = pending[i].getCharacter();
            if (c == NULL)
                continue;
            std::map<Character*, Entry>::iterator found = g_entries.find(c);
            if (found == g_entries.end())
            {
                Entry e;
                e.who = pending[i];
                e.off = NULL;
                e.slot = SLOT_HAND;
                e.offData = NULL;
                e.lastOff = NULL;
                e.lastMain = NULL;
                for (int k = 0; k < 3; ++k)
                {
                    e.hidden[k] = NULL;
                    e.hiddenData[k] = NULL;
                }
                e.checkHandsAt = 0;
                e.restoreUntil = 0;
                e.verifyAt = 0;
                e.verifiedOk = false;
                e.refresh = false;
                e.refreshLateAt = 0;
                e.lastHands = NULL;
                e.lastHandsData = NULL;
                e.seenHands = false;
                e.dashTech = NULL;
                e.dashProg = 0.0f;
                e.dashStall = 0;
                e.dashDone = false;
                e.dashClip = -1;
                e.offHip = false;
                e.sheathe = false;
                e.sheatheHip = false;
                e.sheatheStart = 0.0;
                e.sheatheMain = false;
                e.sheatheOffHome = false;
                e.sheatheClip = arms::CLIP_SHEATHE;
                e.drawHide = false;
                e.drawHold = false;
                e.drawHeld = false;
                e.drawHidden = false;
                e.drawStart = 0.0;
                e.foughtAt = 0.0;
                found = g_entries.insert(std::make_pair(c, e)).first;
            }
            // With some mods setWeapon runs constantly without any change, and each call would push
            // the settle time on, so the blade would never be attached or detached in a fight. Only a
            // real change of the weapon in the hands counts as a draw.
            Weapon* hands = c->getCurrentWeapon();
            Entry& e = found->second;
            if (e.seenHands && hands == e.lastHands)
            {
                diag::count("visual_draw_same_weapon");
                continue;
            }
            // A weapon of the same kind replacing another in the hands (two identical katanas trading
            // places, which setWeapon also does twice per vanilla draw) is no draw: update() re-binds
            // the left blade in place, and the settle is not pushed on.
            GameData* handsData = hands != NULL ? hands->getGameData() : NULL;
            bool sameKind = e.seenHands && hands != NULL && e.lastHands != NULL && handsData == e.lastHandsData;
            logDraw(c, e, hands, sameKind);
            e.seenHands = true;
            e.lastHands = hands;
            e.lastHandsData = handsData;
            if (sameKind)
                continue;
            if (hands == NULL)
            {
                // A sheathe is acted on at this tick, not after a settle (the left blade would stay
                // in the hand).
                e.settleAt = now;
                diag::count("visual_draw_sheathe");
                continue;
            }
            e.settleAt = now + SETTLE_MS;
            e.refresh = true;
            e.refreshLateAt = now + REFRESH_LATE_MS;
            // the dual draw's hold: a fresh draw (no left blade attached, no sheathe hold being cut short)
            e.drawHide = g_drawHold && e.off == NULL && !e.sheathe && !e.drawHold;
            diag::count("visual_draw_noted");
        }
        lefty::takeNoted(noteLefty);
        for (std::map<Character*, LeftyEntry>::iterator it = g_lefties.begin(); it != g_lefties.end();)
        {
            Character* c = it->second.who.getCharacter();
            if (c == NULL || c != it->first)
            {
                arms::untrack(it->first);
                g_lefties.erase(it++);
                continue;
            }
            if (!g_pose || (g_entries.find(c) != g_entries.end() && g_entries[c].off != NULL))
            {
                ++it;                       // no pose engine, or the dual visuals still hold the left hand
                continue;
            }
            if (leftyUpdate(c, it->second))
                ++it;
            else
                g_lefties.erase(it++);
        }
        for (std::map<Character*, Entry>::iterator it = g_entries.begin(); it != g_entries.end();)
        {
            Entry& e = it->second;
            Character* c = e.who.getCharacter();
            if (c == NULL || c != it->first)
            {
                arms::untrack(it->first);   // gone: its appearance went with it
                setReady(it->first, false);
                g_entries.erase(it++);
                continue;
            }
            update(c, e, now);
            bool idle = e.off == NULL && reached(now, e.settleAt)
                        && (e.restoreUntil == 0 || reached(now, e.restoreUntil));
            if (idle && !stance::evaluate(c, NULL).dual)
            {
                arms::release(c, arms::OWNER_DUAL);     // nothing erased while tracked
                g_entries.erase(it++);
            }
            else
                ++it;
        }
    }
}

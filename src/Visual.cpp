#include "kit/input.h"

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
#include "Diagnostics.h"
#include "kit/diag.h"
#include "kit/fcs.h"

namespace
{

    const char* SLOT_PROP = "nitoryu_offhand";
    const char* SLOT_HAND = "nitoryu_offhand_hand";
    const DWORD TICK_MS = 100;

    const DWORD SETTLE_MS = 300;
    const DWORD CHECK_HANDS_MS = 1000;
    const DWORD VERIFY_MS = 1000;

    const DWORD REFRESH_LATE_MS = 1200;
    const DWORD RESTORE_WATCH_MS = 2000;

    struct SlotData
    {
        std::string name;
        std::string bone;
        Ogre::Vector3 pos;
        Ogre::Quaternion rot;
        std::string mesh;
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

    std::string g_bone = "Bip01 L Hand";
    Ogre::Vector3 g_pos(0.01f, 0.0f, -0.275f);
    Ogre::Quaternion g_rot(0.7053f, -0.0007f, 0.0007f, -0.7089f);
    bool g_enabled = true;
    bool g_pose = true;

    volatile bool g_live = true;

    struct Entry
    {
        hand who;
        Weapon* off;
        const char* slot;
        GameData* offData;
        Weapon* lastOff;
        Weapon* lastMain;

        Weapon* hidden[3];
        GameData* hiddenData[3];
        DWORD settleAt;
        DWORD checkHandsAt;
        DWORD restoreUntil;
        DWORD verifyAt;
        bool verifiedOk;
        bool refresh;
        DWORD refreshLateAt;
        Weapon* lastHands;
        GameData* lastHandsData;
        bool seenHands;
        CombatTechniqueData* dashTech;
        float dashProg;
        int dashStall;
        bool dashDone;
        int dashClip;

        bool offHip;
        bool sheathe;
        bool sheatheHip;
        double sheatheStart;
        bool sheatheMain;
        bool sheatheOffHome;
        int sheatheClip;

        double foughtAt;
        hand foe;
    };

    CRITICAL_SECTION g_lock;
    std::set<Character*> g_ready;

    void setReady(Character* c, bool on)
    {
        EnterCriticalSection(&g_lock);
        if (on)
            g_ready.insert(c);
        else
            g_ready.erase(c);
        LeaveCriticalSection(&g_lock);
    }
    std::vector<hand> g_pending;
    std::vector<hand> g_pendingSheathe;
    std::map<Character*, Entry> g_entries;
    DWORD g_lastTick = 0;

    std::string nameOf(RootObjectBase* o) { return o != NULL ? o->displayName : std::string("<null>"); }

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

    bool onBody(Character* c, Weapon* w)
    {
        Inventory* inv = c->getInventory();
        return inv != NULL && (indexInSection(inv, "back", w) >= 0 || indexInSection(inv, "hip", w) >= 0);
    }

    struct EntityView
    {
        int kind;
        GameData* data;
        size_t meshLen;
        unsigned long long itemKey;
    };

    int g_itemOff = -2;

    unsigned long long keyOf(const Item* it)
    {
        if (it == NULL)
            return 0;
        const hand& h = const_cast<Item*>(it)->getHandle();
        return (unsigned long long)h.index | ((unsigned long long)h.serial << 32);
    }
    int g_itemProbeMisses = 0;

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
        unsigned long long want;
        int found;
    };

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
                    diag::count("visual_sheath_none");
                if (diag::takeDetail())
                    kit::Diag("visual_sheath").kv("who", nameOf(c)).kv("slot", slot).kv("item", k == 0 ? "off" : "main")
                        .kv("sheath", sheath).emit();
                break;
            }
        }
    }

    int restoreSheaths(Character* c, AppearanceBase* app, Entry& e, Weapon* only = NULL)
    {
        Weapon* hands = c->getCurrentWeapon();
        int fixed = 0;
        Weapon* put[3] = { NULL, NULL, NULL };
        for (int i = 0; i < 3; ++i)
        {
            Weapon* w = e.hidden[i];
            if (w == NULL || (only != NULL && w != only))
                continue;
            std::string slot(BODY_SLOTS[i]);
            if (w == hands)
            {
                e.hidden[i] = NULL;
                diag::count("visual_restore_in_hands");
                continue;
            }
            bool carried = onBody(c, w);
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
                    continue;
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
                break;
            }
        }
        return fixed;
    }

    bool attach(Character* c, AppearanceBase* app, Weapon* main, Weapon* off, Entry& e)
    {

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
            diag::count("visual_no_bare_mesh");
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
        e.refresh = false;
        setReady(c, true);
        e.slot = slot;
        e.offData = off->getGameData();
        e.lastOff = off;
        e.lastMain = main;
        e.restoreUntil = 0;
        probeItemOffset(c, app, slot, off);

        fixSheaths(c, app, main, off, e);
        return true;
    }

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

    void sheatheDropMain(Character* c, AppearanceBase* app, Entry& e);

    void detach(Character* c, AppearanceBase* app, Entry& e, const char* why)
    {
        if (e.off != NULL)
        {
            e.off = NULL;
            setReady(c, false);
            if (app != NULL)
            {
                sheatheDropMain(c, app, e);

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
        arms::release(c, arms::OWNER_DUAL);
    }

    bool g_sheatheMain = true;

    bool upright(Character* c)
    {
        return !c->isDown() && !c->isUnconcious() && !c->isRagdoll() && !c->isBeingCarried();
    }

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

        if (hands != NULL && !(onBody(c, e.lastMain)
                               && slotState(app, std::string("hands"), e.lastMain, NULL) == SHOWS_OVERRIDE))
            return;
        app->detachItem(std::string("hands"));
        if (hands != NULL)
        {
            std::string bare = kit::fcsFile(hands->data, "bare sword", "");
            if (!bare.empty())
                app->attachItem(hands, bare, std::string("hands"));
            diag::count("sheathe_main_replaced");
        }
    }

    const double CHIBURI_WINDOW_MS = 8000.0;

    bool chiburiNow(Character* c, Entry& e, double t)
    {
        if (!arms::clipOn(e.offHip ? arms::CLIP_CHIBURI_HIP : arms::CLIP_CHIBURI))
            return false;
        const char* why = NULL;
        Character* foe = e.foughtAt > 0.0 ? e.foe.getCharacter() : NULL;
        double gap = t - e.foughtAt;
        if (e.foughtAt <= 0.0)
            why = "chiburi_skipped_no_fight";
        else if (gap < 0.0 || gap > CHIBURI_WINDOW_MS)
            why = "chiburi_skipped_late";
        else if (foe == NULL)
            why = "chiburi_skipped_foe_gone";
        else if (!foe->isDead() && !foe->isDown() && !foe->isUnconcious())
            why = "chiburi_skipped_foe_up";
        if (diag::takeDetail())
            kit::Diag("chiburi").kv("who", nameOf(c)).kv("foe", nameOf(foe)).kv("gap_ms", gap)
                .kv("plays", why == NULL ? "yes" : why).emit();
        e.foughtAt = 0.0;
        if (why != NULL)
        {
            diag::count(why);
            return false;
        }
        return true;
    }

    bool sheatheBegin(Character* c, AppearanceBase* app, Entry& e)
    {
        if (e.off == NULL || e.sheathe || !arms::clipOn(arms::CLIP_SHEATHE))
            return false;
        const char* why = NULL;
        if (e.slot != SLOT_PROP)
            why = "sheathe_hold_skipped_not_posed";
        else if (c->getCurrentWeapon() != NULL)
            why = "sheathe_hold_skipped_weapon_in_hands";
        else if (e.lastMain == NULL || !onBody(c, e.off) || !onBody(c, e.lastMain))
            why = "sheathe_hold_skipped_pair_not_on_body";
        else if (!upright(c))
            why = "sheathe_hold_skipped_down";
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
            diag::count("sheathe_hold_skipped_clip_busy");
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
        setReady(c, false);
        diag::count("sheathe_hold_start");
        if (e.sheatheHip)
            diag::count("sheathe_hold_hip");

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
            end = age >= d.lengthS - 0.15f ? "sheathe_hold_end" : "sheathe_hold_cut_clip";
        else if (age > d.lengthS + 0.4f || age < -0.5f)
            end = "sheathe_hold_timeout";
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

        Weapon* main = e.sheatheMain && onBody(c, e.lastMain) ? e.lastMain : NULL;
        Weapon* off = !e.sheatheOffHome && onBody(c, e.off) ? e.off : NULL;
        if (main != NULL || off != NULL)
            fixSheaths(c, app, main, off, e);
        return true;
    }

    void sheatheCut(Character* c, AppearanceBase* app, Entry& e)
    {
        arms::stopClip(c, (arms::Clip)e.sheatheClip);
        sheatheDropMain(c, app, e);
        e.sheathe = false;
        if (e.sheatheOffHome)
            e.off = NULL;
        else
            setReady(c, true);
        e.sheatheOffHome = false;
        diag::count("sheathe_hold_cut_redraw_dual");
    }

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
        if (app == NULL || !reached(now, e.settleAt))
            return;
        stance::Info s = stance::evaluate(c, NULL);
        if (!s.dual)
        {

            if (e.sheathe ? sheatheHold(c, app, e) : sheatheBegin(c, app, e))
                return;
            detach(c, app, e, "visual_detached_not_dual");

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
                rebind(c, app, s.main, s.off, e);
            else
                detach(c, app, e, "visual_detached_pair_changed");
            if (e.off != NULL)
                return;
        }
        e.lastMain = s.main;
        e.offHip = s.offFromHip;
        if (e.off == NULL)
        {
            if (attach(c, app, s.main, s.off, e))
            {
                e.checkHandsAt = now + CHECK_HANDS_MS;

                arms::playClip(c, s.offFromHip ? arms::CLIP_DRAW_HIP : arms::CLIP_DRAW, defense::clockMs());
            }
            return;
        }

        bool wrong = false;

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

    struct LeftyEntry
    {
        hand who;
        Weapon* w;
        GameData* data;
    };
    std::map<Character*, LeftyEntry> g_lefties;

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
            arms::release(c, arms::OWNER_LEFTY);
            return;
        }
        if (app != NULL)
        {
            app->detachItem(std::string(SLOT_PROP));
            Weapon* hands = c->getCurrentWeapon();

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
            leftyEnd(c, app, e);
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

        if (app->getAttachedEntity("hands") != NULL)
        {
            app->detachItem(std::string("hands"));
            diag::count("lefty_hands_hidden");
        }
        return true;
    }

    void updateModes()
    {
        for (std::map<Character*, Entry>::iterator it = g_entries.begin(); it != g_entries.end(); ++it)
        {
            if (it->second.off == NULL)
                continue;
            Character* c = it->second.who.getCharacter();
            if (c == NULL || c != it->first)
            {

                arms::untrack(it->first);
                setReady(it->first, false);
                it->second.off = NULL;
                diag::count("arms_released_dead");
                continue;
            }

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
            combat::enforceSpeed(c);
            ambidex::noteRank(c);
            CombatClass* cc = c->getCombatClass();
            arms::Mode m = arms::MIRROR;
            if (cc != NULL && cc->combatState == CHOP_WEAPON && cc->currentTechnique != NULL)
            {
                CombatTechniqueData* t = cc->currentTechnique;

                int own = combat::armsModeOf(t);
                if (own >= 0)
                    m = (arms::Mode)own;
                else if (combat::twinsForeignDash(c, t))
                {
                    m = arms::TWIN;
                    if (diag::stage())
                        diag::count("foreign_dash_twin_frame");
                }
                else
                    m = arms::HOLD;
            }
            else if (cc != NULL && (cc->combatState == BLOCK || cc->combatState == REACTION_BLOCK))
                m = arms::BLOCK;

            Entry& ent = it->second;

            int curClip = cc != NULL ? combat::tailClip(cc->currentTechnique) : -1;
            CombatTechniqueData* cur = curClip >= 0 ? cc->currentTechnique : NULL;
            float curProg = cur != NULL && cc->animation != NULL ? cc->animation->getAnimationProgress(cur) : -1.0f;
            bool dashNow = cur != NULL && curProg > 0.001f && curProg < 0.999f;
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
                ent.dashDone = false;
            if (dashNow && ent.dashDone)
                dashNow = false;
            if (dashNow && cur != ent.dashTech)
            {
                ent.dashTech = cur;
                ent.dashProg = 0.0f;
                ent.dashStall = 0;
                ent.dashClip = curClip;
            }

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
                    ent.dashProg = p;
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
                diag::count("attack_tail_cut");
                ent.dashTech = NULL;
                ent.dashDone = true;
            }

            arms::FrameInput fin;
            fin.mode = m;
            fin.inCombat = cc != NULL && cc->_isInCombatMode();

            if (fin.inCombat)
            {
                hand foe = cc->_getAttackTarget();
                if (foe.getCharacter() != NULL)
                {
                    ent.foughtAt = defense::clockMs();
                    ent.foe = foe;
                }
            }

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

            bool stage = diag::stage();
            if (stage && cc != NULL && cc->_isInCombatMode())
            {
                Character* t = cc->_getAttackTarget().getCharacter();
                CombatClass* tc = t != NULL ? t->getCombatClass() : NULL;
                if (tc != NULL)
                {
                    diag::count(tc->combatState == STUMBLE ? "target_stumble_frame" : "target_steady_frame");

                    char key[40];
                    sprintf_s(key, "target_state_%d", (int)tc->combatState);
                    diag::count(key);
                }

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

            CharMovement* mv = c->getMovement();
            fin.running = mv != NULL && mv->isRunning();

            fin.moving = c->getMovementSpeed() > 1.0f;

            fin.rank = fin.inCombat ? stance::evaluate(c, NULL).tier : 0;

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
        }
        arms::emitClipLog();
    }

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

    void shutdownAll()
    {
        for (std::map<Character*, Entry>::iterator it = g_entries.begin(); it != g_entries.end(); ++it)
        {
            Character* c = it->second.who.getCharacter();
            if (c != NULL && c == it->first)
                detach(c, c->getAppearance(), it->second, "visual_detached_disabled");
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

    bool animSwitch(const std::string& path, const char* key, const char* legacy)
    {
        int v = GetPrivateProfileIntA("Animations", key, -1, path.c_str());
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

        for (int k = 0; k < arms::CLIP_COUNT; ++k)
            arms::setClipOn(moves::CLIPS[k].clip, animSwitch(path, moves::CLIPS[k].iniKey, moves::CLIPS[k].legacyKey));

        char style[16] = { 0 };
        GetPrivateProfileStringA("Animations", "offhand_block_style", "mix", style, sizeof(style), path.c_str());
        arms::setOffBlockStyle(_stricmp(style, "low") == 0 ? 1 : _stricmp(style, "forward") == 0 ? 2 : 0);

        arms::setAngledSwap(GetPrivateProfileIntA("Animations", "angled_block_swap", 0, path.c_str()) != 0);

        g_sheatheMain = GetPrivateProfileIntA("Animations", "dual_sheathe_main_blade", 1, path.c_str()) != 0;

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
                e.foughtAt = 0.0;
                found = g_entries.insert(std::make_pair(c, e)).first;
            }

            Weapon* hands = c->getCurrentWeapon();
            Entry& e = found->second;
            if (e.seenHands && hands == e.lastHands)
            {
                diag::count("visual_draw_same_weapon");
                continue;
            }

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

                e.settleAt = now;
                diag::count("visual_draw_sheathe");
                continue;
            }
            e.settleAt = now + SETTLE_MS;
            e.refresh = true;
            e.refreshLateAt = now + REFRESH_LATE_MS;
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
                ++it;
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
                arms::untrack(it->first);
                setReady(it->first, false);
                g_entries.erase(it++);
                continue;
            }
            update(c, e, now);
            bool idle = e.off == NULL && reached(now, e.settleAt)
                        && (e.restoreUntil == 0 || reached(now, e.restoreUntil));
            if (idle && !stance::evaluate(c, NULL).dual)
            {
                arms::release(c, arms::OWNER_DUAL);
                g_entries.erase(it++);
            }
            else
                ++it;
        }
    }
}

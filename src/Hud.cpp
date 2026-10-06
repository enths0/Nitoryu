#include "kit/input.h"

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Kenshi.h>
#include <kenshi/Globals.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Character.h>
#include <kenshi/KingOfRenderThread.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Renderer.h>
#include <kenshi/gui/ForgottenGUI.h>
#include <kenshi/gui/MainBarGUI.h>
#include <kenshi/gui/PortraitManager.h>
#include <kenshi/gui/SquadManagementScreen.h>
#include <kenshi/util/hand.h>

#include <mygui/MyGUI.h>
#include <OgreCamera.h>
#include <OgreSceneManager.h>
#include <OgreMatrix4.h>
#include <OgreVector3.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Hud.h"
#include "Lang.h"
#include "Mastery.h"
#include "Pressure.h"
#include "Settings.h"
#include "Ui.h"
#include "Diagnostics.h"
#include "kit/diag.h"

namespace
{
    const char* const ROOT_NAME = "Nitoryu_hud";
    const char* const LAYER = "Back";
    const char* const ICON_FLOW = "Nitoryu_mark_flow.png";
    const char* const ICON_MUGEN = "Nitoryu_mark_mugen.png";
    const char* const ICON_PIP = "Nitoryu_mark_pip.png";
    const char* const ICON_PIP_FULL = "Nitoryu_mark_pip_full.png";
    const char* const ICON_BAR = "Nitoryu_mark_bar.png";
    const size_t MAX_IMAGES = 96;
    const int MAX_PIPS = 10;

    const char* const CELL_PREFIX = "Nitoryu_pm_";
    const int CELL_PIPS = 5;
    const int BAR_STEPS = 8;

    enum Place { PORTRAITS = 1, HEADS = 2 };

    enum Kind { FLOW, MUGEN, PRESSURE, KINDS };
    const char* const TIP_NAME = "Nitoryu_hud_tip";
    const char* const TIP_LAYER = "ToolTip";
    const char* const TIP_SKIN = "Kenshi_FloatinglPanelSmallSkin";
    const char* const TIP_KEY[KINDS] = { "tip.flow", "tip.mugen", "tip.pressure" };

    const char* const TIP_FONT = "Kenshi_StandardFont_Small";

    struct Hot
    {
        MyGUI::IntCoord at;
        int kind;
    };

    volatile bool g_live = true;

    int g_scope = 1;
    int g_place = HEADS;
    bool g_flow = true;
    bool g_mugen = true;
    bool g_pressure = true;
    int g_size = 22;
    float g_height = 24.0f;
    bool g_onName = true;

    bool g_cellHook = false;
    bool g_failed = false;

    void failed(const char* where)
    {
        g_failed = true;
        diag::count("hud_failed");
        kit::Diag("hud").kv("error", "exception").kv("where", where).kv("marks", "off for this session").emit();
    }

    bool mine(Character* c) { return c != NULL && c->isPlayerCharacter(); }
    bool selected(Character* c) { return mine(c) && ou != NULL && ou->player != NULL && ou->player->isObjectSelected(c); }

    bool inScope(Character* c) { return g_scope >= 3 || (g_scope == 2 ? mine(c) : selected(c)); }

    struct State
    {
        float left;
        bool chain;
        int stacks;
        bool full;
        hand who;

        int signature() const
        {
            int bar = left > 0.0f ? 1 + (int)(left * BAR_STEPS) : 0;
            return bar | (chain ? 0x100 : 0) | (stacks << 12) | (full ? 0x800 : 0);
        }
    };
    typedef std::map<Character*, State> States;

    State& at(States& m, Character* c)
    {
        States::iterator it = m.find(c);
        if (it == m.end())
        {
            State z;
            z.left = 0.0f;
            z.chain = false;
            z.stacks = 0;
            z.full = false;
            z.who = c->getHandle();
            it = m.insert(std::make_pair(c, z)).first;
        }
        return it->second;
    }

    States g_heads;
    States g_portraits;

    std::vector<Hot> g_hotHeads;
    std::map<Character*, std::vector<Hot> > g_hotCells;
    bool g_tips = true;

    void gather()
    {
        States heads, portraits;
        const bool onHeads = (g_place & HEADS) != 0;
        const bool onPortraits = (g_place & PORTRAITS) != 0 && g_cellHook;
        const int maxStacks = pressure::maxStacks();
        if (g_flow)
        {
            std::vector<flow::Shown> v;
            flow::shown(v);
            for (size_t i = 0; i < v.size(); ++i)
            {
                Character* c = v[i].who.getCharacter();

                if (c == NULL || !flow::active(c))
                    continue;
                float left = v[i].left < 0.02f ? 0.02f : v[i].left;
                if (onPortraits && mine(c))
                    at(portraits, c).left = left;
                if (onHeads && inScope(c))
                    at(heads, c).left = left;
            }
        }
        if (g_mugen)
        {
            std::vector<hand> v;
            mugen::shown(v);
            for (size_t i = 0; i < v.size(); ++i)
            {
                Character* c = v[i].getCharacter();
                if (c == NULL)
                    continue;
                if (onPortraits && mine(c))
                    at(portraits, c).chain = true;
                if (onHeads && inScope(c))
                    at(heads, c).chain = true;
            }
        }
        if (g_pressure)
        {
            std::vector<pressure::Shown> v;
            pressure::shown(v);
            for (size_t i = 0; i < v.size(); ++i)
            {
                Character* target = v[i].target.getCharacter();
                Character* by = v[i].by.getCharacter();
                if (target == NULL || by == NULL)
                    continue;
                if (onPortraits && mine(by))
                {
                    State& s = at(portraits, by);
                    if (v[i].stacks > s.stacks)
                    {
                        s.stacks = v[i].stacks;
                        s.full = v[i].stacks >= maxStacks;
                    }
                }
                if (onHeads && (inScope(target) || inScope(by)))
                {
                    State& s = at(heads, target);
                    s.stacks = v[i].stacks;
                    s.full = v[i].stacks >= maxStacks;
                }
            }
        }

        if (gui != NULL && gui->mainbar != NULL)
        {
            for (States::iterator it = portraits.begin(); it != portraits.end(); ++it)
            {
                States::iterator was = g_portraits.find(it->first);
                if (was == g_portraits.end() || was->second.signature() != it->second.signature())
                    gui->mainbar->updatePortrait(it->second.who);
            }
            for (States::iterator it = g_portraits.begin(); it != g_portraits.end(); ++it)
                if (portraits.find(it->first) == portraits.end())
                    gui->mainbar->updatePortrait(it->second.who);
        }
        g_heads.swap(heads);
        g_portraits.swap(portraits);
    }

    MyGUI::ImageBox* cellImage(MyGUI::Widget* parent, const char* what, bool create)
    {
        std::string name = std::string(CELL_PREFIX) + what;
        for (size_t i = 0; i < parent->getChildCount(); ++i)
            if (parent->getChildAt(i)->getName() == name)
                return parent->getChildAt(i)->castType<MyGUI::ImageBox>(false);
        if (!create)
            return NULL;
        ui::iconLocation();
        MyGUI::ImageBox* box = parent->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, 8, 8), MyGUI::Align::Default, name);
        box->setNeedMouseFocus(false);
        box->setNeedKeyFocus(false);
        diag::count("hud_cell_image_created");
        return box;
    }

    void cellPut(MyGUI::Widget* parent, const char* what, bool show, const char* texture, int x, int y, int w, int h)
    {
        MyGUI::ImageBox* box = cellImage(parent, what, show && w > 0 && h > 0);
        if (box == NULL)
            return;
        if (!show || w <= 0 || h <= 0)
        {
            if (box->getVisible())
                box->setVisible(false);
            return;
        }
        if (box->getUserString("tex") != texture)
        {
            box->setImageTexture(texture);
            box->setUserString("tex", texture);
        }
        box->setCoord(x, y, w, h);
        if (!box->getVisible())
            box->setVisible(true);
    }

    void cellHot(std::vector<Hot>& hot, MyGUI::Widget* parent, bool show, int kind, int x, int y, int w, int h)
    {
        if (!show)
            return;
        Hot ht;
        ht.at = MyGUI::IntCoord(parent->getAbsoluteLeft() + x, parent->getAbsoluteTop() + y, w, h);
        ht.kind = kind;
        hot.push_back(ht);
    }

    void paintCell(PortraitMainCellView* cell, PortraitData* data)
    {
        MyGUI::Widget* parent = cell->mMainWidget;
        if (parent == NULL)
            return;
        State s;
        s.left = 0.0f;
        s.chain = false;
        s.stacks = 0;
        s.full = false;
        if (g_live && g_scope != 0 && (g_place & PORTRAITS) != 0)
        {
            States::iterator it = g_portraits.find(data->getCharacter());
            if (it != g_portraits.end())
                s = it->second;
        }
        const int w = parent->getWidth();
        const int icon = w / 3 < 12 ? 12 : w / 3;
        const int bar = 3, pad = 2;
        cellPut(parent, "flow", s.left > 0.0f, ICON_FLOW, pad, pad, icon, icon);
        cellPut(parent, "bar", s.left > 0.0f, ICON_BAR, pad, pad + icon, (int)(icon * s.left + 0.5f), bar);
        cellPut(parent, "mugen", s.chain, ICON_MUGEN, w - icon - pad, pad, icon, icon);
        const int pip = w / 7 < 6 ? 6 : w / 7;
        const int n = s.stacks > CELL_PIPS ? CELL_PIPS : s.stacks;
        int x = (w - (n * pip + (n - 1))) / 2;
        for (int k = 0; k < CELL_PIPS; ++k)
        {
            char what[8];
            sprintf_s(what, "pip%d", k);
            cellPut(parent, what, k < n, s.full ? ICON_PIP_FULL : ICON_PIP, x + k * (pip + 1), pad + icon + bar + pad, pip, pip);
        }
        std::vector<Hot> hot;
        cellHot(hot, parent, s.left > 0.0f, FLOW, pad, pad, icon, icon + bar);
        cellHot(hot, parent, s.chain, MUGEN, w - icon - pad, pad, icon, icon);
        cellHot(hot, parent, n > 0, PRESSURE, x, pad + icon + bar + pad, n * (pip + 1), pip);
        if (hot.empty())
            g_hotCells.erase(data->getCharacter());
        else
            g_hotCells[data->getCharacter()].swap(hot);
    }

    void (*cellUpdate_orig)(PortraitMainCellView*, const MyGUI::IBDrawItemInfo&, PortraitData*) = NULL;
    void cellUpdate_hook(PortraitMainCellView* self, const MyGUI::IBDrawItemInfo& info, PortraitData* data)
    {
        cellUpdate_orig(self, info, data);
        if (g_failed || self == NULL || data == NULL)
            return;
        try
        {
            paintCell(self, data);
        }
        catch (...)
        {
            failed("portrait");
        }
    }

    struct Image
    {
        MyGUI::ImageBox* box;
        const char* texture;
    };

    MyGUI::Widget* g_root = NULL;
    std::vector<Image> g_images;
    size_t g_used = 0;
    MyGUI::TextBox* g_tip = NULL;
    int g_tipKind = -1;

    bool alive(MyGUI::Widget* w)
    {
        if (w == NULL)
            return false;
        MyGUI::EnumeratorWidgetPtr e = MyGUI::Gui::getInstance().getEnumerator();
        while (e.next())
            if (e.current() == w)
                return true;
        return false;
    }

    bool rootAlive() { return alive(g_root); }

    void headHot(int kind, int x, int y, int w, int h)
    {
        Hot ht;
        ht.at = MyGUI::IntCoord(x, y, w, h);
        ht.kind = kind;
        g_hotHeads.push_back(ht);
    }

    int hotAt(const std::vector<Hot>& hot, const MyGUI::IntPoint& p)
    {
        for (size_t i = 0; i < hot.size(); ++i)
            if (hot[i].at.inside(p))
                return hot[i].kind;
        return -1;
    }

    void hideTip()
    {
        if (g_tip != NULL && g_tip->getVisible())
            g_tip->setVisible(false);
    }

    void tip()
    {
        if (!g_tips || gui == NULL || !gui->visible)
        {
            hideTip();
            return;
        }

        for (std::map<Character*, std::vector<Hot> >::iterator it = g_hotCells.begin(); it != g_hotCells.end();)
        {
            if (g_portraits.find(it->first) == g_portraits.end())
                g_hotCells.erase(it++);
            else
                ++it;
        }
        const MyGUI::IntPoint mouse = MyGUI::InputManager::getInstance().getMousePosition();
        int kind = hotAt(g_hotHeads, mouse);
        for (std::map<Character*, std::vector<Hot> >::iterator it = g_hotCells.begin(); kind < 0 && it != g_hotCells.end(); ++it)
            kind = hotAt(it->second, mouse);
        if (kind < 0)
        {
            hideTip();
            return;
        }
        if (g_tip == NULL)
        {
            g_tip = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>(TIP_SKIN, MyGUI::IntCoord(0, 0, 100, 40), MyGUI::Align::Default,
                                                                           TIP_LAYER, TIP_NAME);
            if (g_tip == NULL)
                return;
            g_tip->setNeedMouseFocus(false);
            g_tip->setNeedKeyFocus(false);
            g_tip->setFontName(TIP_FONT);
            g_tip->setTextColour(MyGUI::Colour(0.85f, 0.80f, 0.68f));
            g_tip->setTextAlign(MyGUI::Align::Center);
            g_tipKind = -1;
            diag::count("hud_tip_created");
        }
        const bool fresh = g_tipKind != kind;
        if (fresh)
        {
            g_tip->setCaption(lang::text(TIP_KEY[kind]));
            g_tipKind = kind;
            diag::count("hud_tip_shown");
        }
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        int w = 0, h = 0;
        ui::tipSize(g_tip, TIP_FONT, fresh, w, h);
        int x = mouse.left + 18, y = mouse.top - h - 6;
        if (x + w > view.width)
            x = view.width - w;
        if (y < 0)
            y = mouse.top + 22;
        g_tip->setCoord(x < 0 ? 0 : x, y, w, h);
        if (!g_tip->getVisible())
            g_tip->setVisible(true);
    }

    bool ensureRoot(const MyGUI::IntSize& view)
    {
        if (g_root == NULL)
        {
            ui::iconLocation();
            g_root = MyGUI::Gui::getInstance().createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, view.width, view.height),
                                                                            MyGUI::Align::Stretch, LAYER, ROOT_NAME);
            if (g_root == NULL)
                return false;
            g_root->setNeedMouseFocus(false);
            g_root->setNeedKeyFocus(false);
            diag::count("hud_root_created");
        }
        if (g_root->getWidth() != view.width || g_root->getHeight() != view.height)
            g_root->setCoord(0, 0, view.width, view.height);
        return true;
    }

    void hideFrom(size_t first)
    {
        for (size_t i = first; i < g_images.size(); ++i)
            if (g_images[i].box->getVisible())
                g_images[i].box->setVisible(false);
    }

    void put(const char* texture, int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return;
        if (g_used == g_images.size())
        {
            if (g_images.size() >= MAX_IMAGES)
            {
                diag::count("hud_images_full");
                return;
            }
            Image im;
            im.box = g_root->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(x, y, w, h), MyGUI::Align::Default);
            im.box->setNeedMouseFocus(false);
            im.box->setNeedKeyFocus(false);
            im.texture = NULL;
            g_images.push_back(im);
        }
        Image& im = g_images[g_used++];
        if (im.texture != texture)
        {
            im.box->setImageTexture(texture);
            im.texture = texture;
        }
        im.box->setCoord(x, y, w, h);
        if (!im.box->getVisible())
            im.box->setVisible(true);
    }

    struct Label
    {
        MyGUI::IntCoord at;
        std::string caption;
    };
    std::vector<Label> g_labels;
    bool g_labelsRead = false;

    void readLabels(MyGUI::EnumeratorWidgetPtr e, int depth)
    {
        while (e.next())
        {
            MyGUI::Widget* w = e.current();
            if (!w->getVisible() || w->getName().compare(0, 8, "Nitoryu_") == 0)
                continue;
            MyGUI::TextBox* t = w->castType<MyGUI::TextBox>(false);
            if (t != NULL)
            {
                const MyGUI::UString& text = t->getCaption();
                if (!text.empty() && text.size() < 64)
                {
                    MyGUI::IntCoord r = t->getTextRegion();
                    if (r.width <= 0 || r.height <= 0)
                        r = MyGUI::IntCoord(0, 0, w->getWidth(), w->getHeight());
                    Label l;
                    l.at = MyGUI::IntCoord(w->getAbsoluteLeft() + r.left, w->getAbsoluteTop() + r.top, r.width, r.height);
                    l.caption = text.asUTF8();
                    g_labels.push_back(l);
                }
            }
            if (depth < 8 && w->getChildCount() > 0)
                readLabels(w->getEnumerator(), depth + 1);
        }
    }

    bool nameLabel(Character* c, const MyGUI::IntSize& view, int x, int y, MyGUI::IntCoord& at)
    {
        if (!g_labelsRead)
        {
            g_labels.clear();
            readLabels(MyGUI::Gui::getInstance().getEnumerator(), 0);
            g_labelsRead = true;
        }
        const std::string name = c->getName();
        if (name.empty())
            return false;
        const int reach = view.height / 4;
        int best = -1, bestD = 0;
        for (size_t i = 0; i < g_labels.size(); ++i)
        {
            const Label& l = g_labels[i];
            if (l.caption.find(name) == std::string::npos)
                continue;
            int dx = l.at.left + l.at.width / 2 - x, dy = l.at.top - y;
            dx = dx < 0 ? -dx : dx;
            dy = dy < 0 ? -dy : dy;
            if (dx > reach || dy > reach)
                continue;
            if (best < 0 || dx + dy < bestD)
            {
                best = (int)i;
                bestD = dx + dy;
            }
        }
        if (best < 0)
            return false;
        at = g_labels[best].at;
        return true;
    }

    bool toScreen(Ogre::Camera* cam, Ogre::SceneManager* scene, const MyGUI::IntSize& view, const Ogre::Vector3& world, int& x, int& y)
    {
        Ogre::Vector3 eye = cam->getViewMatrix() * (world - scene->getRelativeOrigin());
        if (eye.z >= 0.0f)
            return false;
        Ogre::Vector3 ndc = cam->getProjectionMatrix() * eye;
        if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f)
            return false;
        x = (int)((ndc.x + 1.0f) * 0.5f * view.width);
        y = (int)((1.0f - (ndc.y + 1.0f) * 0.5f) * view.height);
        return true;
    }

    void drawHeads()
    {
        g_hotHeads.clear();
        if (g_heads.empty() || au == NULL || au->render == NULL || gui == NULL || !gui->visible)
        {
            hideFrom(0);
            return;
        }
        Ogre::Camera* cam = au->render->getCamera();
        Ogre::SceneManager* scene = au->render->getSceneManager();
        if (cam == NULL || scene == NULL)
        {
            hideFrom(0);
            return;
        }
        MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        if (!ensureRoot(view))
        {
            diag::count("hud_no_root");
            return;
        }
        g_used = 0;
        g_labelsRead = false;
        const int icon = g_size;
        const int pip = icon / 2;
        const int gap = 2;
        for (States::iterator it = g_heads.begin(); it != g_heads.end(); ++it)
        {
            Character* c = it->first;
            const State& h = it->second;
            if (!c->isOnScreen)
                continue;
            Ogre::Vector3 p = c->getPosition();
            p.y += g_height;
            int hx = 0, hy = 0;
            if (!toScreen(cam, scene, view, p, hx, hy))
                continue;
            MyGUI::IntCoord label;
            if (g_onName && nameLabel(c, view, hx, hy, label))
            {
                hx = label.left + label.width / 2;
                hy = label.top - gap;
                diag::count("hud_on_name");
            }
            else
                diag::count("hud_at_height");
            int y = hy;

            int icons = (h.left > 0.0f ? 1 : 0) + (h.chain ? 1 : 0);
            if (icons > 0)
            {
                const int bar = 3;
                y -= icon + gap + bar;
                int x = hx - (icons * icon + (icons - 1) * gap) / 2;
                if (h.left > 0.0f)
                {
                    put(ICON_FLOW, x, y, icon, icon);
                    put(ICON_BAR, x, y + icon + gap, (int)(icon * h.left + 0.5f), bar);
                    headHot(FLOW, x, y, icon, icon + gap + bar);
                    x += icon + gap;
                }
                if (h.chain)
                {
                    put(ICON_MUGEN, x, y, icon, icon);
                    headHot(MUGEN, x, y, icon, icon);
                }
                y -= gap;
            }

            if (h.stacks > 0)
            {
                int n = h.stacks > MAX_PIPS ? MAX_PIPS : h.stacks;
                y -= pip;
                int x = hx - (n * pip + (n - 1) * gap) / 2;
                headHot(PRESSURE, x, y, n * (pip + gap), pip);
                for (int k = 0; k < n; ++k, x += pip + gap)
                    put(h.full ? ICON_PIP_FULL : ICON_PIP, x, y, pip, pip);
            }
        }
        hideFrom(g_used);
    }
}

namespace hud
{
    void init() { applySettings(); }

    void applySettings()
    {
        int s = cfg::i(cfg::HUD_MARKS);
        g_scope = s < 0 ? 0 : (s > 3 ? 3 : s);
        int p = cfg::i(cfg::HUD_PLACE);
        g_place = p < 1 || p > 3 ? HEADS : p;
        g_flow = cfg::b(cfg::HUD_FLOW);
        g_mugen = cfg::b(cfg::HUD_MUGEN);
        g_pressure = cfg::b(cfg::HUD_PRESSURE);
        int size = cfg::i(cfg::HUD_SIZE);
        g_size = size < 8 ? 8 : (size > 64 ? 64 : size);
        g_height = cfg::f(cfg::HUD_HEIGHT);
        g_onName = cfg::b(cfg::HUD_ON_NAME);
        g_tips = cfg::b(cfg::HUD_TIPS);
    }

    bool installHooks()
    {
        g_cellHook = KenshiLib::AddHook(KenshiLib::GetRealAddress(&PortraitMainCellView::update), cellUpdate_hook, &cellUpdate_orig)
                     == KenshiLib::SUCCESS;
        if (!g_cellHook)
            kit::log::error("Nitoryu: could not hook PortraitMainCellView::update (no marks on the portraits; [Marks] place=2 shows them above the heads)");
        return g_cellHook;
    }

    void setEnabled(bool on) { g_live = on; }

    void tick()
    {
        if (g_failed)
            return;

        try
        {
            if (g_root != NULL && !rootAlive())
            {
                g_root = NULL;
                g_images.clear();
            }
            if (g_tip != NULL && !alive(g_tip))
                g_tip = NULL;
            if (!g_live || g_scope == 0)
            {
                States none;
                if (!g_portraits.empty() && gui != NULL && gui->mainbar != NULL)
                    for (States::iterator it = g_portraits.begin(); it != g_portraits.end(); ++it)
                        gui->mainbar->updatePortrait(it->second.who);
                g_portraits.swap(none);
                g_heads.clear();
                g_hotHeads.clear();
                g_hotCells.clear();
                hideFrom(0);
                hideTip();
                return;
            }
            gather();
            drawHeads();
            tip();
        }
        catch (...)
        {
            failed("tick");
        }
    }
}

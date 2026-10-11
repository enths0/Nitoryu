// The stance toggle as a button in the inventory window.
//
// A small icon sits in the top-right corner of the "back" weapon section of a
// player character who carries a pair: two crossed blades = the stance is on,
// one grey blade = off. A click toggles it, the character says the line, and
// the state is saved with the character (Ambidex.cpp, NIT_dual_off).
// A second button to its left shows when the two carried weapons pair only with the character's
// any-weapon switch (blunt, hackers): a blade crossed with a club, grey = off. Saved the same way
// (NIT_any_weapon). The mouse over a button shows what it does ([UI] button_tips).
// How: an InventoryGUI::refreshSection post-hook; the button is owned by the section widget and never
// destroyed by us (the parent frees it; a second destroy is a double free); the icons are found once
// the mod folder is an Ogre location in every resource group.
// Threads:
//   refreshSection_hook - main loop (the inventory window). Every widget call stays on the main loop.
//   No lock: the stance state it shows is read through stance::, which locks.

#include "kit/input.h"   // Windows.h with NOMINMAX first

#include "kit/log.h"
#include <core/Functions.h>

#include <kenshi/Character.h>
#include <kenshi/Inventory.h>
#include <kenshi/gui/InventoryGUI.h>

#include <mygui/MyGUI.h>
#include <OgreResourceGroupManager.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "Ui.h"
#include "Stance.h"
#include "Feedback.h"
#include "Lang.h"
#include "Diagnostics.h"
#include "kit/diag.h"
#include "Hooks.h"

namespace
{
    const size_t OFF_INVGUI_SECTIONS = 0x60;   // InventoryGUI::inventorySections (KenshiLib 0.5.0)
    typedef std::map<std::string, InventorySectionGUI*> SectionGuiMap;

    const char* const BUTTON_PREFIX = "Nitoryu_toggle_";
    const char* const ICON_ON = "Nitoryu_two_blades.png";
    const char* const ICON_OFF = "Nitoryu_one_blade.png";
    const char* const ANY_PREFIX = "Nitoryu_any_";
    const char* const ICON_ANY_ON = "Nitoryu_any_weapon.png";
    const char* const ICON_ANY_OFF = "Nitoryu_any_weapon_off.png";
    const int BUTTON_DEPTH = -1;    // MyGUI: among siblings the smaller depth is drawn on top (the default is 0)

    bool g_enabled = true;          // [UI] toggle_button
    bool g_anyEnabled = true;       // [UI] any_weapon_button
    volatile bool g_live = true;    // false when Nitoryu is turned off at run time
    int g_size = 22;                // [UI] button_size
    std::string g_skin = "Kenshi_Button1";
    std::string g_dir;
    bool g_locationAdded = false;

    std::map<MyGUI::Widget*, InventoryGUI*> g_buttons;

    // The hover text: a widget of our own on the tooltip layer, as the marks' (Hud.cpp).
    const char* const TIP_NAME = "Nitoryu_ui_tip";
    const char* const TIP_LAYER = "ToolTip";
    const char* const TIP_SKIN = "Kenshi_FloatinglPanelSmallSkin";  // (sic) data/gui/skins/kenshi_skins.xml
    const char* const TIP_FONT = "Kenshi_StandardFont_Small";       // the skin sets no font
    const char* const TIP_STANCE = "tip.stance_button";
    const char* const TIP_ANY = "tip.any_weapon_button";
    const int TIP_PAD_X = 25, TIP_PAD_Y = 16;

    bool g_tips = true;             // [UI] button_tips
    bool g_tipFailed = false;
    MyGUI::TextBox* g_tip = NULL;
    const char* g_tipKey = NULL;

    std::string modDir()
    {
        HMODULE self = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&modDir), &self);
        char path[MAX_PATH] = { 0 };
        GetModuleFileNameA(self, path, MAX_PATH);
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        return slash == std::string::npos ? std::string(".\\") : p.substr(0, slash + 1);
    }

    void addIconLocation()
    {
        if (g_locationAdded)
            return;
        g_locationAdded = true;
        // MyGUI searches its own resource group, whose name Kenshi does not publish, and KenshiLib's
        // headers do not declare MyGUI::OgreDataManager to ask for it: so the folder is added to every
        // group. Guarded: an Ogre exception costs the icons, not the game.
        try
        {
            Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
            Ogre::StringVector groups = rgm.getResourceGroups();
            for (size_t i = 0; i < groups.size(); ++i)
                if (!rgm.resourceLocationExists(g_dir, groups[i]))
                    rgm.addResourceLocation(g_dir, "FileSystem", groups[i]);
            kit::Diag("ui_icons").kv("location", g_dir).kv("found", rgm.resourceExistsInAnyGroup(ICON_ON)).emit();
        }
        catch (...)
        {
            diag::count("ui_icons_failed");
            kit::Diag("ui_icons").kv("location", g_dir).kv("found", false).kv("error", "exception").emit();
        }
    }

    void paint(MyGUI::Widget* button, const char* texture)
    {
        for (size_t i = 0; i < button->getChildCount(); ++i)
        {
            MyGUI::ImageBox* icon = button->getChildAt(i)->castType<MyGUI::ImageBox>(false);
            if (icon != NULL)
            {
                icon->setImageTexture(texture);
                return;
            }
        }
    }

    Character* clicked(MyGUI::Widget* sender)
    {
        std::map<MyGUI::Widget*, InventoryGUI*>::iterator t = g_buttons.find(sender);
        Character* c = t != g_buttons.end() ? t->second->getCallbackCharacter() : NULL;
        if (c == NULL || !c->isPlayerCharacter())
        {
            diag::count("ui_click_no_character");
            return NULL;
        }
        return c;
    }

    void ensureButton(InventoryGUI* gui, InventorySection* section);

    // MyGUI throws (a widget the game freed, a bad cast), and an exception out of a click delegate or the
    // refreshSection hook would end the game. The first one turns the buttons off for the session and is
    // logged once; the game's own inventory goes on.
    bool g_uiFailed = false;

    void uiFault(const char* where, const char* what)
    {
        diag::count("ui_exception");
        if (g_uiFailed)
            return;
        g_uiFailed = true;
        feedback::featureOff("ui", (std::string(where) + ": " + (what != NULL ? what : "?")).c_str());
    }

    void onAnyClickBody(MyGUI::Widget* sender)
    {
        std::map<MyGUI::Widget*, InventoryGUI*>::iterator t = g_buttons.find(sender);
        Character* c = clicked(sender);
        if (c == NULL)
            return;
        bool on = stance::toggleAnyWeapon(c);
        paint(sender, on ? ICON_ANY_ON : ICON_ANY_OFF);
        feedback::say(c, lang::text(on ? "speech.any_weapon" : "speech.blades_only").c_str(), 0);
        diag::count(on ? "ui_any_weapon_on" : "ui_any_weapon_off");
        kit::Diag("any_weapon").kv("who", c->displayName).kv("on", on).kv("pair", stance::hasPair(c)).emit();
        // The stance button follows at once: it shows only for a pair, and the pair just changed.
        Inventory* inv = c->getInventory();
        InventorySection* back = inv != NULL ? inv->getSection(std::string("back")) : NULL;
        if (back != NULL)
            ensureButton(t->second, back);
    }

    void onClickBody(MyGUI::Widget* sender)
    {
        Character* c = clicked(sender);
        if (c == NULL)
            return;
        bool on = stance::togglePlayer(c);
        paint(sender, on ? ICON_ON : ICON_OFF);
        // Said from the main loop (feedback::flush), like every other line.
        feedback::say(c, lang::text(on ? "speech.two_blades" : "speech.one_blade").c_str(), 0);
        diag::count(on ? "ui_toggle_on" : "ui_toggle_off");
        kit::Diag("toggle").kv("who", c->displayName).kv("dual_allowed", on).kv("from", "button").emit();
    }

    void onClick(MyGUI::Widget* sender)
    {
        if (g_uiFailed)
            return;
        try { onClickBody(sender); }
        catch (const std::exception& e) { uiFault("stance button click", e.what()); }
        catch (...) { uiFault("stance button click", "unknown exception"); }
    }

    void onAnyClick(MyGUI::Widget* sender)
    {
        if (g_uiFailed)
            return;
        try { onAnyClickBody(sender); }
        catch (const std::exception& e) { uiFault("any-weapon button click", e.what()); }
        catch (...) { uiFault("any-weapon button click", "unknown exception"); }
    }

    // One button of the section: found by name or created, `slot` buttons in from the right edge.
    void ensureOne(InventoryGUI* gui, MyGUI::Widget* parent, const char* prefix, int slot, bool show, const char* texture,
                   void (*click)(MyGUI::Widget*))
    {
        char name[96];
        sprintf_s(name, "%s%p", prefix, static_cast<void*>(gui));
        MyGUI::Widget* button = NULL;
        for (size_t i = 0; i < parent->getChildCount(); ++i)
            if (parent->getChildAt(i)->getName() == name)
            {
                button = parent->getChildAt(i);
                break;
            }
        if (button == NULL)
        {
            if (!show)
                return;
            addIconLocation();
            // Owned by `parent`: never destroyed by us.
            button = parent->createWidget<MyGUI::Button>(g_skin, MyGUI::IntCoord(parent->getWidth() - (slot + 1) * g_size - slot * 2, 0, g_size, g_size),
                                                         MyGUI::Align::Right | MyGUI::Align::Top, name);
            button->eventMouseButtonClick += MyGUI::newDelegate(click);
            button->setNeedKeyFocus(false);
            MyGUI::ImageBox* icon = button->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(2, 2, g_size - 4, g_size - 4),
                                                                          MyGUI::Align::Stretch, std::string(name) + "_icon");
            icon->setNeedMouseFocus(false);
            diag::count("ui_button_created");
        }
        // When the window opens again the game adds the weapon's picture anew, as a later sibling, and
        // draws it over our button. A smaller depth keeps ours above every later sibling.
        if (button->getDepth() != BUTTON_DEPTH)
        {
            button->setDepth(BUTTON_DEPTH);
            diag::count("ui_button_raised");
        }
        g_buttons[button] = gui;
        button->setVisible(show);
        paint(button, texture);
    }

    void ensureButton(InventoryGUI* gui, InventorySection* section)
    {
        if (section->name != "back")
            return;
        Character* c = gui->getCallbackCharacter();
        if (c == NULL || !c->isPlayerCharacter())
            return;
        SectionGuiMap& guis = *reinterpret_cast<SectionGuiMap*>(reinterpret_cast<char*>(gui) + OFF_INVGUI_SECTIONS);
        SectionGuiMap::iterator it = guis.find(section->name);
        if (it == guis.end() || it->second == NULL || it->second->getWidget() == NULL)
        {
            diag::count("ui_no_section_widget");
            return;
        }
        MyGUI::Widget* parent = it->second->getWidget();
        bool on = stance::playerAllowed(c);
        bool show = g_live && (stance::hasPair(c) || !on);   // an "off" character can always turn it back on
        ensureOne(gui, parent, BUTTON_PREFIX, 0, show, on ? ICON_ON : ICON_OFF, &onClick);
        if (!g_anyEnabled)
            return;
        bool any = stance::anyWeapon(c);
        // An "on" character can always turn it back off, whatever it carries now.
        ensureOne(gui, parent, ANY_PREFIX, 1, g_live && (any || stance::needsAnyWeapon(c)), any ? ICON_ANY_ON : ICON_ANY_OFF,
                  &onAnyClick);
    }

    void (*refreshSection_orig)(InventoryGUI*, InventorySection*) = NULL;
    void refreshSection_hook(InventoryGUI* self, InventorySection* section)
    {
        diag::HookCall hc(diag::HK_REFRESH_SECTION);
        refreshSection_orig(self, section);
        if (g_enabled && !g_uiFailed && self != NULL && section != NULL)
        {
            hc.ours();
            try { ensureButton(self, section); }        // after the original, so the game's window is whole
            catch (const std::exception& e) { uiFault("inventory button", e.what()); }
            catch (...) { uiFault("inventory button", "unknown exception"); }
        }
    }

    // The game destroys every widget when it goes back to the title screen; ours go with them.
    bool tipAlive()
    {
        MyGUI::EnumeratorWidgetPtr e = MyGUI::Gui::getInstance().getEnumerator();
        while (e.next())
            if (e.current() == g_tip)
            {
                // a new widget at a freed address is not ours
                if (g_tip->getName() == TIP_NAME)
                    return true;
                diag::count("ui_widget_address_reused");
                return false;
            }
        return false;
    }

    // Which of the two buttons the mouse is over. The widget under the mouse is alive, so it is asked by
    // its name; g_buttons keeps buttons the game may have destroyed and is not read here.
    const char* hovered()
    {
        MyGUI::Widget* w = MyGUI::InputManager::getInstance().getMouseFocusWidget();
        if (w == NULL)
            return NULL;
        const std::string& n = w->getName();
        if (n.compare(0, strlen(BUTTON_PREFIX), BUTTON_PREFIX) == 0)
            return TIP_STANCE;
        if (n.compare(0, strlen(ANY_PREFIX), ANY_PREFIX) == 0)
            return TIP_ANY;
        return NULL;
    }

    void showTip(const char* key)
    {
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
            g_tipKey = NULL;
            diag::count("ui_tip_created");
        }
        const bool fresh = g_tipKey != key;
        if (fresh)
        {
            g_tip->setCaption(lang::text(key));
            g_tipKey = key;
            diag::count("ui_tip_shown");
        }
        const MyGUI::IntPoint mouse = MyGUI::InputManager::getInstance().getMousePosition();
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

    // refreshSection is protected; only a derived class may take its address.
    struct InventoryGUIAccess : public InventoryGUI
    {
        static bool hook()
        {
            return hooks::add("InventoryGUI::refreshSection", "ui", KenshiLib::GetRealAddress(&InventoryGUIAccess::_NV_refreshSection),
                                      refreshSection_hook, &refreshSection_orig) == KenshiLib::SUCCESS;
        }
    };
}

namespace ui
{
    void init()
    {
        g_dir = modDir();
        std::string ini = stance::iniFile();
        g_enabled = GetPrivateProfileIntA("UI", "toggle_button", 1, ini.c_str()) != 0;
        g_anyEnabled = GetPrivateProfileIntA("UI", "any_weapon_button", 1, ini.c_str()) != 0;
        g_size = GetPrivateProfileIntA("UI", "button_size", g_size, ini.c_str());
        if (g_size < 12 || g_size > 64)
            g_size = 22;
        g_tips = GetPrivateProfileIntA("UI", "button_tips", 1, ini.c_str()) != 0;
    }

    void tick()
    {
        if (!g_enabled || !g_tips || g_tipFailed)
            return;
        // MyGUI throws; an exception costs the hover text, not the game.
        try
        {
            if (g_tip != NULL && !tipAlive())
                g_tip = NULL;
            const char* key = g_live ? hovered() : NULL;
            if (key != NULL)
                showTip(key);
            else if (g_tip != NULL && g_tip->getVisible())
                g_tip->setVisible(false);
        }
        catch (...)
        {
            g_tipFailed = true;
            diag::count("ui_tip_failed");
        }
    }

    // getTextSize comes out smaller than the text the game draws, about 5% in width and 22% in line height
    // (the game scales its fonts, `font size2` in settings.cfg). So the text is given room to spare
    // (centred, the spare is an even margin), and the panel then grows until the skin's own text region
    // holds it. The tip_size line carries the numbers.
    void tipSize(MyGUI::TextBox* tip, const char* font, bool fresh, int& w, int& h)
    {
        const MyGUI::IntSize text = tip->getTextSize();
        const std::string caption = tip->getCaption().asUTF8();
        int lines = 1;
        for (size_t i = 0; i < caption.size(); ++i)
            if (caption[i] == '\n')
                ++lines;
        MyGUI::IFont* f = MyGUI::FontManager::getInstance().getByName(font);
        const int fontHeight = tip->getFontHeight(), defaultHeight = f != NULL ? f->getDefaultHeight() : 0;
        int line = (text.height + lines - 1) / lines;
        if (line < fontHeight)
            line = fontHeight;
        if (line < defaultHeight)
            line = defaultHeight;
        const int needW = text.width * 108 / 100 + 8, needH = lines * line * 130 / 100;
        w = needW + 2 * TIP_PAD_X;
        h = needH + 2 * TIP_PAD_Y;
        tip->setSize(w, h);
        const MyGUI::IntCoord region = tip->getTextRegion();
        if (region.width < needW)
            w += needW - region.width;
        if (region.height < needH)
            h += needH - region.height;
        if (fresh && diag::stage())
            kit::Diag("tip_size").kv("font", font).kv("lines", lines).kv("text_w", text.width).kv("text_h", text.height)
                .kv("font_height", fontHeight).kv("default_height", defaultHeight).kv("region_w", region.width)
                .kv("region_h", region.height).kv("w", w).kv("h", h).emit();
    }

    bool iniOn() { return g_enabled; }
    void iconLocation() { addIconLocation(); }
    void setEnabled(bool on) { g_live = on; }

    bool installHooks()
    {
        if (!g_enabled)
            return true;
        if (!stance::knownLayout())
            return false;       // InventoryGUI +0x60 is a 1.0.65 layout; logged by the features line
        if (!InventoryGUIAccess::hook())
        {
            kit::log::error("Nitoryu: could not hook InventoryGUI::refreshSection (no toggle button; Alt+Shift+N still works)");
            return false;
        }
        return true;
    }
}

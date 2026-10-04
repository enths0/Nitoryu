#include "kit/input.h"

#include <Debug.h>
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

namespace
{
    const size_t OFF_INVGUI_SECTIONS = 0x60;
    typedef std::map<std::string, InventorySectionGUI*> SectionGuiMap;

    const char* const BUTTON_PREFIX = "Nitoryu_toggle_";
    const char* const ICON_ON = "Nitoryu_two_blades.png";
    const char* const ICON_OFF = "Nitoryu_one_blade.png";
    const char* const ANY_PREFIX = "Nitoryu_any_";
    const char* const ICON_ANY_ON = "Nitoryu_any_weapon.png";
    const char* const ICON_ANY_OFF = "Nitoryu_any_weapon_off.png";

    bool g_enabled = true;
    bool g_anyEnabled = true;
    volatile bool g_live = true;
    int g_size = 22;
    std::string g_skin = "Kenshi_Button1";
    std::string g_dir;
    bool g_locationAdded = false;

    std::map<MyGUI::Widget*, InventoryGUI*> g_buttons;

    const char* const TIP_NAME = "Nitoryu_ui_tip";
    const char* const TIP_LAYER = "ToolTip";
    const char* const TIP_SKIN = "Kenshi_FloatinglPanelSmallSkin";
    const char* const TIP_FONT = "Kenshi_StandardFont_Small";
    const char* const TIP_STANCE = "tip.stance_button";
    const char* const TIP_ANY = "tip.any_weapon_button";
    const int TIP_PAD_X = 25, TIP_PAD_Y = 16;

    bool g_tips = true;
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

    void onAnyClick(MyGUI::Widget* sender)
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

        Inventory* inv = c->getInventory();
        InventorySection* back = inv != NULL ? inv->getSection(std::string("back")) : NULL;
        if (back != NULL)
            ensureButton(t->second, back);
    }

    void onClick(MyGUI::Widget* sender)
    {
        Character* c = clicked(sender);
        if (c == NULL)
            return;
        bool on = stance::togglePlayer(c);
        paint(sender, on ? ICON_ON : ICON_OFF);

        feedback::say(c, lang::text(on ? "speech.two_blades" : "speech.one_blade").c_str(), 0);
        diag::count(on ? "ui_toggle_on" : "ui_toggle_off");
        kit::Diag("toggle").kv("who", c->displayName).kv("dual_allowed", on).kv("from", "button").emit();
    }

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

            button = parent->createWidget<MyGUI::Button>(g_skin, MyGUI::IntCoord(parent->getWidth() - (slot + 1) * g_size - slot * 2, 0, g_size, g_size),
                                                         MyGUI::Align::Right | MyGUI::Align::Top, name);
            button->eventMouseButtonClick += MyGUI::newDelegate(click);
            button->setNeedKeyFocus(false);
            MyGUI::ImageBox* icon = button->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(2, 2, g_size - 4, g_size - 4),
                                                                          MyGUI::Align::Stretch, std::string(name) + "_icon");
            icon->setNeedMouseFocus(false);
            diag::count("ui_button_created");
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
        bool show = g_live && (stance::hasPair(c) || !on);
        ensureOne(gui, parent, BUTTON_PREFIX, 0, show, on ? ICON_ON : ICON_OFF, &onClick);
        if (!g_anyEnabled)
            return;
        bool any = stance::anyWeapon(c);

        ensureOne(gui, parent, ANY_PREFIX, 1, g_live && (any || stance::needsAnyWeapon(c)), any ? ICON_ANY_ON : ICON_ANY_OFF,
                  &onAnyClick);
    }

    void (*refreshSection_orig)(InventoryGUI*, InventorySection*) = NULL;
    void refreshSection_hook(InventoryGUI* self, InventorySection* section)
    {
        refreshSection_orig(self, section);
        if (g_enabled && self != NULL && section != NULL)
            ensureButton(self, section);
    }

    bool tipAlive()
    {
        MyGUI::EnumeratorWidgetPtr e = MyGUI::Gui::getInstance().getEnumerator();
        while (e.next())
            if (e.current() == g_tip)
                return true;
        return false;
    }

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

    struct InventoryGUIAccess : public InventoryGUI
    {
        static bool hook()
        {
            return KenshiLib::AddHook(KenshiLib::GetRealAddress(&InventoryGUIAccess::_NV_refreshSection),
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
            return false;
        if (!InventoryGUIAccess::hook())
        {
            ErrorLog("Nitoryu: could not hook InventoryGUI::refreshSection (no toggle button; Alt+Shift+N still works)");
            return false;
        }
        return true;
    }
}

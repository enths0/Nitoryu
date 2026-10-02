#include "kit/input.h"

#include <Debug.h>
#include <core/Functions.h>

#include <kenshi/Character.h>
#include <kenshi/Inventory.h>
#include <kenshi/gui/InventoryGUI.h>

#include <mygui/MyGUI.h>
#include <OgreResourceGroupManager.h>

#include <cstdio>
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

    bool g_enabled = true;
    volatile bool g_live = true;
    int g_size = 22;
    std::string g_skin = "Kenshi_Button1";
    std::string g_dir;
    bool g_locationAdded = false;

    std::map<MyGUI::Widget*, InventoryGUI*> g_buttons;

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

    void paint(MyGUI::Widget* button, bool on)
    {
        for (size_t i = 0; i < button->getChildCount(); ++i)
        {
            MyGUI::ImageBox* icon = button->getChildAt(i)->castType<MyGUI::ImageBox>(false);
            if (icon != NULL)
            {
                icon->setImageTexture(on ? ICON_ON : ICON_OFF);
                return;
            }
        }
    }

    void onClick(MyGUI::Widget* sender)
    {
        std::map<MyGUI::Widget*, InventoryGUI*>::iterator t = g_buttons.find(sender);
        Character* c = t != g_buttons.end() ? t->second->getCallbackCharacter() : NULL;
        if (c == NULL || !c->isPlayerCharacter())
        {
            diag::count("ui_click_no_character");
            return;
        }
        bool on = stance::togglePlayer(c);
        paint(sender, on);

        feedback::say(c, lang::text(on ? "speech.two_blades" : "speech.one_blade").c_str(), 0);
        diag::count(on ? "ui_toggle_on" : "ui_toggle_off");
        kit::Diag("toggle").kv("who", c->displayName).kv("dual_allowed", on).kv("from", "button").emit();
    }

    void ensureButton(InventoryGUI* gui, InventorySection* section)
    {
        if (section->name != "back")
            return;
        Character* c = gui->getCallbackCharacter();
        if (c == NULL || !c->isPlayerCharacter())
            return;
        bool on = stance::playerAllowed(c);
        bool show = g_live && (stance::hasPair(c) || !on);
        SectionGuiMap& guis = *reinterpret_cast<SectionGuiMap*>(reinterpret_cast<char*>(gui) + OFF_INVGUI_SECTIONS);
        SectionGuiMap::iterator it = guis.find(section->name);
        if (it == guis.end() || it->second == NULL || it->second->getWidget() == NULL)
        {
            diag::count("ui_no_section_widget");
            return;
        }
        MyGUI::Widget* parent = it->second->getWidget();
        char name[96];
        sprintf_s(name, "%s%p", BUTTON_PREFIX, static_cast<void*>(gui));
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

            button = parent->createWidget<MyGUI::Button>(g_skin, MyGUI::IntCoord(parent->getWidth() - g_size, 0, g_size, g_size),
                                                         MyGUI::Align::Right | MyGUI::Align::Top, name);
            button->eventMouseButtonClick += MyGUI::newDelegate(&onClick);
            button->setNeedKeyFocus(false);
            MyGUI::ImageBox* icon = button->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(2, 2, g_size - 4, g_size - 4),
                                                                          MyGUI::Align::Stretch, std::string(name) + "_icon");
            icon->setNeedMouseFocus(false);
            diag::count("ui_button_created");
        }
        g_buttons[button] = gui;
        button->setVisible(show);
        paint(button, on);
    }

    void (*refreshSection_orig)(InventoryGUI*, InventorySection*) = NULL;
    void refreshSection_hook(InventoryGUI* self, InventorySection* section)
    {
        refreshSection_orig(self, section);
        if (g_enabled && self != NULL && section != NULL)
            ensureButton(self, section);
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
        g_size = GetPrivateProfileIntA("UI", "button_size", g_size, ini.c_str());
        if (g_size < 12 || g_size > 64)
            g_size = 22;
    }

    bool iniOn() { return g_enabled; }
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

// The stance toggle as an inventory button (Ui.cpp).
#pragma once

namespace MyGUI { class TextBox; }

namespace ui
{
    // The panel size that holds a hover text's caption whole (the buttons' here, the marks' in Hud.cpp).
    // `fresh`: the caption has just changed, one `tip_size` line goes to the log.
    void tipSize(MyGUI::TextBox* tip, const char* font, bool fresh, int& w, int& h);
    void init();            // [UI] from Nitoryu.ini
    bool installHooks();    // InventoryGUI::refreshSection post-hook
    bool iniOn();           // [UI] toggle_button
    void iconLocation();    // main loop: makes the mod folder's PNGs visible to MyGUI (once); the marks use it too
    void setEnabled(bool on);   // false hides the button at the next refresh and creates none
    void tick();            // main loop: the text next to the mouse over a button ([UI] button_tips)
}

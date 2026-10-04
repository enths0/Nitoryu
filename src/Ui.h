#pragma once

namespace MyGUI { class TextBox; }

namespace ui
{

    void tipSize(MyGUI::TextBox* tip, const char* font, bool fresh, int& w, int& h);
    void init();
    bool installHooks();
    bool iniOn();
    void iconLocation();

    void setEnabled(bool on);
    void tick();
}

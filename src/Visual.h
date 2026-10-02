#pragma once

class Character;

namespace visual
{
    void init();

    void setEnabled(bool on);

    void setPoseEngine(bool on);
    bool poseEngine();
    void noteDraw(Character* c);
    void noteSheathe(Character* c);
    void tick();

    bool bladeReady(Character* c);
}

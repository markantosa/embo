#pragma once

#include "ui_screen.h"

class MixingRunningScreen;

// "WARNING: ENSURE SYRINGE IS PROPERLY MOUNTED INSIDE" — the last step
// before a run actually starts. Reached by push() from MixingMenuScreen
// (so its own touch Back button just pops back there); encoder confirm
// starts the run and replaces the whole stack with the running screen
// (goTo — no way back into the warning/menu once a run is underway).
class WarningScreen : public Screen {
public:
    void wire(MixingRunningScreen &runningScreen) { _runningScreen = &runningScreen; }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    MixingRunningScreen *_runningScreen = nullptr;

    void _draw();
};

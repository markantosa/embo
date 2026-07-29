#pragma once

#include "ui_screen.h"

class DoneScreen;

// "Mixing..." loading screen, shown for the duration of a run. Deliberately
// NOT a live sensor readout — see ui.h for why. Handles BTN1 (graceful /
// emergency stop) and detects the scheduler reaching its target on its own.
class RunningScreen : public Screen {
public:
    void wire(DoneScreen &doneScreen) { _doneScreen = &doneScreen; }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    DoneScreen *_doneScreen = nullptr;

    void _draw(bool forceFull);
};

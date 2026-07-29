#pragma once

#include "ui_screen.h"

class RunningScreen;
class VerifyingScreen;

// The idle "pick a target size and start" screen — the first screen shown
// after boot/homing, and where every run begins.
class SetTargetScreen : public Screen {
public:
    // Wired once at boot, after all screens exist (breaks the
    // SetTarget -> Running -> Done -> SetTarget reference cycle) — see
    // ui.cpp.
    void wire(RunningScreen &runningScreen, VerifyingScreen &verifyingScreen) {
        _runningScreen = &runningScreen;
        _verifyingScreen = &verifyingScreen;
    }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    RunningScreen   *_runningScreen   = nullptr;
    VerifyingScreen *_verifyingScreen = nullptr;

    void _draw();
};

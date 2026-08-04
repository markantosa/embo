#pragma once

#include "ui_screen.h"

class VerifyingScreen;

// "End screen" — replaces the old DoneScreen. Shown after a run finishes
// (target reached, safety cap, or a stop/e-stop). MixingRunningScreen
// calls setResult() just before navigating here. Touch "Back to menu"
// returns to the Start Menu (goTo — no way back into this run); encoder
// long-press still offers the independent camera size verification, same
// as before.
class EndScreen : public Screen {
public:
    void wire(Screen &startMenuScreen, VerifyingScreen &verifyingScreen) {
        _startMenuScreen = &startMenuScreen;
        _verifyingScreen = &verifyingScreen;
    }

    void setResult(const char *msg);

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    Screen          *_startMenuScreen = nullptr;
    VerifyingScreen *_verifyingScreen = nullptr;
    char _resultMsg[32] = "";

    void _draw();
};

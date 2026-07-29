#pragma once

#include "ui_screen.h"

class SetTargetScreen;
class VerifyingScreen;

// Shown after a run finishes (target reached, safety cap, or a stop).
// RunningScreen calls setResult() just before navigating here.
class DoneScreen : public Screen {
public:
    void wire(SetTargetScreen &setTargetScreen, VerifyingScreen &verifyingScreen) {
        _setTargetScreen = &setTargetScreen;
        _verifyingScreen = &verifyingScreen;
    }

    void setResult(const char *msg);

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    SetTargetScreen *_setTargetScreen = nullptr;
    VerifyingScreen *_verifyingScreen = nullptr;
    char _resultMsg[32] = "";

    void _draw();
};

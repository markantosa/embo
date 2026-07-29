#pragma once

#include "ui_screen.h"

// Optional, operator-triggered, single-shot CV check — NOT part of the
// mixing loop itself (see scheduler.h). Reachable via push() from any
// screen with a "hold knob to verify" hint (set-target, done); always
// returns to whichever screen pushed it via pop(), so it needs no
// knowledge of its caller.
class VerifyingScreen : public Screen {
public:
    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    char _resultMsg[40] = "";
    char _specMsg[16]   = "";
    bool _inSpec        = false;

    void _draw(bool waiting, bool forceFull);
};

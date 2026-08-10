#pragma once

#include "ui_screen.h"

class VerifyingScreen;

// "End screen" — replaces the old DoneScreen. Shown after a run finishes
// (target reached, safety cap, or a stop/e-stop) — NOT shown after a
// fault (scheduler_hit_fault()), which goes to the persistent fault
// screen instead; auto-re-homing after a detected stall/timeout could
// make things worse, not better, so this deliberately doesn't apply
// there. MixingRunningScreen calls setResult() just before navigating
// here. Touch "Back to menu" returns to the Start Menu (goTo — no way
// back into this run); encoder long-press still offers the independent
// camera size verification, same as before.
//
// onEnter() draws the result immediately, THEN re-homes (blocking,
// unconditionally, same as scheduler_start()) — so the operator sees the
// actual result (target reached / e-stop / etc.) before the homing wait,
// not a stale Mixing-screen frame. If that re-home fails, this shows the
// persistent fault screen instead — a failed home here is a real
// hardware problem needing attention, same as anywhere else in this
// firmware.
class EndScreen : public Screen {
public:
    void wire(Screen &startMenuScreen, VerifyingScreen &verifyingScreen) {
        _startMenuScreen = &startMenuScreen;
        _verifyingScreen = &verifyingScreen;
    }

    void setResult(const char *msg);

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    Screen          *_startMenuScreen = nullptr;
    VerifyingScreen *_verifyingScreen = nullptr;
    char _resultMsg[32] = "";

    void _draw();
};

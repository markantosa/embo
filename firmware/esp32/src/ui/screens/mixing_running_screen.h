#pragma once

#include "ui_screen.h"

class EndScreen;

// "mixing screen" — replaces the old RunningScreen. ANY input at all
// during mixing — BTN1 (either press length), the encoder knob (either
// press length), or the on-screen EMERGENCY STOP touch button — triggers
// scheduler_emergency_stop() immediately. Deliberately simplified: no
// more graceful-stop-vs-emergency-stop distinction and no pause
// capability on this screen, exactly one outcome for any operator
// interaction while mixing is active.
//
// onEnter() draws this screen immediately, THEN calls scheduler_start()
// (which homes, blocking, unconditionally — see scheduler.cpp) — in that
// order deliberately, so the operator sees the Mixing screen itself
// before the homing wait, not stuck looking at the Warning screen while
// it happens. WarningScreen just navigates here; it doesn't call
// scheduler_start() itself anymore.
class MixingRunningScreen : public Screen {
public:
    void wire(EndScreen &endScreen) { _endScreen = &endScreen; }

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    EndScreen *_endScreen = nullptr;

    void _draw(bool forceFull);
};

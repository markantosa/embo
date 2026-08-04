#pragma once

#include "ui_screen.h"

class EndScreen;

// "mixing screen" — replaces the old RunningScreen. Adds touch Pause/Stop
// buttons on top of everything the old screen already did. BTN1 is
// UNCHANGED and still the authoritative safety control (short = graceful
// stop, held = emergency stop, see ui.h's rationale) — the touch Stop
// button calls the exact same scheduler_stop() BTN1's short-press already
// called, it's just an additional way to reach it, not a replacement.
// Touch Pause is new capability (scheduler_pause()/resume()), independent
// of Stop — pausing does not end the run.
class MixingRunningScreen : public Screen {
public:
    void wire(EndScreen &endScreen) { _endScreen = &endScreen; }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    EndScreen *_endScreen = nullptr;

    void _draw(bool forceFull);
};

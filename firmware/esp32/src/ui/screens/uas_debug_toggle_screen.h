#pragma once

#include "ui_screen.h"

// Developer Mode > UAS debug mode. Confirms the tradeoff before flipping
// it on ("turning on this mode will enable bluetooth" per the flowchart —
// BLE is off by default, see ble_debug.h) and shows the current state.
// Encoder press or the touch toggle button both flip it; touch Back pops.
class UasDebugToggleScreen : public Screen {
public:
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    void _draw();
};

#pragma once

#include "ui_screen.h"

// Settings > Developer Mode > Telemetry. Live readout of load cells,
// turbidity, UAS, and motor position/homed state — pure display, no
// navigation to anything else (UAS debug mode is now a separate sibling
// menu item, see ui.cpp's Developer Mode submenu).
class TelemetryScreen : public Screen {
public:
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    void _draw(bool forceFull);
};

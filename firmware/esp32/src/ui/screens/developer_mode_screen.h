#pragma once

#include "ui_screen.h"

class UasDebugToggleScreen;

// Settings > Developer mode. Shows a live telemetry summary (per the
// flowchart: "telemetry view of sensors e.g. loadcell readings") and one
// selectable row, "UAS debug mode >", which opens the toggle-confirm
// screen (UasDebugToggleScreen). If more developer-only toggles get added
// later, this is the place to add more rows — see MenuScreen for a
// data-driven list if it grows past one or two entries.
class DeveloperModeScreen : public Screen {
public:
    void wire(UasDebugToggleScreen &uasDebugToggleScreen) {
        _uasDebugToggleScreen = &uasDebugToggleScreen;
    }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    UasDebugToggleScreen *_uasDebugToggleScreen = nullptr;

    void _draw(bool forceFull);
};

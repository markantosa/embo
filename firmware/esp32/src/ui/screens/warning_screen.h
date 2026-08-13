#pragma once

#include <stdint.h>
#include "ui_screen.h"

class MixingRunningScreen;
class ViscosityMixingRunningScreen;

// "WARNING: ENSURE SYRINGE IS PROPERLY MOUNTED INSIDE" — the last step
// before a run actually starts. Reached by push() from MixingMenuScreen or
// ViscosityMenuScreen (so its own touch Back button just pops back to
// whichever one it came from); encoder confirm starts the run and
// replaces the whole stack with the appropriate running screen (goTo — no
// way back into the warning/menu once a run is underway) — Size targets
// go to MixingRunningScreen, Viscosity targets go to
// ViscosityMixingRunningScreen, chosen at confirm time via
// mixing_options_get_target_type().
class WarningScreen : public Screen {
public:
    void wire(MixingRunningScreen &sizeRunningScreen, ViscosityMixingRunningScreen &viscosityRunningScreen) {
        _sizeRunningScreen = &sizeRunningScreen;
        _viscosityRunningScreen = &viscosityRunningScreen;
    }

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    MixingRunningScreen           *_sizeRunningScreen      = nullptr;
    ViscosityMixingRunningScreen  *_viscosityRunningScreen = nullptr;

    void _draw();

    // Syringe-presence note — see SYRINGE_DETECT_* in config.h.
    uint16_t _alsBaseline = 0;
    bool     _alsBaselineSet = false;
    uint32_t _lastAlsSampleMs = 0;
    bool     _syringeDetected = false;
    bool     _noteNeedsRedraw = false;

    void _pollSyringeDetect();
    void _drawSyringeNote();
};

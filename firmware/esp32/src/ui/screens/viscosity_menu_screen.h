#pragma once

#include "ui_screen.h"

class WarningScreen;
class VerifyingScreen;

// "Viscosity Menu" — reached from Target Type > Viscosity, structurally a
// mirror of MixingMenuScreen (same encoder-adjust-then-confirm pattern),
// adjusting mixing_options_get/set_viscosity_target_pa_s() instead of the
// particle-size target. IMPORTANT: this is stored/displayed only — there
// is no viscosity calibration or measurement anywhere in this firmware,
// so a run started from here still runs the exact same way as one
// started from Size Selection; the actual stop condition (scheduler.cpp)
// is still purely the UAS-voltage particle-size equation regardless. See
// mixing_options.h for the same scoping note.
class ViscosityMenuScreen : public Screen {
public:
    void wire(WarningScreen &warningScreen, VerifyingScreen &verifyingScreen) {
        _warningScreen = &warningScreen;
        _verifyingScreen = &verifyingScreen;
    }

    void update(ScreenManager &mgr, bool forceFull) override;

private:
    WarningScreen   *_warningScreen   = nullptr;
    VerifyingScreen *_verifyingScreen = nullptr;

    void _draw(bool forceFull);
};

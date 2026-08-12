#pragma once

#include "ui_screen.h"

class EndScreen;

// "viscosity mixing screen" — structurally a mirror of MixingRunningScreen
// (same onEnter()/any-input-emergency-stop/target-reached/fault handling),
// showing the live viscosity reading (scheduler_get_last_measured_viscosity_pa_s())
// and its raw force input instead of the particle-size/UAS-voltage
// readout. WarningScreen picks between this and MixingRunningScreen based
// on mixing_options_get_target_type() at confirm time — see
// warning_screen.cpp.
class ViscosityMixingRunningScreen : public Screen {
public:
    void wire(EndScreen &endScreen) { _endScreen = &endScreen; }

    void onEnter() override;
    void update(ScreenManager &mgr, bool forceFull) override;

private:
    EndScreen *_endScreen = nullptr;

    void _draw(bool forceFull);
};

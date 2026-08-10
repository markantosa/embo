#pragma once

#include "ui_screen.h"

class WarningScreen;
class VerifyingScreen;

// "Mixing Menu" from the Start Menu's Start item. Target size adjustment
// (encoder rotation) is the same control the old SetTargetScreen had; the
// syringe preset (Terumo/Nipro) is new and, per product decision, cosmetic
// only for now — it's stored and displayed but doesn't change any
// calibration constant yet (see mixing_options.h).
class MixingMenuScreen : public Screen {
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

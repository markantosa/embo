#include "warning_screen.h"
#include "mixing_running_screen.h"
#include "viscosity_mixing_running_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "mixing_options.h"
#include "turbidity.h"
#include "config.h"

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void WarningScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_WHITE);
    tft.fillRect(0, 0, tft.width(), 30, COLOR_CLOWN_NOSE);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("WARNING", 4, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Ensure syringe is properly", 90, TFT_BLACK, 1);
    ui_display_draw_centered("mounted inside", 115, TFT_BLACK, 1);
    ui_display_draw_centered("Press knob to confirm and start", 200, COLOR_ASH, 1);
    ui_display_draw_centered("(hold knob to go back)", 245, COLOR_ASH, 1);
    if (_syringeDetected) _drawSyringeNote();
}

// Hint only — a missed detection (e.g. syringe already in before this
// screen was reached, or ambient light drifting instead of a real
// insertion) just means no note shows / a possibly wrong note, never
// something that blocks starting a run. See SYRINGE_DETECT_* in config.h.
void WarningScreen::_pollSyringeDetect() {
    if (_syringeDetected || !turbidity_apds_ok()) return;

    uint32_t now = millis();
    if (now - _lastAlsSampleMs < SYRINGE_DETECT_SAMPLE_INTERVAL_MS) return;
    _lastAlsSampleMs = now;

    uint16_t sample = turbidity_get_als_clear();
    if (!_alsBaselineSet) {
        _alsBaseline = sample;
        _alsBaselineSet = true;
        return;
    }
    if ((int32_t)sample - (int32_t)_alsBaseline >= SYRINGE_DETECT_RISE_COUNTS) {
        _syringeDetected = true;
        _noteNeedsRedraw = true;
    }
}

void WarningScreen::_drawSyringeNote() {
    ui_display_tft().setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Syringe detected, proceed to mixing", 150, COLOR_ASH, 1);
}

void WarningScreen::onEnter() {
    _alsBaselineSet = false;
    _lastAlsSampleMs = 0;
    _syringeDetected = false;
    _noteNeedsRedraw = false;
}

void WarningScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    _pollSyringeDetect();
    if (_noteNeedsRedraw) {
        _drawSyringeNote();
        _noteNeedsRedraw = false;
    }

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS) {
        // Homing (and scheduler_start() itself) now happens from
        // MixingRunningScreen/ViscosityMixingRunningScreen::onEnter(),
        // AFTER the screen has actually switched — not here, blocking on
        // this screen. Just navigate to whichever one matches the
        // current target type.
        Screen *target = (mixing_options_get_target_type() == TargetType::VISCOSITY)
            ? static_cast<Screen *>(_viscosityRunningScreen)
            : static_cast<Screen *>(_sizeRunningScreen);
        if (target) {
            mgr.goTo(target);
        }
        return;
    }
    // Long-press as a back fallback while touch is unavailable — see
    // LGFX_Config.h's TOUCH_TODO.
    if (ev == ButtonEvent::LONG_PRESS) {
        mgr.pop();
        return;
    }
    // BTN1 = Back here too, gated on !scheduler_is_running() — see
    // menu_screen.cpp for the full rationale. This screen is only reached
    // before a run starts, so the guard is always true here in practice,
    // but it costs nothing to state explicitly.
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }
    if (ui_input_poll_touch_tap(&kBackButton, 1) == 0) {
        mgr.pop();
    }
}

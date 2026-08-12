#include "mixing_running_screen.h"
#include "end_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "ui.h"
#include <Arduino.h>
#include <stdio.h>

// Single large E-STOP button — ANY input at all (this button, BTN1
// short/long press, or the encoder knob short/long press) triggers
// scheduler_emergency_stop() immediately during mixing. No more
// distinction between graceful stop, pause, and emergency stop on this
// screen — deliberately simplified so there's exactly one outcome for any
// operator interaction while mixing is active: stop now.
static const TouchButton kStopButton = { 60, 255, 200, 45, "EMERGENCY STOP" };

// Live UAS readout refresh rate — throttled separately from forceFull.
// The underlying value (scheduler.cpp) updates roughly every loop()
// iteration, but that's far faster than useful for a number a person is
// reading, not measuring — a few times a second is plenty and avoids
// redrawing text on every single iteration.
#define MIXING_SCREEN_UAS_REFRESH_INTERVAL_MS 250

void MixingRunningScreen::onEnter() {
    // Draw the Mixing screen itself FIRST, before the blocking homing call
    // below — so the operator sees this screen (spinner, button, "Mixing"
    // title) rather than staying on the Warning screen for the whole
    // homing wait. forceFull=true forces the full static content (title,
    // button) to draw, matching what a normal first-update() would do.
    _draw(true);

    if (!scheduler_start()) {
        ui_show_error("HOMING FAILED - check limit switches");
    }
}

void MixingRunningScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();

    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Mixing", 70, TFT_BLACK, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        ui_display_draw_centered("Press any button to emergency stop", 220, COLOR_ASH, 1);
        ui_display_draw_touch_button(kStopButton.x, kStopButton.y, kStopButton.w, kStopButton.h,
                                      kStopButton.label, COLOR_CLOWN_NOSE, TFT_WHITE);
    }

    // Live UAS readout — same values EndScreen shows at the end, just
    // continuously refreshed here while a run is actually in progress.
    static uint32_t lastUasDrawMs = 0;
    uint32_t now = millis();
    if (forceFull || now - lastUasDrawMs >= MIXING_SCREEN_UAS_REFRESH_INTERVAL_MS) {
        lastUasDrawMs = now;
        tft.fillRect(0, 100, tft.width(), 48, TFT_WHITE);
        char line[48];
        snprintf(line, sizeof(line), "%.1f um", scheduler_get_last_fused_size_um());
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered(line, 103, TFT_BLACK, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        snprintf(line, sizeof(line), "V=%.3fV  target=%u um",
                 scheduler_get_last_measured_voltage(), scheduler_get_target_um());
        ui_display_draw_centered(line, 132, COLOR_ASH, 1);
    }

    ui_display_draw_spinner(160);
}

void MixingRunningScreen::update(ScreenManager &mgr, bool forceFull) {
    if (!_endScreen) return;  // not wired — see ui.cpp

    // ANY input during mixing is an emergency stop — BTN1 (either press
    // length), the encoder knob (either press length), or the touch
    // button all lead here the same way. No graceful stop, no pause, on
    // this screen — deliberately simplified for maximum responsiveness.
    bool anyInput = false;
    if (ui_input_poll_btn1() != ButtonEvent::NONE) anyInput = true;
    if (ui_input_poll_enc_sw() != ButtonEvent::NONE) anyInput = true;
    if (ui_input_poll_touch_tap(&kStopButton, 1) == 0) anyInput = true;

    if (anyInput) {
        scheduler_emergency_stop();
        _endScreen->setResult("STOPPED (e-stop)");
        mgr.goTo(_endScreen);
        return;
    }

    if (scheduler_target_reached()) {
        _endScreen->setResult(scheduler_hit_safety_cap() ? "Stopped: safety cap" : "Target size reached");
        mgr.goTo(_endScreen);
        return;
    }

    // Check BEFORE the generic !scheduler_is_running() below — a fault
    // also makes scheduler_is_running() false (motors are stopped either
    // way), but it needs the persistent fault screen, not a normal "the
    // run finished" result on the End screen.
    if (scheduler_hit_fault()) {
        ui_show_error("MIXING FAULT - stall or timeout detected, check motors");
        return;
    }

    if (!scheduler_is_running()) {
        // Defensive fallback — nothing on this screen calls a graceful
        // scheduler_stop() anymore (that's what the "any input" branch
        // above replaced), so this shouldn't normally fire, but stopping
        // running without landing anywhere would leave the operator
        // stuck if scheduler_is_running() ever goes false for an
        // unanticipated reason.
        _endScreen->setResult("Stopped");
        mgr.goTo(_endScreen);
        return;
    }

    _draw(forceFull);
}

#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "rpi_uart.h"
#include "calibration.h"
#include "motors.h"
#include "scheduler.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void VerifyingScreen::onEnter() {
    _resultMsg[0] = '\0';
    _specMsg[0] = '\0';
    rpi_request_capture();
}

void VerifyingScreen::_draw(bool waiting, bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Camera Verify", 50, TFT_WHITE, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
    }
    if (waiting) {
        if (forceFull) ui_display_draw_centered("Capturing...", 130, COLOR_LUNAR_ROCK, 1);
        ui_display_draw_spinner(180);
    } else {
        tft.fillRect(0, 110, tft.width(), 90, TFT_BLACK);
        ui_display_draw_centered(_resultMsg, 140, TFT_WHITE, 1);
        if (_specMsg[0] != '\0') {
            ui_display_draw_centered(_specMsg, 170, _inSpec ? COLOR_BRIGHT_BLUE : COLOR_CLOWN_NOSE, 1);
        }
        ui_display_draw_centered("Press knob to continue", 220, COLOR_LUNAR_ROCK, 1);
    }
}

void VerifyingScreen::update(ScreenManager &mgr, bool forceFull) {
    RpiCaptureStatus status = rpi_capture_status();
    if (status == RpiCaptureStatus::RESULT_READY) {
        int16_t median, iqr;
        rpi_pop_capture_result(median, iqr);
        // Feeds the (diagnostic-only) breakage-model fit for cross-checking
        // against the live sensor-fusion estimate in future runs — does not
        // alter the run already done, and does not itself drive the stop
        // condition (see scheduler.h).
        calib_breakage_add_point(motor_get_stroke_count(), (float)median);
        snprintf(_resultMsg, sizeof(_resultMsg), "Median %d um   IQR %d um", median, iqr);
        _inSpec = (abs((int)median - (int)scheduler_get_target_um()) <= TARGET_TOLERANCE_UM);
        strncpy(_specMsg, _inSpec ? "IN SPEC" : "OUT OF SPEC", sizeof(_specMsg) - 1);
        forceFull = true;
    } else if (status == RpiCaptureStatus::TIMED_OUT) {
        int16_t dummyMedian, dummyIqr;
        rpi_pop_capture_result(dummyMedian, dummyIqr);  // consumes/clears the timeout flag
        strncpy(_resultMsg, "No response from RPi", sizeof(_resultMsg) - 1);
        _specMsg[0] = '\0';
        forceFull = true;
    }

    bool waiting = (_resultMsg[0] == '\0');
    // Redraw when something changed (forceFull) or while waiting (to keep
    // the spinner animating) — once the result is showing and nothing's
    // changed, skip the redraw entirely so the screen doesn't flicker.
    if (forceFull || waiting) _draw(waiting, forceFull);

    if (!waiting && ui_input_poll_enc_sw() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
    }
}

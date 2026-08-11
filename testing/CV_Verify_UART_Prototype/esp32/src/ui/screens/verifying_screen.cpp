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
    _timedOut = false;
    rpi_request_capture();
}

void VerifyingScreen::_draw(bool waiting, bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Camera Verify", 20, TFT_BLACK, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
    }
    if (waiting) {
        if (forceFull) ui_display_draw_centered("Capturing...", 130, COLOR_ASH, 1);
        ui_display_draw_spinner(180);
    } else if (_timedOut) {
        tft.fillRect(0, 110, tft.width(), 90, TFT_WHITE);
        ui_display_draw_centered(_resultMsg, 140, TFT_BLACK, 1);
        ui_display_draw_centered("Press knob to continue", 220, COLOR_ASH, 1);
    } else {
        // Received image, drawn 1:1 (no scaling — RPI_IMG_MAX_W/H were
        // chosen to fit directly under the title), centered horizontally.
        uint16_t imgW = rpi_get_last_image_width();
        uint16_t imgH = rpi_get_last_image_height();
        if (imgW > 0 && imgH > 0) {
            int16_t imgX = (tft.width() - imgW) / 2;
            ui_display_draw_grayscale_image(imgX, 40, imgW, imgH, rpi_get_last_image());
        }
        _drawStubHistogramAndMedian();
        ui_display_draw_centered("Press knob to continue", 300, COLOR_ASH, 1);
    }
}

void VerifyingScreen::_drawStubHistogramAndMedian() {
    LGFX &tft = ui_display_tft();

    // PROTOTYPE: fixed, made-up bar heights — not derived from any real
    // measurement. Just enough to prove out the "histogram + median"
    // layout ahead of detection.py/sizing.py actually existing. Replace
    // this whole function with a real PSD histogram (bucketed ECD counts
    // from sizing.py) once that data exists — nothing else on this screen
    // needs to change when that happens, the image-draw and layout stay
    // the same.
    static const uint8_t kStubBarHeightsPct[] = {20, 45, 70, 100, 55, 30, 15};
    static const uint8_t kNumBars = sizeof(kStubBarHeightsPct) / sizeof(kStubBarHeightsPct[0]);

    const int16_t histX = 30, histY = 150, histW = 180, histH = 70;
    const int16_t barGap = 4;
    const int16_t barW = (histW - barGap * (kNumBars - 1)) / kNumBars;

    tft.fillRect(0, histY - 5, tft.width(), histH + 15, TFT_WHITE);  // clear previous
    for (uint8_t i = 0; i < kNumBars; i++) {
        int16_t barH = (histH * kStubBarHeightsPct[i]) / 100;
        int16_t barX = histX + i * (barW + barGap);
        int16_t barY = histY + (histH - barH);
        tft.fillRect(barX, barY, barW, barH, COLOR_GUILLIMAN_BLUE);
    }
    tft.drawFastHLine(histX, histY + histH, histW, COLOR_ASH);  // baseline

    tft.fillRect(0, 235, tft.width(), 30, TFT_WHITE);
    char stubMedianMsg[40];
    snprintf(stubMedianMsg, sizeof(stubMedianMsg), "Median: %d um (stub)", 320);
    ui_display_draw_centered(stubMedianMsg, 245, TFT_BLACK, 1);
}

void VerifyingScreen::update(ScreenManager &mgr, bool forceFull) {
    RpiCaptureStatus status = rpi_capture_status();
    if (status == RpiCaptureStatus::RESULT_READY) {
        int16_t median, iqr;
        bool gotSize = rpi_pop_capture_result(median, iqr);
        // PROTOTYPE: image arrival alone completes the capture (see
        // rpi_uart.h) — a real SIZE line is NOT expected from this
        // prototype's Pi side yet (detection.py is still unimplemented),
        // so gotSize/median/iqr are usually stale/unset here. Only feed
        // the breakage-model fit when a real SIZE line actually arrived,
        // so stub/placeholder data never contaminates it.
        if (gotSize && median > 0) {
            calib_breakage_add_point(motor_get_stroke_count(), (float)median);
        }
        strncpy(_resultMsg, "Capture received", sizeof(_resultMsg) - 1);
        _specMsg[0] = '\0';  // no real spec check without a real median yet
        _timedOut = false;
        forceFull = true;
    } else if (status == RpiCaptureStatus::TIMED_OUT) {
        int16_t dummyMedian, dummyIqr;
        rpi_pop_capture_result(dummyMedian, dummyIqr);  // consumes/clears the timeout flag
        strncpy(_resultMsg, "No response from RPi", sizeof(_resultMsg) - 1);
        _specMsg[0] = '\0';
        _timedOut = true;
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

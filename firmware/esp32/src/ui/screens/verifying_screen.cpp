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
    _timedOut = false;
    _gotSize = false;
    _inSpec = false;
    _lastMedianUm = -1;
    _lastIqrUm = -1;
    rpi_request_capture();
}

void VerifyingScreen::_draw(bool waiting, bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        // Moved up from the original y=50 to y=20 to make room for the
        // image + result layout below (image starts at y=45) — the old
        // y=50 title would otherwise overlap the top of the image.
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
        // Two-column result layout: captured (annotated) image on the
        // left, median/IQR/spec stacked on the right — sized around
        // whatever the image actually is (RPI_IMG_MAX_W/H) rather than
        // hardcoded, so this still lays out sanely if that constant ever
        // changes.
        uint16_t imgW = rpi_get_last_image_width();
        uint16_t imgH = rpi_get_last_image_height();
        const int16_t imgX = 8, imgY = 45;
        if (imgW > 0 && imgH > 0) {
            ui_display_draw_grayscale_image(imgX, imgY, imgW, imgH, rpi_get_last_image());
        }
        const int16_t rightX = imgX + imgW + 12;
        const int16_t rightW = tft.width() - rightX - 8;
        _drawSizeResult(rightX, imgY, rightW, imgH);

        int16_t footerY = imgY + imgH + 25;
        if (footerY > tft.height() - 20) footerY = tft.height() - 20;
        ui_display_draw_centered("Press knob to continue", footerY, COLOR_ASH, 1);
    }
}

void VerifyingScreen::_drawSizeResult(int16_t x, int16_t y, int16_t w, int16_t colH) {
    LGFX &tft = ui_display_tft();
    tft.fillRect(x, y, w, colH, TFT_WHITE);  // clear previous (image column untouched)
    tft.setFont(&fonts::FreeSans9pt7b);
    tft.setTextSize(1);

    if (!_gotSize) {
        // No real SIZE line this capture (e.g. zero particles detected,
        // or the Pi-side detector errored) — say so instead of drawing
        // fabricated numbers.
        tft.setTextColor(COLOR_ASH);
        const char *l1 = "No size data";
        const char *l2 = "(no particles found)";
        tft.setCursor(x + (w - tft.textWidth(l1)) / 2, y + colH / 2 - 14);
        tft.print(l1);
        tft.setCursor(x + (w - tft.textWidth(l2)) / 2, y + colH / 2 + 4);
        tft.print(l2);
        return;
    }

    // Real result: median prominent, IQR shown as a spread bar centered
    // on the median. No per-bin histogram is drawn — the wire protocol
    // (rpi_uart.h) only carries median+IQR, not bucketed ECD counts, so
    // there's no real distribution to bucket. Spread bar width is capped
    // to the column width; it's a relative indicator, not an absolute
    // axis (no fixed real-world min/max is defined anywhere upstream).
    char medianMsg[24];
    snprintf(medianMsg, sizeof(medianMsg), "%d um", _lastMedianUm);
    tft.setTextColor(TFT_BLACK);
    int16_t textW = tft.textWidth(medianMsg);
    tft.setCursor(x + (w - textW) / 2, y + 4);
    tft.print(medianMsg);

    const int16_t barY = y + 34;
    const int16_t barH = 14;
    tft.fillRect(x, barY, w, barH, TFT_WHITE);
    tft.drawFastHLine(x, barY + barH / 2, w, COLOR_ASH);  // axis baseline
    // IQR half-width as a fraction of the median, clamped so a degenerate
    // (zero) or huge IQR still renders something sane on a fixed-width bar.
    float spreadFrac = (_lastMedianUm > 0) ? ((float)_lastIqrUm / (float)_lastMedianUm) : 0.0f;
    if (spreadFrac < 0.02f) spreadFrac = 0.02f;
    if (spreadFrac > 1.0f) spreadFrac = 1.0f;
    int16_t halfBarW = (int16_t)((w / 2) * spreadFrac);
    int16_t centerX = x + w / 2;
    tft.fillRect(centerX - halfBarW, barY, halfBarW * 2, barH, COLOR_GUILLIMAN_BLUE);
    tft.fillRect(centerX - 1, barY - 3, 2, barH + 6, TFT_BLACK);  // median tick

    char iqrMsg[24];
    snprintf(iqrMsg, sizeof(iqrMsg), "IQR %d um", _lastIqrUm);
    tft.setTextColor(COLOR_ASH);
    textW = tft.textWidth(iqrMsg);
    tft.setCursor(x + (w - textW) / 2, barY + barH + 10);
    tft.print(iqrMsg);

    const char *specMsg = _inSpec ? "IN SPEC" : "OUT OF SPEC";
    tft.setTextColor(_inSpec ? COLOR_BRIGHT_BLUE : COLOR_CLOWN_NOSE);
    textW = tft.textWidth(specMsg);
    tft.setCursor(x + (w - textW) / 2, barY + barH + 30);
    tft.print(specMsg);
}

void VerifyingScreen::update(ScreenManager &mgr, bool forceFull) {
    RpiCaptureStatus status = rpi_capture_status();
    if (status == RpiCaptureStatus::RESULT_READY) {
        int16_t median, iqr;
        bool gotSize = rpi_pop_capture_result(median, iqr);
        // Image arrival alone completes the capture (see rpi_uart.h) — a
        // SIZE line is not guaranteed every capture (e.g. zero particles
        // detected), so only trust/display/feed a result when a real SIZE
        // line actually arrived, so absent data never contaminates the
        // breakage-model fit or gets shown as if it were real.
        _gotSize = gotSize && median > 0;
        if (_gotSize) {
            _lastMedianUm = median;
            _lastIqrUm = iqr;
            // Feeds the (diagnostic-only) breakage-model fit for cross-checking
            // against the live sensor-fusion estimate in future runs — does not
            // alter the run already done, and does not itself drive the stop
            // condition (see scheduler.h).
            calib_breakage_add_point(motor_get_stroke_count(), (float)median);
            _inSpec = (abs((int)median - (int)scheduler_get_target_um()) <= TARGET_TOLERANCE_UM);
        } else {
            _lastMedianUm = -1;
            _lastIqrUm = -1;
            _inSpec = false;
        }
        strncpy(_resultMsg, "Capture received", sizeof(_resultMsg) - 1);
        _timedOut = false;
        forceFull = true;
    } else if (status == RpiCaptureStatus::TIMED_OUT) {
        int16_t dummyMedian, dummyIqr;
        rpi_pop_capture_result(dummyMedian, dummyIqr);  // consumes/clears the timeout flag
        strncpy(_resultMsg, "No response from RPi", sizeof(_resultMsg) - 1);
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

#include "ui_display.h"
#include <Arduino.h>

LGFX &ui_display_tft() {
    static LGFX tft;  // constructed on first use, lives for the program's duration
    return tft;
}

void ui_display_draw_centered(const char *text, int16_t y, uint16_t color, uint8_t size) {
    LGFX &tft = ui_display_tft();
    tft.setTextColor(color);
    tft.setTextSize(size);
    int16_t w = tft.textWidth(text);
    tft.setCursor((tft.width() - w) / 2, y);
    tft.print(text);
}

static uint8_t  _spinnerFrame   = 0;
static uint32_t _lastSpinnerMs  = 0;

void ui_display_draw_spinner(int16_t cy) {
    uint32_t now = millis();
    if (now - _lastSpinnerMs < 150) return;
    _lastSpinnerMs = now;
    _spinnerFrame = (_spinnerFrame + 1) % 4;

    LGFX &tft = ui_display_tft();
    int16_t cx = tft.width() / 2;
    tft.fillRect(cx - 40, cy - 10, 80, 20, TFT_WHITE);
    char dots[5] = "";
    for (uint8_t i = 0; i < _spinnerFrame; i++) dots[i] = '.';
    dots[_spinnerFrame] = '\0';
    ui_display_draw_centered(dots, cy - 8, TFT_BLACK, 2);
}

void ui_display_draw_touch_button(int16_t x, int16_t y, int16_t w, int16_t h,
                                   const char *label, uint16_t bgColor, uint16_t textColor) {
    LGFX &tft = ui_display_tft();
    tft.fillRoundRect(x, y, w, h, 8, bgColor);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextSize(1);
    tft.setTextColor(textColor);
    int16_t tw = tft.textWidth(label);
    int16_t th = tft.fontHeight();
    tft.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    tft.print(label);
}

void ui_display_draw_grayscale_image(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                      const uint8_t *pixels) {
    LGFX &tft = ui_display_tft();
    tft.startWrite();
    for (uint16_t row = 0; row < h; row++) {
        for (uint16_t col = 0; col < w; col++) {
            uint8_t g = pixels[(uint32_t)row * w + col];
            // 8-bit grey -> RGB565: replicate into each channel at its
            // native bit depth (5/6/5) rather than a flat shift, so pure
            // white/black still map exactly to 0xFFFF/0x0000.
            uint16_t r5 = g >> 3;
            uint16_t g6 = g >> 2;
            uint16_t b5 = g >> 3;
            uint16_t color = (r5 << 11) | (g6 << 5) | b5;
            tft.writePixel(x + col, y + row, color);
        }
    }
    tft.endWrite();
}

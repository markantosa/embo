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
    tft.fillRect(cx - 40, cy - 10, 80, 20, TFT_BLACK);
    char dots[5] = "";
    for (uint8_t i = 0; i < _spinnerFrame; i++) dots[i] = '.';
    dots[_spinnerFrame] = '\0';
    ui_display_draw_centered(dots, cy - 8, TFT_WHITE, 2);
}

#include "error_screen.h"
#include "ui_display.h"
#include <string.h>

void ErrorScreen::setMessage(const char *msg) {
    strncpy(_msg, msg, sizeof(_msg) - 1);
    _msg[sizeof(_msg) - 1] = '\0';
}

void ErrorScreen::update(ScreenManager &mgr, bool forceFull) {
    (void)mgr;
    if (!forceFull) return;  // static screen, nothing to poll, no way out but reboot

    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("HARDWARE FAULT", 100, COLOR_CLOWN_NOSE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered(_msg, 140, COLOR_CLOWN_NOSE, 1);
    ui_display_draw_centered("Reboot required", 200, COLOR_LUNAR_ROCK, 1);
}

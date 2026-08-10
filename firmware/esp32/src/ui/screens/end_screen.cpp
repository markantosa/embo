#include "end_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include <string.h>

static const TouchButton kBackButton = { 20, 260, 140, 40, "Back to menu" };

void EndScreen::setResult(const char *msg) {
    strncpy(_resultMsg, msg, sizeof(_resultMsg) - 1);
    _resultMsg[sizeof(_resultMsg) - 1] = '\0';
}

void EndScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_WHITE);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("Results", 30, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered(_resultMsg, 100, COLOR_BRIGHT_BLUE, 1);
    ui_display_draw_centered("Hold knob to verify with camera", 220, COLOR_ASH, 1);
    ui_display_draw_centered("(or press BTN1)", 245, COLOR_ASH, 1);
}

void EndScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    if (ui_input_poll_enc_sw() == ButtonEvent::LONG_PRESS && _verifyingScreen) {
        mgr.push(_verifyingScreen);
        return;
    }
    // BTN1 = Back to menu here too, gated on !scheduler_is_running() — see
    // menu_screen.cpp for the full rationale. A run has always genuinely
    // ended by the time this screen is showing, so the guard is always
    // true here in practice. Uses goTo (not pop), matching the touch
    // button below — there's nothing to "return to" after a finished run.
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS && _startMenuScreen) {
        mgr.goTo(_startMenuScreen);
        return;
    }
    if (ui_input_poll_touch_tap(&kBackButton, 1) == 0 && _startMenuScreen) {
        mgr.goTo(_startMenuScreen);
    }
}

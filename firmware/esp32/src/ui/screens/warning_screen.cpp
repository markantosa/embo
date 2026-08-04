#include "warning_screen.h"
#include "mixing_running_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void WarningScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, tft.width(), 30, TFT_RED);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("WARNING", 4, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Ensure syringe is properly", 90, TFT_WHITE, 1);
    ui_display_draw_centered("mounted inside", 115, TFT_WHITE, 1);
    ui_display_draw_centered("Press knob to confirm and start", 200, TFT_DARKGREY, 1);
    ui_display_draw_centered("(hold knob to go back)", 245, TFT_DARKGREY, 1);
}

void WarningScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS && _runningScreen) {
        scheduler_start();
        mgr.goTo(_runningScreen);
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

#include "placeholder_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void PlaceholderScreen::configure(const char *title, const char *message, bool showBack) {
    _title = title;
    _message = message;
    _showBack = showBack;
}

void PlaceholderScreen::setConfirmTarget(Screen *target, bool viaPush) {
    _confirmTarget = target;
    _confirmPushes = viaPush;
}

void PlaceholderScreen::onEnter() {}

void PlaceholderScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    int16_t y = 80;
    if (_title[0] != '\0') {
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered(_title, y, TFT_WHITE, 1);
        y += 40;
    }
    ui_display_draw_centered(_message, y, COLOR_LUNAR_ROCK, 1);
    if (_confirmTarget) {
        ui_display_draw_centered("Press knob to continue", 220, COLOR_LUNAR_ROCK, 1);
    }
    if (_showBack) {
        ui_display_draw_touch_button(kBackButton.x, kBackButton.y, kBackButton.w, kBackButton.h,
                                      kBackButton.label, COLOR_LUNAR_ROCK, TFT_WHITE);
        ui_display_draw_centered("(or hold knob)", 245, COLOR_LUNAR_ROCK, 1);
    }
}

void PlaceholderScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS && _confirmTarget) {
        if (_confirmPushes) mgr.push(_confirmTarget);
        else mgr.goTo(_confirmTarget);
        return;
    }
    // Long-press as a back fallback while touch is unavailable (see
    // LGFX_Config.h's TOUCH_TODO) — the touch Back button below still
    // works whenever touch does; this just means a dead touchscreen isn't
    // a dead end.
    if (ev == ButtonEvent::LONG_PRESS && _showBack) {
        mgr.pop();
        return;
    }
    // BTN1 = Back here too, gated on !scheduler_is_running() — see
    // menu_screen.cpp for the full rationale.
    if (_showBack && !scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }

    if (_showBack && ui_input_poll_touch_tap(&kBackButton, 1) == 0) {
        mgr.pop();
    }
}

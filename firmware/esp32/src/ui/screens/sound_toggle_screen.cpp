#include "sound_toggle_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "sound_settings.h"
#include "scheduler.h"

static const TouchButton kButtons[] = {
    { 60,  180, 200, 40, "Toggle" },
    { 20,  260, 100, 40, "Back" },
};

void SoundToggleScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("Sound", 25, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Boot chirp and future UI tones", 90, COLOR_LUNAR_ROCK, 1);
    ui_display_draw_centered("(resets to ON every power-on)", 115, COLOR_LUNAR_ROCK, 1);

    bool on = sound_is_enabled();
    ui_display_draw_centered(on ? "Currently: ON" : "Currently: OFF", 150,
                              on ? COLOR_BRIGHT_BLUE : COLOR_CLOWN_NOSE, 1);

    ui_display_draw_touch_button(kButtons[0].x, kButtons[0].y, kButtons[0].w, kButtons[0].h,
                                  on ? "Turn OFF" : "Turn ON", on ? COLOR_CLOWN_NOSE : COLOR_BRIGHT_BLUE, TFT_BLACK);
    ui_display_draw_touch_button(kButtons[1].x, kButtons[1].y, kButtons[1].w, kButtons[1].h,
                                  kButtons[1].label, COLOR_LUNAR_ROCK, TFT_WHITE);
    ui_display_draw_centered("Or press knob to toggle", 230, COLOR_LUNAR_ROCK, 1);
    ui_display_draw_centered("(hold knob to go back)", 245, COLOR_LUNAR_ROCK, 1);
}

void SoundToggleScreen::update(ScreenManager &mgr, bool forceFull) {
    if (forceFull) _draw();

    ButtonEvent ev = ui_input_poll_enc_sw();
    bool toggled = (ev == ButtonEvent::SHORT_PRESS);
    if (ev == ButtonEvent::LONG_PRESS) {
        mgr.pop();
        return;
    }
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }

    int tap = ui_input_poll_touch_tap(kButtons, 2);
    if (tap == 0) {
        toggled = true;
    } else if (tap == 1) {
        mgr.pop();
        return;
    }

    if (toggled) {
        sound_set_enabled(!sound_is_enabled());
        _draw();
    }
}

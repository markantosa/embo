#include "telemetry_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "force_sensor.h"
#include "turbidity.h"
#include "uas.h"
#include "motors.h"
#include <stdio.h>

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void TelemetryScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_BLACK);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Telemetry", 15, TFT_WHITE, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        ui_display_draw_touch_button(kBackButton.x, kBackButton.y, kBackButton.w, kBackButton.h,
                                      kBackButton.label, COLOR_LUNAR_ROCK, TFT_WHITE);
        ui_display_draw_centered("(or hold knob)", 245, COLOR_LUNAR_ROCK, 1);
    }

    // Live telemetry — refreshed every call, not just on forceFull (this is
    // the whole point of the screen). Cheap enough: a handful of fillRect +
    // text draws, same pattern as the spinner elsewhere. Bounded well
    // clear of the Back button/hint below it.
    tft.fillRect(0, 55, tft.width(), 155, TFT_BLACK);
    char line[48];
    snprintf(line, sizeof(line), "Load cell 1: %.1f g", force_sensor_get_grams_1());
    ui_display_draw_centered(line, 58, COLOR_LUNAR_ROCK, 1);
    snprintf(line, sizeof(line), "Load cell 2: %.1f g", force_sensor_get_grams_2());
    ui_display_draw_centered(line, 78, COLOR_LUNAR_ROCK, 1);
    snprintf(line, sizeof(line), "Turbidity ALS: %u  (%s)",
             turbidity_get_als_clear(), turbidity_apds_ok() ? "ok" : "fault");
    ui_display_draw_centered(line, 98, COLOR_LUNAR_ROCK, 1);
    snprintf(line, sizeof(line), "Turbidity IR: %lu  (%s)",
             (unsigned long)turbidity_get_ir_raw(), turbidity_max_ok() ? "ok" : "fault");
    ui_display_draw_centered(line, 118, COLOR_LUNAR_ROCK, 1);
    snprintf(line, sizeof(line), "UAS reading: %lu mV", (unsigned long)uas_read_mv());
    ui_display_draw_centered(line, 138, COLOR_LUNAR_ROCK, 1);
    // Exact, interrupt-counted position — see motors.h.
    snprintf(line, sizeof(line), "Motors: %s  M1:%ld M2:%ld",
             motors_is_homed() ? "HOMED" : "NOT HOMED",
             (long)motor_get_position(1), (long)motor_get_position(2));
    ui_display_draw_centered(line, 158, COLOR_LUNAR_ROCK, 1);
}

void TelemetryScreen::update(ScreenManager &mgr, bool forceFull) {
    _draw(forceFull);

    ButtonEvent ev = ui_input_poll_enc_sw();
    // Long-press as a back fallback while touch is unavailable — see
    // LGFX_Config.h's TOUCH_TODO.
    if (ev == ButtonEvent::LONG_PRESS) {
        mgr.pop();
        return;
    }
    // BTN1 = Back here too, gated on !scheduler_is_running() — see
    // menu_screen.cpp for the full rationale (same pattern everywhere
    // except mixing_running_screen.cpp, where BTN1 stays stop/e-stop only).
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }
    if (ui_input_poll_touch_tap(&kBackButton, 1) == 0) {
        mgr.pop();
    }
}

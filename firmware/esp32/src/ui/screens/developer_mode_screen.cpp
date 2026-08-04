#include "developer_mode_screen.h"
#include "uas_debug_toggle_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "force_sensor.h"
#include "turbidity.h"
#include "uas.h"
#include <stdio.h>

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void DeveloperModeScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_BLACK);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Developer Mode", 15, TFT_WHITE, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        ui_display_draw_centered("UAS debug mode >", 200, TFT_GREEN, 1);
        ui_display_draw_centered("Press knob to open", 225, TFT_DARKGREY, 1);
        ui_display_draw_touch_button(kBackButton.x, kBackButton.y, kBackButton.w, kBackButton.h,
                                      kBackButton.label, TFT_DARKGREY, TFT_WHITE);
        ui_display_draw_centered("(or hold knob)", 245, TFT_DARKGREY, 1);
    }

    // Live telemetry — refreshed every call, not just on forceFull (this is
    // the whole point of the screen). Cheap enough: a handful of fillRect +
    // text draws, same pattern as the spinner elsewhere.
    tft.fillRect(0, 55, tft.width(), 130, TFT_BLACK);
    char line[48];
    snprintf(line, sizeof(line), "Load cell 1: %.1f g", force_sensor_get_grams_1());
    ui_display_draw_centered(line, 60, TFT_DARKGREY, 1);
    snprintf(line, sizeof(line), "Load cell 2: %.1f g", force_sensor_get_grams_2());
    ui_display_draw_centered(line, 85, TFT_DARKGREY, 1);
    snprintf(line, sizeof(line), "Turbidity ALS: %u  (%s)",
             turbidity_get_als_clear(), turbidity_apds_ok() ? "ok" : "fault");
    ui_display_draw_centered(line, 110, TFT_DARKGREY, 1);
    snprintf(line, sizeof(line), "Turbidity IR: %lu  (%s)",
             (unsigned long)turbidity_get_ir_raw(), turbidity_max_ok() ? "ok" : "fault");
    ui_display_draw_centered(line, 135, TFT_DARKGREY, 1);
    snprintf(line, sizeof(line), "UAS reading: %lu mV", (unsigned long)uas_read_mv());
    ui_display_draw_centered(line, 160, TFT_DARKGREY, 1);
}

void DeveloperModeScreen::update(ScreenManager &mgr, bool forceFull) {
    _draw(forceFull);

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS && _uasDebugToggleScreen) {
        mgr.push(_uasDebugToggleScreen);
        return;
    }
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

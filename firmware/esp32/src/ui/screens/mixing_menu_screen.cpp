#include "mixing_menu_screen.h"
#include "warning_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "motors.h"
#include "config.h"
#include "ui.h"
#include "mixing_options.h"
#include <stdio.h>
#include <string.h>

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void MixingMenuScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
#ifdef BENCH_NO_HOMING
    tft.fillRect(0, 0, tft.width(), 20, COLOR_CLOWN_NOSE);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("BENCH BUILD - NO HOMING", 2, TFT_WHITE, 1);
#endif
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered(mixing_options_target_type_label(), 26, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    char agentLine[24];
    snprintf(agentLine, sizeof(agentLine), "Agent: %s", mixing_options_agent_label());
    ui_display_draw_centered(agentLine, 60, COLOR_LUNAR_ROCK, 1);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 95, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);

    bool homed = motors_is_homed();
    ui_display_draw_centered(homed ? "Press knob to continue" : "NOT HOMED - press knob to home", 225,
                              homed ? COLOR_LUNAR_ROCK : COLOR_CLOWN_NOSE, homed ? 2 : 1);

    ui_display_draw_touch_button(kBackButton.x, kBackButton.y, kBackButton.w, kBackButton.h,
                                  kBackButton.label, COLOR_LUNAR_ROCK, TFT_WHITE);
    ui_display_draw_centered("(or press BTN1)", 245, COLOR_LUNAR_ROCK, 1);
}

void MixingMenuScreen::update(ScreenManager &mgr, bool forceFull) {
    int step = ui_input_read_encoder_step();
    bool needsRedraw = forceFull;
    if (step != 0) {
        int32_t newTarget = (int32_t)scheduler_get_target_um() + step * TARGET_SIZE_UM_STEP;
        if (newTarget < TARGET_SIZE_UM_MIN) newTarget = TARGET_SIZE_UM_MIN;
        if (newTarget > TARGET_SIZE_UM_MAX) newTarget = TARGET_SIZE_UM_MAX;
        scheduler_set_target_um((uint16_t)newTarget);
        needsRedraw = true;
    }

    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS) {
        if (!motors_is_homed()) {
            // Blocking, same tradeoff already accepted for the BLE
            // console's own HOME command — nothing can be running yet.
            if (!motors_home()) {
                ui_show_error("HOMING FAILED - check limit switches");
                return;
            }
            needsRedraw = true;
        } else if (_warningScreen) {
            mgr.push(_warningScreen);
            return;
        }
    } else if (ev == ButtonEvent::LONG_PRESS && _verifyingScreen) {
        // Kept from the old set-target screen — an independent camera-based
        // size check, unrelated to the Start Menu's separate (stub) Camera
        // feature item. Not shown on the flowchart, not contradicted by it.
        mgr.push(_verifyingScreen);
        return;
    }

    int tap = ui_input_poll_touch_tap(&kBackButton, 1);
    if (tap == 0) {
        mgr.pop();
        return;
    }

    // BTN1 = Back, gated on !scheduler_is_running() — see menu_screen.cpp
    // for the full rationale. This screen can't structurally be active
    // while a run is going (a run only starts from the Warning screen
    // past this one), but the guard costs nothing and keeps the same
    // guarantee explicit here too.
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }

    if (needsRedraw) _draw();
}

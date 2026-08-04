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
#include <stdio.h>
#include <string.h>

enum class SyringePreset : uint8_t { TERUMO, NIPRO };
// Cosmetic only for now, by product decision — stored/displayed but does
// not change any calibration constant yet. If/when it does, this is where
// a real steps-per-mm or barrel-diameter lookup would plug in.
static SyringePreset _preset = SyringePreset::TERUMO;

static const TouchButton kButtons[] = {
    { 60, 175, 200, 34, "Preset" },  // [0] cycles the syringe preset
    { 20, 260, 100, 40, "Back"   },  // [1] back to Start Menu
};

static const char *_presetLabel() {
    return _preset == SyringePreset::TERUMO ? "Terumo" : "Nipro";
}

void MixingMenuScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
#ifdef BENCH_NO_HOMING
    tft.fillRect(0, 0, tft.width(), 20, TFT_RED);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("BENCH BUILD - NO HOMING", 2, TFT_WHITE, 1);
#endif
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("Mixing Menu", 26, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Size/viscosity selection", 60, TFT_DARKGREY, 1);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 95, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);

    char presetLine[32];
    snprintf(presetLine, sizeof(presetLine), "Syringe: %s", _presetLabel());
    ui_display_draw_touch_button(kButtons[0].x, kButtons[0].y, kButtons[0].w, kButtons[0].h,
                                  presetLine, TFT_DARKGREY, TFT_WHITE);

    bool homed = motors_is_homed();
    ui_display_draw_centered(homed ? "Press knob to continue" : "NOT HOMED - press knob to home", 225,
                              homed ? TFT_DARKGREY : TFT_RED, homed ? 2 : 1);

    ui_display_draw_touch_button(kButtons[1].x, kButtons[1].y, kButtons[1].w, kButtons[1].h,
                                  kButtons[1].label, TFT_DARKGREY, TFT_WHITE);
    ui_display_draw_centered("(or press BTN1)", 245, TFT_DARKGREY, 1);
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

    int tap = ui_input_poll_touch_tap(kButtons, 2);
    if (tap == 0) {
        _preset = (_preset == SyringePreset::TERUMO) ? SyringePreset::NIPRO : SyringePreset::TERUMO;
        needsRedraw = true;
    } else if (tap == 1) {
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

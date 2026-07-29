#include "set_target_screen.h"
#include "running_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "motors.h"
#include "config.h"
#include <stdio.h>

void SetTargetScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    ui_display_draw_centered("EMBO", 30, TFT_WHITE, 3);
    ui_display_draw_centered("Set target size", 90, TFT_DARKGREY, 2);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    ui_display_draw_centered(buf, 130, TFT_WHITE, 4);

    bool homed = motors_is_homed();
    ui_display_draw_centered(homed ? "Press knob to start" : "NOT HOMED", 195,
                              homed ? TFT_DARKGREY : TFT_RED, 2);
    ui_display_draw_centered("Hold knob to verify with camera", 225, TFT_DARKGREY, 1);
}

void SetTargetScreen::update(ScreenManager &mgr, bool forceFull) {
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
    if (ev == ButtonEvent::SHORT_PRESS && motors_is_homed() && _runningScreen) {
        scheduler_start();
        mgr.goTo(_runningScreen);
        return;
    } else if (ev == ButtonEvent::LONG_PRESS && _verifyingScreen) {
        mgr.push(_verifyingScreen);
        return;
    }

    // BTN1 has no function on this screen — dedicated stop button, nothing
    // running yet — but poll it anyway so a press-and-release here doesn't
    // get misread as a long-held press once a run actually starts.
    ui_input_poll_btn1();

    if (needsRedraw) _draw();
}

#include "set_target_screen.h"
#include "running_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "motors.h"
#include "config.h"
#include "ui.h"
#include <stdio.h>

void SetTargetScreen::_draw() {
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
#ifdef BENCH_NO_HOMING
    // Loud, impossible-to-miss reminder this is a bench build with homing
    // skipped — see motors.cpp's motors_home() and platformio.ini's
    // embo_bench env. This banner is the only on-hardware indication; there
    // is deliberately no way to reach this code path in a normal build.
    tft.fillRect(0, 0, tft.width(), 20, TFT_RED);
    ui_display_draw_centered("BENCH BUILD - NO HOMING", 4, TFT_WHITE, 1);
#endif
    ui_display_draw_centered("EMBO", 30, TFT_WHITE, 3);
    ui_display_draw_centered("Set target size", 90, TFT_DARKGREY, 2);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    ui_display_draw_centered(buf, 130, TFT_WHITE, 4);

    bool homed = motors_is_homed();
    ui_display_draw_centered(homed ? "Press knob to start" : "NOT HOMED - press knob to home", 195,
                              homed ? TFT_DARKGREY : TFT_RED, homed ? 2 : 1);
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
    if (ev == ButtonEvent::SHORT_PRESS) {
        if (!motors_is_homed()) {
            // Blocking, same as the BLE console's own HOME command — see
            // motors_home(). Nothing can be "running" yet at this point
            // (a run requires being homed first), so stalling this screen's
            // own redraw/input for the homing duration is the same
            // tradeoff already accepted for BLE HOME.
            if (!motors_home()) {
                ui_show_error("HOMING FAILED - check limit switches");
                return;
            }
            needsRedraw = true;  // homed now — redraw to drop the NOT HOMED banner
        } else if (_runningScreen) {
            scheduler_start();
            mgr.goTo(_runningScreen);
            return;
        }
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

#include "mixing_running_screen.h"
#include "end_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "ui.h"

// [0] Pause/Resume (label swaps with state), [1] Stop.
static const TouchButton kButtons[] = {
    { 30,  260,  90, 40, "Pause" },
    { 140, 260,  90, 40, "Stop"  },
};

void MixingRunningScreen::onEnter() {
    // Draw the Mixing screen itself FIRST, before the blocking homing call
    // below — so the operator sees this screen (spinner, buttons, "Mixing"
    // title) rather than staying on the Warning screen for the whole
    // homing wait. forceFull=true forces the full static content (title,
    // buttons) to draw, matching what a normal first-update() would do.
    _draw(true);

    if (!scheduler_start()) {
        ui_show_error("HOMING FAILED - check limit switches");
    }
}

void MixingRunningScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    bool paused = scheduler_is_paused();

    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Mixing", 70, TFT_BLACK, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        ui_display_draw_centered("Hold BTN1 for emergency stop", 220, COLOR_ASH, 1);
    }

    // The pause/resume label can change without anything else on screen
    // changing, so redraw just that button whenever paused state might
    // have changed (forceFull, or right after we act on a tap/BTN1 below).
    // Text color follows the background: COLOR_BRIGHT_BLUE (Resume) is dark
    // enough for white text, COLOR_LUNAR_ROCK (Pause) is light and needs
    // dark text instead.
    ui_display_draw_touch_button(kButtons[0].x, kButtons[0].y, kButtons[0].w, kButtons[0].h,
                                  paused ? "Resume" : "Pause", paused ? COLOR_BRIGHT_BLUE : COLOR_LUNAR_ROCK,
                                  paused ? TFT_WHITE : TFT_BLACK);
    ui_display_draw_touch_button(kButtons[1].x, kButtons[1].y, kButtons[1].w, kButtons[1].h,
                                  "Stop", COLOR_CLOWN_NOSE, TFT_WHITE);

    if (paused) {
        if (forceFull) {
            tft.fillRect(0, 130, tft.width(), 30, TFT_WHITE);
            ui_display_draw_centered("PAUSED", 140, COLOR_ASH, 1);
        }
    } else {
        ui_display_draw_spinner(160);
    }
}

void MixingRunningScreen::update(ScreenManager &mgr, bool forceFull) {
    if (!_endScreen) return;  // not wired — see ui.cpp

    // BTN1 stays exactly as it was — see this screen's header comment.
    ButtonEvent btn = ui_input_poll_btn1();
    if (btn == ButtonEvent::LONG_PRESS) {
        scheduler_emergency_stop();
        _endScreen->setResult("STOPPED (e-stop)");
        mgr.goTo(_endScreen);
        return;
    } else if (btn == ButtonEvent::SHORT_PRESS) {
        scheduler_stop();
    }

    int tap = ui_input_poll_touch_tap(kButtons, 2);
    if (tap == 0) {
        if (scheduler_is_paused()) scheduler_resume();
        else scheduler_pause();
        forceFull = true;  // reflect the new pause/resume label and PAUSED banner
    } else if (tap == 1) {
        scheduler_stop();
    }

    if (scheduler_target_reached()) {
        _endScreen->setResult(scheduler_hit_safety_cap() ? "Stopped: safety cap" : "Target size reached");
        mgr.goTo(_endScreen);
        return;
    }

    // Check BEFORE the generic !scheduler_is_running() below — a fault
    // also makes scheduler_is_running() false (motors are stopped either
    // way), but it needs the persistent fault screen, not a normal "the
    // run finished" result on the End screen.
    if (scheduler_hit_fault()) {
        ui_show_error("MIXING FAULT - stall or timeout detected, check motors");
        return;
    }

    if (!scheduler_is_running()) {
        // Graceful stop finished taking effect (scheduler_is_running() is
        // false for both IDLE and — deliberately — never for PAUSED, so
        // this only fires once a stop has actually completed, not merely
        // paused).
        _endScreen->setResult("Stopped");
        mgr.goTo(_endScreen);
        return;
    }

    _draw(forceFull);
}

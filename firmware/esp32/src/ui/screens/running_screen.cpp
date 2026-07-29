#include "running_screen.h"
#include "done_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"

void RunningScreen::_draw(bool forceFull) {
    if (forceFull) {
        LGFX &tft = ui_display_tft();
        tft.fillScreen(TFT_BLACK);
        ui_display_draw_centered("Mixing", 100, TFT_WHITE, 3);
        ui_display_draw_centered("Hold BTN1 for emergency stop", 220, TFT_DARKGREY, 1);
    }
    ui_display_draw_spinner(160);
}

void RunningScreen::update(ScreenManager &mgr, bool forceFull) {
    // BTN1 is the ONLY input this screen reads — see ui.h's rationale for
    // why it's kept to one job. Don't add encoder/enc-switch handling here;
    // a run in progress shouldn't be interruptible any other way.
    if (!_doneScreen) return;  // not wired — see ui.cpp

    ButtonEvent btn = ui_input_poll_btn1();
    if (btn == ButtonEvent::LONG_PRESS) {
        scheduler_emergency_stop();
        _doneScreen->setResult("STOPPED (e-stop)");
        mgr.goTo(_doneScreen);
        return;
    } else if (btn == ButtonEvent::SHORT_PRESS) {
        scheduler_stop();  // takes effect once the in-progress stroke finishes
    }

    if (scheduler_target_reached()) {
        _doneScreen->setResult(scheduler_hit_safety_cap() ? "Stopped: safety cap" : "Target size reached");
        mgr.goTo(_doneScreen);
        return;
    }

    if (!scheduler_is_running()) {
        // Graceful stop finished taking effect.
        _doneScreen->setResult("Stopped");
        mgr.goTo(_doneScreen);
        return;
    }

    _draw(forceFull);
}

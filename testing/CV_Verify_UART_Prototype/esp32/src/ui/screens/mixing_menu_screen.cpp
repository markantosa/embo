#include "mixing_menu_screen.h"
#include "warning_screen.h"
#include "verifying_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "scheduler.h"
#include "config.h"
#include "mixing_options.h"
#include <stdio.h>
#include <string.h>

static const TouchButton kBackButton = { 20, 260, 100, 40, "Back" };

void MixingMenuScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();

    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered(mixing_options_target_type_label(), 26, TFT_BLACK, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        char agentLine[24];
        snprintf(agentLine, sizeof(agentLine), "Agent: %s", mixing_options_agent_label());
        ui_display_draw_centered(agentLine, 60, COLOR_ASH, 1);
        ui_display_draw_touch_button(kBackButton.x, kBackButton.y, kBackButton.w, kBackButton.h,
                                      kBackButton.label, COLOR_LUNAR_ROCK, TFT_BLACK);
        ui_display_draw_centered("(or press BTN1)", 245, COLOR_ASH, 1);
    }

    // Dynamic content — redrawn every call that reaches here, not just on
    // forceFull, since the target value changes with encoder rotation.
    tft.fillRect(0, 80, tft.width(), 100, TFT_WHITE);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 95, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);

    // No homed/not-homed distinction here anymore — scheduler_start()
    // homes unconditionally every time a run actually starts, regardless
    // of anything done on this screen, so there's nothing to gate here.
    ui_display_draw_centered("Press knob to continue", 225, COLOR_ASH, 1);
}

void MixingMenuScreen::update(ScreenManager &mgr, bool forceFull) {
    bool needsRedraw = forceFull;

    int step = ui_input_read_encoder_step();
    if (step != 0) {
        int32_t newTarget = (int32_t)scheduler_get_target_um() + step * TARGET_SIZE_UM_STEP;
        if (newTarget < TARGET_SIZE_UM_MIN) newTarget = TARGET_SIZE_UM_MIN;
        if (newTarget > TARGET_SIZE_UM_MAX) newTarget = TARGET_SIZE_UM_MAX;
        scheduler_set_target_um((uint16_t)newTarget);
        needsRedraw = true;
    }

    // Every branch below that navigates or takes a real action returns
    // immediately — nothing falls through into an unrelated check further
    // down (e.g. a long homing wait finishing and then accidentally also
    // being interpreted as a BTN1-back on the same call).
    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS) {
        // No homing gate here anymore — scheduler_start() (via the
        // Warning screen -> Mixing Running screen) homes unconditionally
        // every time a run actually starts, so this just navigates.
        if (_warningScreen) {
            mgr.push(_warningScreen);
        }
        return;
    }
    if (ev == ButtonEvent::LONG_PRESS) {
        // Kept from the old set-target screen — an independent camera-based
        // size check, unrelated to the Start Menu's separate (stub) Camera
        // feature item. Not shown on the flowchart, not contradicted by it.
        if (_verifyingScreen) {
            mgr.push(_verifyingScreen);
        }
        return;
    }

    if (ui_input_poll_touch_tap(&kBackButton, 1) == 0) {
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

    if (needsRedraw) _draw(forceFull);
}

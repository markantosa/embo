#include "viscosity_menu_screen.h"
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

void ViscosityMenuScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();

    if (forceFull) {
        tft.fillScreen(TFT_WHITE);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        ui_display_draw_centered("Viscosity", 26, TFT_BLACK, 1);
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
    snprintf(buf, sizeof(buf), "%u cP", mixing_options_get_viscosity_target_cp());
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 95, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);

    // No homed/not-homed distinction here — scheduler_start() homes
    // unconditionally every time a run actually starts, same as Size
    // Selection, so there's nothing to gate here.
    ui_display_draw_centered("Press knob to continue", 225, COLOR_ASH, 1);
}

void ViscosityMenuScreen::update(ScreenManager &mgr, bool forceFull) {
    bool needsRedraw = forceFull;

    int step = ui_input_read_encoder_step();
    if (step != 0) {
        int32_t newTarget = (int32_t)mixing_options_get_viscosity_target_cp() + step * TARGET_VISCOSITY_CP_STEP;
        if (newTarget < TARGET_VISCOSITY_CP_MIN) newTarget = TARGET_VISCOSITY_CP_MIN;
        if (newTarget > TARGET_VISCOSITY_CP_MAX) newTarget = TARGET_VISCOSITY_CP_MAX;
        mixing_options_set_viscosity_target_cp((uint16_t)newTarget);
        needsRedraw = true;
    }

    // Every branch below that navigates or takes a real action returns
    // immediately — same defensive pattern as MixingMenuScreen, avoiding
    // any accidental fall-through between unrelated checks.
    ButtonEvent ev = ui_input_poll_enc_sw();
    if (ev == ButtonEvent::SHORT_PRESS) {
        // Same flow as Size Selection: goes to the Warning screen, which
        // leads to Mixing Running -> scheduler_start() (homes
        // unconditionally, then runs — see mixing_options.h for why this
        // still uses the particle-size stop condition regardless).
        if (_warningScreen) {
            mgr.push(_warningScreen);
        }
        return;
    }
    if (ev == ButtonEvent::LONG_PRESS) {
        // Kept from Size Selection — an independent camera-based size
        // check, unrelated to the Start Menu's separate (stub) Camera
        // feature item.
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
    // for the full rationale.
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }

    if (needsRedraw) _draw(forceFull);
}

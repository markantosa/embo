#include "jog_motor_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "motors.h"
#include "scheduler.h"
#include "config.h"
#include "ble_debug.h"
#include <stdio.h>

// Direction convention: forward=true drives the DIR pin HIGH, forward=false
// drives it LOW — same DIR pin state HOMING_FORWARD (config.h) uses to mean
// "anticlockwise, toward the limit switch." Since HOMING_FORWARD is
// currently `false` (LOW), jogging with forward=false ("REVERSE" on
// screen) is the SAME physical rotation as homing's approach direction;
// forward=true ("FORWARD" on screen) is the opposite (clockwise). If
// HOMING_FORWARD ever gets flipped after bench verification, the
// FORWARD/REVERSE labels below flip their real-world meaning too — update
// the on-screen text in _draw() to match if that happens, so this screen
// never silently disagrees with what homing actually does.

void JogMotorScreen::onEnter() {
    _active = false;
}

void JogMotorScreen::_startContinuous(bool forward) {
    if (_active && _lastFwd == forward) return;  // already moving that way
    motor_set_dir(_motor, forward);
    motor_enable(_motor, true);
    motor_clear_limit(_motor);
    motor_set_speed(_motor, MOTOR_JOG_HZ);  // fixed speed, config.h -- runs until _stop()
    _active = true;
    _lastFwd = forward;
    ble_log("Jog: M%u %s at %u Hz", _motor, forward ? "FORWARD" : "REVERSE", MOTOR_JOG_HZ);
}

void JogMotorScreen::_stop() {
    if (!_active) return;
    motor_set_speed(_motor, 0);
    motor_enable(_motor, false);
    _active = false;
}

void JogMotorScreen::_draw(bool forceFull) {
    LGFX &tft = ui_display_tft();
    if (forceFull) {
        tft.fillScreen(TFT_BLACK);
        tft.setFont(&fonts::FreeSansBold12pt7b);
        char title[24];
        snprintf(title, sizeof(title), "Jog Motor %u", _motor);
        ui_display_draw_centered(title, 30, TFT_WHITE, 1);
        tft.setFont(&fonts::FreeSans9pt7b);
        ui_display_draw_centered("Turn knob: CW = forward", 90, COLOR_LUNAR_ROCK, 1);
        ui_display_draw_centered("CCW = reverse (anticlockwise,", 112, COLOR_LUNAR_ROCK, 1);
        ui_display_draw_centered("same direction homing uses)", 128, COLOR_LUNAR_ROCK, 1);
        ui_display_draw_centered("Press knob to stop", 225, COLOR_LUNAR_ROCK, 1);
        ui_display_draw_centered("(hold knob or press BTN1 to exit)", 245, COLOR_LUNAR_ROCK, 1);
    }
    // Status line -- redrawn every call regardless of forceFull, so the
    // moving/idle state never lags behind reality.
    tft.fillRect(0, 150, tft.width(), 60, TFT_BLACK);
    if (_active) {
        ui_display_draw_centered(_lastFwd ? "Moving: FORWARD" : "Moving: REVERSE", 160, COLOR_BRIGHT_BLUE, 1);
    } else {
        ui_display_draw_centered("Idle", 160, COLOR_LUNAR_ROCK, 1);
    }
    char posLine[24];
    snprintf(posLine, sizeof(posLine), "Position: %ld steps", (long)motor_get_position(_motor));
    ui_display_draw_centered(posLine, 185, COLOR_LUNAR_ROCK, 1);
}

void JogMotorScreen::update(ScreenManager &mgr, bool forceFull) {
    if (scheduler_is_running()) {
        // Belt-and-braces -- no code path reaches this screen mid-run today
        // (it's only reachable from Settings, which a run can't be active
        // behind), but bail out cleanly if that ever changes.
        _stop();
        mgr.pop();
        return;
    }

    int step = ui_input_read_encoder_step();
    if (step > 0) _startContinuous(true);
    else if (step < 0) _startContinuous(false);

    // Limit check — must use the debounced version, not motor_limit_hit()
    // directly: the ISR that reacts to a limit-switch edge unconditionally
    // zeroes the step pulse the instant it fires, genuine or not (see
    // motors.h). If it turns out to have been noise, the pulse is still
    // silenced and MUST be explicitly restarted, or the motor just quietly
    // stops moving without _active ever going false.
    if (_active) {
        if (motor_limit_hit(_motor)) {
            if (motor_limit_hit_debounced(_motor)) {
                _stop();  // genuine closure
            } else {
                motor_set_speed(_motor, MOTOR_JOG_HZ);  // was noise — resume
            }
        }
    }

    ButtonEvent enc = ui_input_poll_enc_sw();
    if (enc == ButtonEvent::SHORT_PRESS) {
        _stop();  // stop in place, stay on this screen
    } else if (enc == ButtonEvent::LONG_PRESS) {
        _stop();
        mgr.pop();
        return;
    }
    if (ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        _stop();
        mgr.pop();
        return;
    }

    _draw(forceFull);
}

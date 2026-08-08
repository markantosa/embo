#include "stroke_test_screen.h"
#include "ui_screen_manager.h"
#include "ui_input.h"
#include "ui_display.h"
#include "motors.h"
#include "scheduler.h"
#include "config.h"
#include "ui.h"
#include "ble_debug.h"
#include "force_sensor.h"
#include <stdio.h>

// Set by _driveBothConcurrent()/the stroke-1 loop on failure, read by
// _runTest() for the fault screen's message — lets a stall get a specific
// diagnosis ("STALL: left motor, SG=42") instead of the generic "did not
// complete" every failure used to show, regardless of whether it was
// actually a stall or a genuine timeout.
static char _lastFailureReason[80];

void StrokeTestScreen::_draw(bool forceFull) {
    if (!forceFull) return;  // nothing here changes except via encoder rotation, handled below
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("Stroke Testing", 30, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Strokes to run", 90, COLOR_LUNAR_ROCK, 1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _strokeCount);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 120, TFT_WHITE, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Press knob to start", 225, COLOR_LUNAR_ROCK, 1);
    ui_display_draw_centered("(hold knob or press BTN1 to cancel)", 245, COLOR_LUNAR_ROCK, 1);
}

void StrokeTestScreen::update(ScreenManager &mgr, bool forceFull) {
    int step = ui_input_read_encoder_step();
    if (step != 0) {
        int newCount = _strokeCount + step;
        if (newCount < STROKE_TEST_COUNT_MIN) newCount = STROKE_TEST_COUNT_MIN;
        if (newCount > STROKE_TEST_COUNT_MAX) newCount = STROKE_TEST_COUNT_MAX;
        _strokeCount = newCount;
        forceFull = true;
    }

    ButtonEvent enc = ui_input_poll_enc_sw();
    if (enc == ButtonEvent::SHORT_PRESS) {
        _runTest(mgr);
        return;  // _runTest() itself pops on success/refusal; nothing left to draw here either way
    }
    if (enc == ButtonEvent::LONG_PRESS) {
        mgr.pop();
        return;
    }
    if (!scheduler_is_running() && ui_input_poll_btn1() == ButtonEvent::SHORT_PRESS) {
        mgr.pop();
        return;
    }

    _draw(forceFull);
}

// Drives motor 1 and motor 2 CONCURRENTLY in opposite directions — one
// toward its max limit, the other toward its min (0) — stopping each
// individually the instant IT reaches its own target rather than waiting
// for both. leftForward picks which way: true = left toward max (right
// toward min), false = left toward min (right toward max). Returns false
// on timeout (either motor).
static bool _driveBothConcurrent(bool leftForward, int stroke, int totalStrokes, const char *label) {
    ble_log("Motion > Stroke Testing: stroke %d/%d - %s (concurrent)",
            stroke, totalStrokes, label);

    motor_set_dir(1, leftForward);
    motor_enable(1, true);
    motor_set_speed(1, MOTOR_STROKE_TEST_HZ);
    motor_set_dir(2, !leftForward);
    motor_enable(2, true);
    motor_set_speed(2, MOTOR_STROKE_TEST_HZ);

    bool m1Done = false, m2Done = false;
    uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
    uint32_t lastProgressMs = millis();
    while ((!m1Done || !m2Done) && millis() < deadline) {
        // If a limit-switch flag tripped but wasn't a genuine closure
        // (electrical noise from the stepper's own step pulses — more
        // likely here than usual, since this runs at double the normal
        // jog speed and drives all the way out to the soft limits), the
        // ISR already stopped that motor's step timer unconditionally.
        // Resume it, or it just silently stalls until the 30s timeout —
        // this was the actual bug behind "concurrent leg did not
        // complete" failures. See motors_home() for the same pattern.
        if (!m1Done && motor_limit_hit(1) && !motor_limit_hit_debounced(1)) {
            motor_set_speed(1, MOTOR_STROKE_TEST_HZ);
        }
        if (!m2Done && motor_limit_hit(2) && !motor_limit_hit_debounced(2)) {
            motor_set_speed(2, MOTOR_STROKE_TEST_HZ);
        }

        // Stall check — SG_RESULT below threshold means high load (TMC
        // torque loss or a genuine mechanical jam, either way something's
        // actually wrong, not just "still moving, not there yet"). Stop
        // immediately rather than continuing to push against it for up to
        // 30 more seconds — see STROKE_TEST_STALL_SG_THRESHOLD (config.h)
        // for why that number isn't fully trustworthy yet.
        if (!m1Done && motor_sg_result(1) < STROKE_TEST_STALL_SG_THRESHOLD) {
            motor_set_speed(1, 0);
            motor_enable(1, false);
            motor_set_speed(2, 0);
            motor_enable(2, false);
            snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                     "STALL DETECTED - left motor (SG=%u)", motor_sg_result(1));
            ble_log("Motion > Stroke Testing: %s in stroke %d (%s)", _lastFailureReason, stroke, label);
            return false;
        }
        if (!m2Done && motor_sg_result(2) < STROKE_TEST_STALL_SG_THRESHOLD) {
            motor_set_speed(1, 0);
            motor_enable(1, false);
            motor_set_speed(2, 0);
            motor_enable(2, false);
            snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                     "STALL DETECTED - right motor (SG=%u)", motor_sg_result(2));
            ble_log("Motion > Stroke Testing: %s in stroke %d (%s)", _lastFailureReason, stroke, label);
            return false;
        }

        if (!m1Done) {
            int32_t p1 = motor_get_position(1);
            bool reached = leftForward ? (p1 >= MOTOR1_SOFT_LIMIT_MAX) : (p1 <= MOTOR1_SOFT_LIMIT_MIN);
            if (reached) {
                motor_set_speed(1, 0);
                motor_enable(1, false);
                m1Done = true;
                ble_log("Motion > Stroke Testing: stroke %d - left reached target (position=%ld, force1=%.2fg)",
                        stroke, (long)p1, force_sensor_get_grams_1());
            }
        }
        if (!m2Done) {
            int32_t p2 = motor_get_position(2);
            bool reached = leftForward ? (p2 <= MOTOR2_SOFT_LIMIT_MIN) : (p2 >= MOTOR2_SOFT_LIMIT_MAX);
            if (reached) {
                motor_set_speed(2, 0);
                motor_enable(2, false);
                m2Done = true;
                ble_log("Motion > Stroke Testing: stroke %d - right reached target (position=%ld, force2=%.2fg)",
                        stroke, (long)p2, force_sensor_get_grams_2());
            }
        }
        // force_sensor_update() normally runs every loop() iteration in
        // main.cpp — but loop() is entirely frozen for the duration of
        // this blocking test, so force_sensor_get_grams_1/2() would
        // otherwise return whatever they were the instant the test
        // started. Calling it here keeps readings genuinely live.
        force_sensor_update();

        // Throttled progress log — every 300ms while this leg is still
        // running, so a mid-leg anomaly (unexpected jump, one motor
        // stalling while the other keeps going, load spiking, etc.) is
        // visible in the log, not just the start/end snapshot.
        if (millis() - lastProgressMs >= 300) {
            lastProgressMs = millis();
            ble_log("Motion > Stroke Testing: stroke %d progress - left=%ld right=%ld - force1=%.2fg force2=%.2fg",
                    stroke, (long)motor_get_position(1), (long)motor_get_position(2),
                    force_sensor_get_grams_1(), force_sensor_get_grams_2());
        }
        delay(1);
    }
    // Safety: stop anything still running if the deadline hit first.
    if (!m1Done) { motor_set_speed(1, 0); motor_enable(1, false); }
    if (!m2Done) { motor_set_speed(2, 0); motor_enable(2, false); }

    if (!m1Done || !m2Done) {
        snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                 "TIMED OUT - %s (left=%ld right=%ld)",
                 label, (long)motor_get_position(1), (long)motor_get_position(2));
        ble_log("Motion > Stroke Testing: %s in stroke %d - force1=%.2fg force2=%.2fg",
                _lastFailureReason, stroke, force_sensor_get_grams_1(), force_sensor_get_grams_2());
        return false;
    }
    ble_log("Motion > Stroke Testing: stroke %d/%d - %s done (left=%ld right=%ld force1=%.2fg force2=%.2fg)",
            stroke, totalStrokes, label, (long)motor_get_position(1), (long)motor_get_position(2),
            force_sensor_get_grams_1(), force_sensor_get_grams_2());
    return true;
}

// "Left"/"right" = Motor 1/Motor 2 — a naming choice for this test only;
// nothing in firmware itself distinguishes left/right beyond this. Verify
// that actually matches which physical assembly you consider left/right
// on the bench before trusting the labels.
//
// Sequence: left alone 0->max for the very first leg only (right is
// already at its home position 0, nothing to do concurrently yet); every
// leg after that, left and right always move CONCURRENTLY in opposite
// directions, alternating who's headed to max vs who's returning to 0.
// One "stroke" = one full left round-trip (0->max->0), with right doing
// the opposite each time. Test-specific stroke concept, deliberately
// separate from motor_increment_stroke()/the real mixing scheduler's
// stroke counter (see config.h) — this never touches that.
void StrokeTestScreen::_runTest(ScreenManager &mgr) {
    if (scheduler_is_running()) {
        ble_log("Motion > Stroke Testing: refused - a run is in progress");
        return;
    }

    ble_log("Motion > Stroke Testing: homing first (%d strokes requested at %u Hz)",
            _strokeCount, MOTOR_STROKE_TEST_HZ);
    if (!motors_home()) {
        ui_show_error("HOMING FAILED - check limit switches");
        return;
    }

    for (int stroke = 1; stroke <= _strokeCount; stroke++) {
        force_sensor_update();  // fresh reading for this log line — see note above
        ble_log("=== Motion > Stroke Testing: BEGIN stroke %d/%d (left=%ld right=%ld force1=%.2fg force2=%.2fg) ===",
                stroke, _strokeCount, (long)motor_get_position(1), (long)motor_get_position(2),
                force_sensor_get_grams_1(), force_sensor_get_grams_2());

        if (stroke == 1) {
            // First leg only: right is already at 0 (its home position),
            // so there's nothing for it to do concurrently — left runs
            // alone. Checking position against MOTOR1_SOFT_LIMIT_MAX
            // directly, NOT motor_at_soft_limit() — that trips on EITHER
            // boundary, and position starts exactly at softMin (0), so it
            // would report "at limit" before the motor ever took a step.
            ble_log("Motion > Stroke Testing: stroke %d/%d - left motor clockwise to max limit (alone)",
                    stroke, _strokeCount);
            motor_set_dir(1, true);
            motor_enable(1, true);
            motor_set_speed(1, MOTOR_STROKE_TEST_HZ);

            uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
            uint32_t lastProgressMs = millis();
            bool stalled = false;
            while (motor_get_position(1) < MOTOR1_SOFT_LIMIT_MAX && millis() < deadline) {
                // Same noise check/resume as _driveBothConcurrent() below —
                // see that function's comment for why this matters here.
                if (motor_limit_hit(1) && !motor_limit_hit_debounced(1)) {
                    motor_set_speed(1, MOTOR_STROKE_TEST_HZ);
                }
                // Same stall check as _driveBothConcurrent() — see there
                // for why this matters (catches a TMC torque loss or a
                // genuine mechanical jam fast, instead of waiting the full
                // 30s timeout regardless of which one it actually is).
                if (motor_sg_result(1) < STROKE_TEST_STALL_SG_THRESHOLD) {
                    stalled = true;
                    break;
                }
                // Same reasoning as _driveBothConcurrent() — loop() (and
                // its force_sensor_update() call) is frozen for the whole
                // blocking test, so this has to be called here directly
                // to get genuinely live readings, not stale ones.
                force_sensor_update();
                if (millis() - lastProgressMs >= 300) {
                    lastProgressMs = millis();
                    ble_log("Motion > Stroke Testing: stroke %d progress - left=%ld - force1=%.2fg force2=%.2fg",
                            stroke, (long)motor_get_position(1),
                            force_sensor_get_grams_1(), force_sensor_get_grams_2());
                }
                delay(1);
            }
            motor_set_speed(1, 0);
            motor_enable(1, false);

            if (stalled) {
                snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                         "STALL DETECTED - left motor (SG=%u)", motor_sg_result(1));
                ble_log("Motion > Stroke Testing: %s in stroke %d (left alone)", _lastFailureReason, stroke);
                ui_show_error(_lastFailureReason);
                return;
            }
            if (motor_get_position(1) < MOTOR1_SOFT_LIMIT_MAX) {
                snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                         "TIMED OUT - left alone (left=%ld)", (long)motor_get_position(1));
                ble_log("Motion > Stroke Testing: %s in stroke %d - force1=%.2fg",
                        _lastFailureReason, stroke, force_sensor_get_grams_1());
                ui_show_error(_lastFailureReason);
                return;
            }
        } else {
            // Every stroke after the first: right is at max (from the
            // previous stroke's return leg), so left going forward again
            // happens CONCURRENTLY with right returning to 0 — this is
            // the case that was previously missing.
            if (!_driveBothConcurrent(true, stroke, _strokeCount, "left to max, right to 0")) {
                ui_show_error(_lastFailureReason);
                return;
            }
        }

        // Second leg of every stroke: left back to 0 while right goes to
        // max — the same every time, regardless of stroke number.
        if (!_driveBothConcurrent(false, stroke, _strokeCount, "left to 0, right to max")) {
            ui_show_error(_lastFailureReason);
            return;
        }

        force_sensor_update();
        ble_log("Motion > Stroke Testing: stroke %d/%d complete (left=%ld right=%ld force1=%.2fg force2=%.2fg)",
                stroke, _strokeCount, (long)motor_get_position(1), (long)motor_get_position(2),
                force_sensor_get_grams_1(), force_sensor_get_grams_2());
    }

    ble_log("Motion > Stroke Testing: all %d strokes complete", _strokeCount);
    mgr.pop();
}

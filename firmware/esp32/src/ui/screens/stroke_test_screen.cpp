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
#include "uas.h"
#include <stdio.h>

// Set by _driveBothConcurrent()/the stroke-1 loop on failure, read by
// _runTest() for the fault screen's message — lets a stall get a specific
// diagnosis ("STALL: left motor, SG=42") instead of the generic "did not
// complete" every failure used to show, regardless of whether it was
// actually a stall or a genuine timeout.
static char _lastFailureReason[80];

// Structured CSV data output, requested separately from the prose
// ble_log() lines above (which stay as-is for human skim-reading) —
// "ST," prefix follows the same filterable-by-grep convention as the
// "LC," load cell stream in main.cpp. uas_tx = the frequency currently
// being driven (uas_get_current_frequency_hz()), uas_rx = the measured
// return voltage (uas_read_mv()) — "tx/rx" isn't existing terminology in
// this codebase, this is the most sensible mapping onto what's actually
// available; say if something else was meant.
static void _logCsvRow() {
    Serial.printf("ST,%lu,%ld,%ld,%.2f,%.2f,%.1f,%lu\n",
                  (unsigned long)millis(),
                  (long)motor_get_position(1), (long)motor_get_position(2),
                  force_sensor_get_grams_1(), force_sensor_get_grams_2(),
                  uas_get_current_frequency_hz(), (unsigned long)uas_read_mv());
}

void StrokeTestScreen::_draw(bool forceFull) {
    if (!forceFull) return;  // nothing here changes except via encoder rotation, handled below
    LGFX &tft = ui_display_tft();
    tft.fillScreen(TFT_WHITE);
    tft.setFont(&fonts::FreeSansBold12pt7b);
    ui_display_draw_centered("Stroke Testing", 30, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Strokes to run", 90, COLOR_ASH, 1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _strokeCount);
    tft.setFont(&fonts::FreeSansBold24pt7b);
    ui_display_draw_centered(buf, 120, TFT_BLACK, 1);
    tft.setFont(&fonts::FreeSans9pt7b);
    ui_display_draw_centered("Press knob to start", 225, COLOR_ASH, 1);
    ui_display_draw_centered("(hold knob or press BTN1 to cancel)", 245, COLOR_ASH, 1);
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

    int32_t startPos1 = motor_get_position(1);
    int32_t startPos2 = motor_get_position(2);
    uint32_t lastHz1 = STROKE_MIN_HZ, lastHz2 = STROKE_MIN_HZ;

    motor_set_dir(1, leftForward);
    motor_enable(1, true);
    motor_set_speed(1, STROKE_MIN_HZ);
    motor_set_dir(2, !leftForward);
    motor_enable(2, true);
    motor_set_speed(2, STROKE_MIN_HZ);

    bool m1Done = false, m2Done = false;
    uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
    uint32_t lastProgressMs = millis();
    uint32_t lastCsvMs = millis();
    while ((!m1Done || !m2Done) && millis() < deadline) {
        // If a limit-switch flag tripped but wasn't a genuine closure
        // (electrical noise from the stepper's own step pulses — more
        // likely here than usual, since this runs at double the normal
        // jog speed and drives all the way out to the soft limits), the
        // ISR already stopped that motor's step timer unconditionally.
        // Resume it, or it just silently stalls until the 30s timeout —
        // this was the actual bug behind "concurrent leg did not
        // complete" failures. See motors_home() for the same pattern.
        // Resumes at the current ramped speed (computed below), not
        // straight back to full speed — a sudden jump would defeat the
        // point of ramping.
        bool m1NoiseResume = !m1Done && motor_limit_hit(1) && !motor_limit_hit_debounced(1);
        bool m2NoiseResume = !m2Done && motor_limit_hit(2) && !motor_limit_hit_debounced(2);

        // Stall check — SG_RESULT below threshold means high load (TMC
        // torque loss or a genuine mechanical jam, either way something's
        // actually wrong, not just "still moving, not there yet"). Stop
        // immediately rather than continuing to push against it for up to
        // 30 more seconds — see MOTOR_STALL_SG_THRESHOLD (config.h)
        // for why that number isn't fully trustworthy yet.
        //
        // Gated on MOTOR_STALL_SG_THRESHOLD > 0 (compile-time — currently
        // 0, i.e. disabled) — motor_sg_result() does a live blocking UART
        // read over the shared TMC2209 bus, not a cached value, and a
        // reported hang persisted even after throttling how often it was
        // called (MOTOR_STALL_CHECK_INTERVAL_MS below), meaning a SINGLE
        // call can apparently block for a long time under real torque
        // load — not just cumulative frequency. Since the check's result
        // is never acted on while the threshold is 0 anyway
        // (motor_sg_result() is unsigned, so "< 0" can never be true),
        // there's no reason to take that risk for zero functional
        // benefit. Once a real threshold is set, this starts actually
        // calling it again — worth watching closely for the same hang at
        // that point, since the underlying single-call blocking risk
        // hasn't been separately fixed, only avoided while unused.
#if MOTOR_STALL_SG_THRESHOLD > 0
        static uint32_t lastStallCheckMs = 0;
        if (millis() - lastStallCheckMs >= MOTOR_STALL_CHECK_INTERVAL_MS) {
            lastStallCheckMs = millis();
            if (!m1Done && motor_sg_result(1) < MOTOR_STALL_SG_THRESHOLD) {
                motor_set_speed(1, 0);
                motor_enable(1, false);
                motor_set_speed(2, 0);
                motor_enable(2, false);
                snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                         "STALL DETECTED - left motor (SG=%u)", motor_sg_result(1));
                ble_log("Motion > Stroke Testing: %s in stroke %d (%s)", _lastFailureReason, stroke, label);
                return false;
            }
            if (!m2Done && motor_sg_result(2) < MOTOR_STALL_SG_THRESHOLD) {
                motor_set_speed(1, 0);
                motor_enable(1, false);
                motor_set_speed(2, 0);
                motor_enable(2, false);
                snprintf(_lastFailureReason, sizeof(_lastFailureReason),
                         "STALL DETECTED - right motor (SG=%u)", motor_sg_result(2));
                ble_log("Motion > Stroke Testing: %s in stroke %d (%s)", _lastFailureReason, stroke, label);
                return false;
            }
        }
#endif

        if (!m1Done) {
            int32_t p1 = motor_get_position(1);
            int32_t stepsIn1 = p1 - startPos1;
            if (stepsIn1 < 0) stepsIn1 = -stepsIn1;
            int32_t stepsRemaining1 = leftForward ? (MOTOR1_SOFT_LIMIT_MAX - p1) : (p1 - MOTOR1_SOFT_LIMIT_MIN);
            uint32_t rampHz1 = motor_ramped_speed_hz(stepsIn1, stepsRemaining1, MOTOR_STROKE_TEST_HZ);
            if (m1NoiseResume ||
                (rampHz1 > lastHz1 && rampHz1 - lastHz1 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ) ||
                (rampHz1 < lastHz1 && lastHz1 - rampHz1 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ)) {
                motor_set_speed(1, rampHz1);
                lastHz1 = rampHz1;
            }
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
            int32_t stepsIn2 = p2 - startPos2;
            if (stepsIn2 < 0) stepsIn2 = -stepsIn2;
            int32_t stepsRemaining2 = leftForward ? (p2 - MOTOR2_SOFT_LIMIT_MIN) : (MOTOR2_SOFT_LIMIT_MAX - p2);
            uint32_t rampHz2 = motor_ramped_speed_hz(stepsIn2, stepsRemaining2, MOTOR_STROKE_TEST_HZ);
            if (m2NoiseResume ||
                (rampHz2 > lastHz2 && rampHz2 - lastHz2 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ) ||
                (rampHz2 < lastHz2 && lastHz2 - rampHz2 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ)) {
                motor_set_speed(2, rampHz2);
                lastHz2 = rampHz2;
            }
            bool reached = leftForward ? (p2 <= MOTOR2_SOFT_LIMIT_MIN) : (p2 >= MOTOR2_SOFT_LIMIT_MAX);
            if (reached) {
                motor_set_speed(2, 0);
                motor_enable(2, false);
                m2Done = true;
                ble_log("Motion > Stroke Testing: stroke %d - right reached target (position=%ld, force2=%.2fg)",
                        stroke, (long)p2, force_sensor_get_grams_2());
            }
        }
        // force_sensor_update()/uas_update() normally run every loop()
        // iteration in main.cpp — but loop() is entirely frozen for the
        // duration of this blocking test, so their getters would
        // otherwise return whatever they were the instant the test
        // started. Calling them here keeps readings genuinely live.
        force_sensor_update();
        uas_update();

        if (millis() - lastCsvMs >= 50) {
            lastCsvMs = millis();
            _logCsvRow();
        }

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
        // One-shot diagnostic read of the TMC2209's own internal fault
        // flags — motors are already stopped by this point, so this isn't
        // adding UART risk during active high-current stepping, unlike
        // continuous polling (see motor_sg_result()'s history above).
        if (!m1Done) motor_log_driver_status(1);
        if (!m2Done) motor_log_driver_status(2);
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

    Serial.println("ST,timestamp_ms,motor1_pos,motor2_pos,loadcell1_g,loadcell2_g,uas_tx_hz,uas_rx_mv");

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
            int32_t startPos1 = motor_get_position(1);
            uint32_t lastHz1 = STROKE_MIN_HZ;
            motor_set_dir(1, true);
            motor_enable(1, true);
            motor_set_speed(1, STROKE_MIN_HZ);

            uint32_t deadline = millis() + HOMING_TIMEOUT_MS;
            uint32_t lastProgressMs = millis();
            uint32_t lastCsvMs = millis();
#if MOTOR_STALL_SG_THRESHOLD > 0
            uint32_t lastStallCheckMs = millis();
#endif
            bool stalled = false;
            while (motor_get_position(1) < MOTOR1_SOFT_LIMIT_MAX && millis() < deadline) {
                // Same noise check/resume as _driveBothConcurrent() below —
                // resumes at the current ramped speed, not straight back
                // to full speed.
                bool noiseResume = motor_limit_hit(1) && !motor_limit_hit_debounced(1);
                // Same stall check as _driveBothConcurrent() — see there
                // for why this matters, and why it's gated on
                // MOTOR_STALL_SG_THRESHOLD > 0 (currently 0/disabled) —
                // throttling alone didn't fix a reported hang under real
                // torque load, meaning a single motor_sg_result() call can
                // apparently block for a long time, not just cumulative
                // frequency — and this check does nothing anyway while
                // disabled.
#if MOTOR_STALL_SG_THRESHOLD > 0
                if (millis() - lastStallCheckMs >= MOTOR_STALL_CHECK_INTERVAL_MS) {
                    lastStallCheckMs = millis();
                    if (motor_sg_result(1) < MOTOR_STALL_SG_THRESHOLD) {
                        stalled = true;
                        break;
                    }
                }
#endif
                int32_t p1 = motor_get_position(1);
                int32_t stepsIn1 = p1 - startPos1;
                if (stepsIn1 < 0) stepsIn1 = -stepsIn1;
                int32_t stepsRemaining1 = MOTOR1_SOFT_LIMIT_MAX - p1;
                uint32_t rampHz1 = motor_ramped_speed_hz(stepsIn1, stepsRemaining1, MOTOR_STROKE_TEST_HZ);
                if (noiseResume ||
                    (rampHz1 > lastHz1 && rampHz1 - lastHz1 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ) ||
                    (rampHz1 < lastHz1 && lastHz1 - rampHz1 >= STROKE_RAMP_UPDATE_THRESHOLD_HZ)) {
                    motor_set_speed(1, rampHz1);
                    lastHz1 = rampHz1;
                }
                // Same reasoning as _driveBothConcurrent() — loop() (and
                // its force_sensor_update()/uas_update() calls) is frozen
                // for the whole blocking test, so this has to be called
                // here directly to get genuinely live readings, not stale
                // ones.
                force_sensor_update();
                uas_update();
                if (millis() - lastCsvMs >= 50) {
                    lastCsvMs = millis();
                    _logCsvRow();
                }
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
                motor_log_driver_status(1);
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

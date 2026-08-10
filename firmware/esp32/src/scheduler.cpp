#include "scheduler.h"
#include "config.h"
#include "calibration.h"
#include "motors.h"
#include "uas.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <math.h>

// See scheduler.h for why this isn't a PID (breakage is monotonic and
// irreversible, "keep stroking or stop" is the only real control action).
//
// STOP CONDITION: a direct UAS-voltage-to-size equation
// (calib_estimate_particle_size_from_uas_voltage_um(), calibration.h) —
// replaced the old 4-sensor fusion here specifically, by product decision.
// That fusion function (calib_estimate_particle_size_um()) still exists and
// is still used for bench calibration data collection (BLE FUSION/FIT
// commands, ble_debug.cpp), just not for this actual stop decision anymore.
// The check runs EVERY scheduler_update() call — "always", per product
// decision — not just once per completed stroke, so mixing can stop as
// soon as the target is CONFIRMED rather than waiting on the current
// stroke and risking overshoot on an irreversible process. "Confirmed"
// still means debounced (UAS_SIZE_IN_SPEC_HOLD_MS, config.h) — a single
// instantaneous in-tolerance reading isn't trusted to stop on its own.
//
// MOTION PATTERN: matches Stroke Testing's mechanism
// (stroke_test_screen.cpp) — motor 1 (left) and motor 2 (right) move
// CONCURRENTLY in OPPOSITE directions, driven to actual soft-limit
// POSITIONS rather than a fixed elapsed time, alternating who's headed to
// max vs returning to min each half-stroke. scheduler_start() also
// homes unconditionally every time, regardless of any prior homing.
//
// One "stroke" (for MIXING_MAX_STROKES_SAFETY_CAP / motor_increment_stroke())
// = one full left round-trip (0->max->0), with right doing the opposite
// each time — same accounting as Stroke Testing. The very first
// half-stroke of a run is special: right is already at its home position
// (0) with nothing to do concurrently, so left runs alone for that one
// half only.
//
// This being NON-blocking (scheduler_update() is called every loop(), no
// delay()s allowed) is the real difference from Stroke Testing's
// implementation — same logic (noise-resume, stall detection, position
// targets), adapted into single-check-per-call state machine form instead
// of a blocking while loop.

enum class RunState {
    IDLE,
    STROKING,  // driving toward this half-stroke's target(s) — see _leftForward for which direction left is heading
    PAUSED,    // holding mid-half-stroke, resumable — see scheduler_pause()
    DONE,
    FAULT,     // stall or timeout detected mid-run — motors stopped, needs operator attention (see scheduler_hit_fault())
};

static RunState _state = RunState::IDLE;
static uint16_t _target_um = TARGET_SIZE_UM_DEFAULT;
static bool _stopRequested = false;   // graceful stop — finish current half-stroke, then hold

static uint32_t _strokesDone = 0;
static bool _hitSafetyCap = false;

// Continuous UAS-voltage size check state.
static float _lastMeasuredUm = 0.0f;
static uint32_t _inSpecSinceMs = 0;   // 0 = not currently in spec; else timestamp it FIRST became in-spec

// Current half-stroke bookkeeping.
static bool _leftForward = true;      // true = left heading to MAX (right heading to MIN, unless _firstStroke); false = left heading to MIN (right heading to MAX)
static bool _firstStroke = true;      // true only for the very first half-stroke of a run — right is already at 0, nothing to do concurrently yet
static bool _m1Done = false, _m2Done = false;
static uint32_t _phaseDeadlineMs = 0;

// Set by scheduler_pause(), consumed by scheduler_resume().
static RunState _pausedFromState = RunState::IDLE;
static bool _pausedM1Active = false, _pausedM2Active = false;

static void _stopMotorsHold() {
    motor_set_speed(1, 0);
    motor_set_speed(2, 0);
    motor_enable(1, false);
    motor_enable(2, false);
}

// Starts driving toward this half-stroke's target(s). leftForward=true:
// left toward MAX (right toward MIN, unless _firstStroke, where right has
// nothing to do). leftForward=false: left toward MIN, right toward MAX
// (always concurrent — only the very first half-stroke of a run has
// _firstStroke true).
static void _beginHalfStroke(bool leftForward) {
    _leftForward = leftForward;

    motor_set_dir(1, leftForward);
    motor_enable(1, true);
    motor_set_speed(1, STROKE_RUN_HZ);
    motor_clear_limit(1);
    _m1Done = false;

    if (_firstStroke) {
        _m2Done = true;  // nothing for right to do concurrently this one time
    } else {
        motor_set_dir(2, !leftForward);
        motor_enable(2, true);
        motor_set_speed(2, STROKE_RUN_HZ);
        motor_clear_limit(2);
        _m2Done = false;
    }

    _phaseDeadlineMs = millis() + HOMING_TIMEOUT_MS;  // reused as a sane "something's wrong" ceiling, same as stroke_test_screen.cpp
    _state = RunState::STROKING;
}

void scheduler_init() {
    _state = RunState::IDLE;
    _stopRequested = false;
}

bool scheduler_start() {
    // Unconditional — homes every single time a run starts, regardless of
    // any homing already done via other functions (Settings > Motion >
    // Home Motors, the Mixing Menu's own manual homing prompt, etc.).
    // Deliberate: a real mix always begins from a known, freshly-confirmed
    // reference position, not whatever homing state happened to already
    // be true from something else earlier in the session.
    ble_log("Scheduler: homing before starting (always, regardless of prior homing)");
    if (!motors_home()) {
        ble_log("Scheduler: homing FAILED — cannot start");
        return false;
    }

    if (uas_has_manual_override()) {
        // Safety net: a run must never read attenuation at a leftover bench
        // frequency instead of the calibrated production set — regardless
        // of whether the BLE console's own guard already caught this.
        uas_clear_manual_override();
        ble_log("Scheduler: cleared leftover UAS frequency override before starting");
    }

    _strokesDone = 0;
    _hitSafetyCap = false;
    _stopRequested = false;
    _lastMeasuredUm = 0.0f;
    _inSpecSinceMs = 0;
    _firstStroke = true;

    ble_log("Scheduler: run started, target=%u um (UAS voltage equation, checked continuously)", _target_um);
    _beginHalfStroke(true);
    return true;
}

void scheduler_stop() {
    if (_state == RunState::PAUSED) {
        // Nothing in progress to finish — stop right away rather than
        // waiting on a half-stroke that isn't happening.
        _state = RunState::IDLE;
        _stopRequested = false;
        ble_log("Scheduler: stopped (was paused) at %lu strokes", (unsigned long)_strokesDone);
        return;
    }
    _stopRequested = true;  // takes effect once the in-progress half-stroke's containing stroke finishes
}

void scheduler_emergency_stop() {
    _stopMotorsHold();
    _state = RunState::IDLE;
    _stopRequested = false;
    ble_log("Scheduler: EMERGENCY STOP");
}

void scheduler_pause() {
    if (_state != RunState::STROKING) return;
    _pausedFromState = _state;
    _pausedM1Active = !_m1Done;
    _pausedM2Active = !_m2Done;
    _stopMotorsHold();
    _state = RunState::PAUSED;
    ble_log("Scheduler: paused (left %s, M1 active=%d M2 active=%d)",
            _leftForward ? "->max" : "->min", _pausedM1Active, _pausedM2Active);
}

void scheduler_resume() {
    if (_state != RunState::PAUSED) return;
    // Position-based, not time-based — resuming just means re-issuing the
    // same direction/speed for whichever motor(s) hadn't yet reached their
    // target; the ISR-tracked absolute position already tells us exactly
    // where each motor is, nothing to reconstruct.
    if (_pausedM1Active) {
        motor_set_dir(1, _leftForward);
        motor_enable(1, true);
        motor_set_speed(1, STROKE_RUN_HZ);
    }
    if (_pausedM2Active) {
        motor_set_dir(2, !_leftForward);
        motor_enable(2, true);
        motor_set_speed(2, STROKE_RUN_HZ);
    }
    _phaseDeadlineMs = millis() + HOMING_TIMEOUT_MS;  // fresh timeout window from the resume point
    _inSpecSinceMs = 0;  // fresh debounce window too — don't resume already "half-confirmed" from before the pause
    _state = _pausedFromState;
    ble_log("Scheduler: resumed");
}

bool scheduler_is_paused() { return _state == RunState::PAUSED; }

bool scheduler_is_running() {
    return _state == RunState::STROKING || _state == RunState::PAUSED;
}

bool scheduler_target_reached() { return _state == RunState::DONE; }
bool scheduler_hit_safety_cap()  { return _hitSafetyCap; }
bool scheduler_hit_fault()       { return _state == RunState::FAULT; }

void scheduler_set_target_um(uint16_t target_um) {
    if (scheduler_is_running()) return;  // lock the setpoint for the duration of a run
    if (target_um < TARGET_SIZE_UM_MIN) target_um = TARGET_SIZE_UM_MIN;
    if (target_um > TARGET_SIZE_UM_MAX) target_um = TARGET_SIZE_UM_MAX;
    _target_um = target_um;
}

uint16_t scheduler_get_target_um()    { return _target_um; }
uint32_t scheduler_get_strokes_done() { return _strokesDone; }

// "numChannels" is always 0 or 1 now (no reading yet, or the one UAS
// voltage reading) — kept for API compatibility with existing callers
// (BLE FIT command, ble_debug.cpp), not because multiple channels are
// fused anymore.
float   scheduler_get_last_fused_size_um()      { return _lastMeasuredUm; }
uint8_t scheduler_get_last_fused_num_channels() { return _lastMeasuredUm > 0.0f ? 1 : 0; }

float   scheduler_get_fit_k()          { return calib_breakage_get_fit().k; }
float   scheduler_get_fit_d0_um()      { return calib_breakage_get_fit().d0Um; }
uint8_t scheduler_get_fit_num_points() { return calib_breakage_get_fit().numPoints; }

void scheduler_update() {
    switch (_state) {
    case RunState::IDLE:
    case RunState::DONE:
    case RunState::FAULT:
    case RunState::PAUSED:  // motors already held by scheduler_pause(); nothing to advance
        return;

    case RunState::STROKING: {
        // Limit-switch noise check/resume — a tripped flag might be
        // electrical noise from the stepper's own step pulses, not a
        // genuine closure; the ISR already stopped that motor's step
        // timer unconditionally either way. Same pattern as
        // stroke_test_screen.cpp/motors_home().
        if (!_m1Done && motor_limit_hit(1) && !motor_limit_hit_debounced(1)) {
            motor_set_speed(1, STROKE_RUN_HZ);
        }
        if (!_m2Done && motor_limit_hit(2) && !motor_limit_hit_debounced(2)) {
            motor_set_speed(2, STROKE_RUN_HZ);
        }

        // Stall check — SG_RESULT below threshold means high load (TMC
        // torque loss or a genuine mechanical jam). This is REAL mixing,
        // not a bench test — an undetected stall here means silently
        // failing to mix material while nothing on screen shows a
        // problem, so this stops immediately into a real fault state
        // rather than continuing or waiting for a timeout.
        if ((!_m1Done && motor_sg_result(1) < MOTOR_STALL_SG_THRESHOLD) ||
            (!_m2Done && motor_sg_result(2) < MOTOR_STALL_SG_THRESHOLD)) {
            _stopMotorsHold();
            _state = RunState::FAULT;
            ble_log("Scheduler: STALL DETECTED during mixing (M1 SG=%u M2 SG=%u) at %lu strokes — motors stopped",
                    motor_sg_result(1), motor_sg_result(2), (unsigned long)_strokesDone);
            return;
        }

        // Continuous UAS-voltage-based size check — evaluated every call,
        // independent of stroke/half-stroke boundaries, so mixing stops
        // as soon as the target is CONFIRMED (debounced by
        // UAS_SIZE_IN_SPEC_HOLD_MS) rather than waiting for the current
        // stroke to finish and risking overshoot on this irreversible
        // process. Stops the motors wherever they currently are —
        // mid-half-stroke is fine, same _stopMotorsHold() used elsewhere.
        {
            float voltageVolts = uas_read_mv() / 1000.0f;
            _lastMeasuredUm = calib_estimate_particle_size_from_uas_voltage_um(voltageVolts);
            bool inSpec = fabsf(_lastMeasuredUm - (float)_target_um) <= TARGET_TOLERANCE_UM;
            if (inSpec) {
                if (_inSpecSinceMs == 0) _inSpecSinceMs = millis();
                if (millis() - _inSpecSinceMs >= UAS_SIZE_IN_SPEC_HOLD_MS) {
                    _stopMotorsHold();
                    _hitSafetyCap = false;
                    _state = RunState::DONE;
                    ble_log("Scheduler: target confirmed by UAS voltage equation "
                            "(measured=%.1fum, target=%uum, voltage=%.3fV) at %lu strokes",
                            _lastMeasuredUm, _target_um, voltageVolts, (unsigned long)_strokesDone);
                    return;
                }
            } else {
                _inSpecSinceMs = 0;  // reset debounce on any out-of-spec reading
            }
        }

        if (!_m1Done) {
            int32_t p1 = motor_get_position(1);
            bool reached = _leftForward ? (p1 >= MOTOR1_SOFT_LIMIT_MAX) : (p1 <= MOTOR1_SOFT_LIMIT_MIN);
            if (reached) { motor_set_speed(1, 0); motor_enable(1, false); _m1Done = true; }
        }
        if (!_m2Done) {
            int32_t p2 = motor_get_position(2);
            bool reached = _leftForward ? (p2 <= MOTOR2_SOFT_LIMIT_MIN) : (p2 >= MOTOR2_SOFT_LIMIT_MAX);
            if (reached) { motor_set_speed(2, 0); motor_enable(2, false); _m2Done = true; }
        }

        if ((!_m1Done || !_m2Done) && millis() > _phaseDeadlineMs) {
            _stopMotorsHold();
            _state = RunState::FAULT;
            ble_log("Scheduler: TIMED OUT during mixing half-stroke at %lu strokes (M1 done=%d M2 done=%d) — motors stopped",
                    (unsigned long)_strokesDone, _m1Done, _m2Done);
            return;
        }

        if (!_m1Done || !_m2Done) return;  // still in progress, check again next update()

        // Half-stroke complete.
        _firstStroke = false;
        if (_leftForward) {
            // Just finished heading to max — second half of the stroke
            // (left back to min, right to max) always follows immediately.
            _beginHalfStroke(false);
            return;
        }

        // Just finished heading back to min — this completes one full
        // stroke (same accounting as Stroke Testing). The UAS voltage
        // check above already runs every call regardless, so nothing
        // size-related needs re-checking here — this is just the
        // safety-cap backstop and graceful-stop handling.
        motor_increment_stroke();
        _strokesDone++;

        if (_stopRequested) {
            _state = RunState::IDLE;
            _stopRequested = false;
            ble_log("Scheduler: stopped (graceful) at %lu strokes, last measured=%.1fum",
                    (unsigned long)_strokesDone, _lastMeasuredUm);
        } else if (_strokesDone >= MIXING_MAX_STROKES_SAFETY_CAP) {
            _hitSafetyCap = true;
            _state = RunState::DONE;
            ble_log("Scheduler: WARNING — safety cap (%d strokes) hit before UAS voltage equation "
                    "confirmed target; last measured=%.1fum. Check calibration.",
                    MIXING_MAX_STROKES_SAFETY_CAP, _lastMeasuredUm);
        } else {
            _beginHalfStroke(true);  // next stroke's first half
        }
        return;
    }
    }
}

#include "scheduler.h"
#include "config.h"
#include "calibration.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <math.h>

// See scheduler.h for the closed-loop, four-sensor-fusion design rationale.

enum class RunState {
    IDLE,
    STROKE_FORWARD, // executing one stroke's forward phase
    STROKE_RETURN,  // executing one stroke's return phase
    PAUSED,         // holding mid-stroke, resumable — see scheduler_pause()
    DONE,
};

static RunState _state = RunState::IDLE;
static uint16_t _target_um = TARGET_SIZE_UM_DEFAULT;
static bool _stopRequested = false;   // graceful stop — finish current stroke, then hold

static uint32_t _strokesDone = 0;
static uint32_t _phaseStartMs = 0;
static uint8_t _consecutiveInSpec = 0;
static bool _hitSafetyCap = false;

// Set by scheduler_pause(), consumed by scheduler_resume() — which phase to
// resume into, and how far into it we already were.
static RunState _pausedFromState = RunState::IDLE;
static uint32_t _pausedElapsedMs = 0;

static FusedSizeEstimate _lastFused{};

static void _beginStroke() {
    motor_enable(1, true);
    motor_enable(2, true);
    motor_set_dir(1, true);
    motor_set_dir(2, true);
    motor_set_speed(1, STROKE_RUN_HZ);
    motor_set_speed(2, STROKE_RUN_HZ);
    _phaseStartMs = millis();
    _state = RunState::STROKE_FORWARD;
}

static void _stopMotorsHold() {
    motor_set_speed(1, 0);
    motor_set_speed(2, 0);
    motor_enable(1, false);
    motor_enable(2, false);
}

// Reads the current live value of each of the four fusion channels and
// returns the fused size estimate — see calibration.h.
static FusedSizeEstimate _readFusedEstimate() {
    float uasAtten = 0.0f;
    uint8_t numFreq = uas_get_num_frequencies();
    for (uint8_t i = 0; i < numFreq; i++) uasAtten += uas_get_attenuation(i);
    if (numFreq > 0) uasAtten /= (float)numFreq;

    float apdsRatio = calib_turbidity_ratio_als(turbidity_get_als_clear());
    float maxRatio = calib_turbidity_ratio_backscatter_ir(turbidity_get_ir_raw());
    float forceG = (force_sensor_get_grams_1() + force_sensor_get_grams_2()) / 2.0f;

    return calib_estimate_particle_size_um(uasAtten, apdsRatio, maxRatio, forceG);
}

void scheduler_init() {
    _state = RunState::IDLE;
    _stopRequested = false;
}

void scheduler_start() {
    if (!motors_is_homed()) {
        ble_log("Scheduler: refusing to start — not homed");
        return;
    }

    if (uas_has_manual_override()) {
        // Safety net: a run must never read attenuation at a leftover bench
        // frequency instead of the calibrated production set — regardless
        // of whether the BLE console's own guard already caught this.
        uas_clear_manual_override();
        ble_log("Scheduler: cleared leftover UAS frequency override before starting");
    }

    _strokesDone = 0;
    _consecutiveInSpec = 0;
    _hitSafetyCap = false;
    _stopRequested = false;
    _lastFused = FusedSizeEstimate{};

    ble_log("Scheduler: run started, target=%u um (closed-loop, 4-sensor fusion)", _target_um);
    _beginStroke();
}

void scheduler_stop() {
    if (_state == RunState::PAUSED) {
        // Nothing in progress to finish — stop right away rather than
        // waiting on a stroke that isn't happening.
        _state = RunState::IDLE;
        _stopRequested = false;
        ble_log("Scheduler: stopped (was paused) at %lu strokes", (unsigned long)_strokesDone);
        return;
    }
    _stopRequested = true;  // takes effect once the in-progress stroke finishes
}

void scheduler_emergency_stop() {
    _stopMotorsHold();
    _state = RunState::IDLE;
    _stopRequested = false;
    ble_log("Scheduler: EMERGENCY STOP");
}

void scheduler_pause() {
    if (_state != RunState::STROKE_FORWARD && _state != RunState::STROKE_RETURN) return;
    _pausedFromState = _state;
    _pausedElapsedMs = millis() - _phaseStartMs;
    _stopMotorsHold();
    _state = RunState::PAUSED;
    ble_log("Scheduler: paused (phase=%s, %lums into it)",
            _pausedFromState == RunState::STROKE_FORWARD ? "forward" : "return",
            (unsigned long)_pausedElapsedMs);
}

void scheduler_resume() {
    if (_state != RunState::PAUSED) return;
    bool forward = (_pausedFromState == RunState::STROKE_FORWARD);
    motor_enable(1, true);
    motor_enable(2, true);
    motor_set_dir(1, forward);
    motor_set_dir(2, forward);
    motor_set_speed(1, STROKE_RUN_HZ);
    motor_set_speed(2, STROKE_RUN_HZ);
    _phaseStartMs = millis() - _pausedElapsedMs;  // preserve remaining time in this phase
    _state = _pausedFromState;
    ble_log("Scheduler: resumed");
}

bool scheduler_is_paused() { return _state == RunState::PAUSED; }

bool scheduler_is_running() {
    return _state == RunState::STROKE_FORWARD || _state == RunState::STROKE_RETURN || _state == RunState::PAUSED;
}

bool scheduler_target_reached() { return _state == RunState::DONE; }
bool scheduler_hit_safety_cap()  { return _hitSafetyCap; }

void scheduler_set_target_um(uint16_t target_um) {
    if (scheduler_is_running()) return;  // lock the setpoint for the duration of a run
    if (target_um < TARGET_SIZE_UM_MIN) target_um = TARGET_SIZE_UM_MIN;
    if (target_um > TARGET_SIZE_UM_MAX) target_um = TARGET_SIZE_UM_MAX;
    _target_um = target_um;
}

uint16_t scheduler_get_target_um()    { return _target_um; }
uint32_t scheduler_get_strokes_done() { return _strokesDone; }

float   scheduler_get_last_fused_size_um()        { return _lastFused.sizeUm; }
uint8_t scheduler_get_last_fused_num_channels()    { return _lastFused.numChannelsUsed; }

float   scheduler_get_fit_k()          { return calib_breakage_get_fit().k; }
float   scheduler_get_fit_d0_um()      { return calib_breakage_get_fit().d0Um; }
uint8_t scheduler_get_fit_num_points() { return calib_breakage_get_fit().numPoints; }

void scheduler_update() {
    switch (_state) {
    case RunState::IDLE:
    case RunState::DONE:
    case RunState::PAUSED:  // motors already held by scheduler_pause(); nothing to advance
        return;

    case RunState::STROKE_FORWARD:
        if (millis() - _phaseStartMs >= STROKE_FORWARD_MS) {
            motor_set_dir(1, false);
            motor_set_dir(2, false);
            _phaseStartMs = millis();
            _state = RunState::STROKE_RETURN;
        }
        return;

    case RunState::STROKE_RETURN:
        if (millis() - _phaseStartMs >= STROKE_RETURN_MS) {
            motor_increment_stroke();
            _strokesDone++;

            _lastFused = _readFusedEstimate();
            bool inSpec = (_lastFused.numChannelsUsed >= FUSION_MIN_CHANNELS_REQUIRED)
                          && (fabsf(_lastFused.sizeUm - (float)_target_um) <= TARGET_TOLERANCE_UM);
            _consecutiveInSpec = inSpec ? (_consecutiveInSpec + 1) : 0;

            if (_stopRequested) {
                _stopMotorsHold();
                _state = RunState::IDLE;
                _stopRequested = false;
                ble_log("Scheduler: stopped (graceful) at %lu strokes, last fused=%.1fum (n=%u)",
                        (unsigned long)_strokesDone, _lastFused.sizeUm, _lastFused.numChannelsUsed);
            } else if (_consecutiveInSpec >= FUSION_CONSECUTIVE_CHECKS_REQUIRED) {
                _stopMotorsHold();
                _hitSafetyCap = false;
                _state = RunState::DONE;
                ble_log("Scheduler: target confirmed by sensor fusion at %lu strokes "
                        "(fused=%.1fum, n=%u channels, target=%uum)",
                        (unsigned long)_strokesDone, _lastFused.sizeUm, _lastFused.numChannelsUsed, _target_um);
            } else if (_strokesDone >= MIXING_MAX_STROKES_SAFETY_CAP) {
                _stopMotorsHold();
                _hitSafetyCap = true;
                _state = RunState::DONE;
                ble_log("Scheduler: WARNING — safety cap (%d strokes) hit before sensor fusion "
                        "confirmed target; last fused=%.1fum (n=%u). Check calibration.",
                        MIXING_MAX_STROKES_SAFETY_CAP, _lastFused.sizeUm, _lastFused.numChannelsUsed);
            } else {
                _beginStroke();  // next stroke
            }
        }
        return;
    }
}

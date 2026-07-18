#include "scheduler.h"
#include "config.h"
#include "calibration.h"
#include "motors.h"
#include "rpi_uart.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <math.h>

// See scheduler.h for the batch/measure/refit design rationale.

enum class RunState {
    IDLE,
    PLANNING,       // deciding the next batch size from the latest measurement + fit
    STROKE_FORWARD, // executing one stroke's forward phase
    STROKE_RETURN,  // executing one stroke's return phase
    DONE,
};

static RunState _state = RunState::IDLE;
static uint16_t _target_um = TARGET_SIZE_UM_DEFAULT;
static bool _stopRequested = false;   // graceful stop — finish current stroke, then hold

static uint32_t _batchStrokesRemaining = 0;
static uint32_t _phaseStartMs = 0;
static int16_t _lastSeenMedian = -1;  // last rpi_get_median_um() value already fed to the fit

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

void scheduler_init() {
    _state = RunState::IDLE;
    _stopRequested = false;
}

void scheduler_start() {
    if (!motors_is_homed()) {
        ble_log("Scheduler: refusing to start — not homed");
        return;
    }
    calib_breakage_reset();
    _lastSeenMedian = -1;
    _batchStrokesRemaining = 0;
    _stopRequested = false;
    ble_log("Scheduler: run started, target=%u um", _target_um);
    _state = RunState::PLANNING;
}

void scheduler_stop() {
    _stopRequested = true;  // takes effect once the in-progress stroke finishes
}

void scheduler_emergency_stop() {
    _stopMotorsHold();
    _state = RunState::IDLE;
    _stopRequested = false;
    ble_log("Scheduler: EMERGENCY STOP");
}

bool scheduler_is_running() {
    return _state != RunState::IDLE && _state != RunState::DONE;
}

bool scheduler_target_reached() {
    return _state == RunState::DONE;
}

void scheduler_set_target_um(uint16_t target_um) {
    if (scheduler_is_running()) return;  // lock the setpoint for the duration of a run
    if (target_um < TARGET_SIZE_UM_MIN) target_um = TARGET_SIZE_UM_MIN;
    if (target_um > TARGET_SIZE_UM_MAX) target_um = TARGET_SIZE_UM_MAX;
    _target_um = target_um;
}

uint16_t scheduler_get_target_um() { return _target_um; }

float   scheduler_get_fit_k()          { return calib_breakage_get_fit().k; }
float   scheduler_get_fit_d0_um()      { return calib_breakage_get_fit().d0Um; }
uint8_t scheduler_get_fit_num_points() { return calib_breakage_get_fit().numPoints; }

void scheduler_update() {
    switch (_state) {
    case RunState::IDLE:
    case RunState::DONE:
        return;

    case RunState::PLANNING: {
        if (_stopRequested) {
            _state = RunState::IDLE;
            _stopRequested = false;
            ble_log("Scheduler: stopped (graceful)");
            return;
        }

        // Feed any new CV measurement into the fit before (re)planning.
        int16_t median = rpi_get_median_um();
        if (median >= 0 && median != _lastSeenMedian) {
            calib_breakage_add_point(motor_get_stroke_count(), (float)median);
            _lastSeenMedian = median;
        }

        uint32_t nextBatch = calib_predict_next_batch_strokes((float)_target_um, (float)TARGET_TOLERANCE_UM);
        if (nextBatch == 0 && _lastSeenMedian >= 0) {
            ble_log("Scheduler: target reached (median=%d um, target=%u um, strokes=%lu)",
                    _lastSeenMedian, _target_um, (unsigned long)motor_get_stroke_count());
            _stopMotorsHold();
            _state = RunState::DONE;
            return;
        }

        _batchStrokesRemaining = nextBatch;
        ble_log("Scheduler: batch=%lu strokes (fit k=%.4f D0=%.1f n=%u)",
                (unsigned long)_batchStrokesRemaining, scheduler_get_fit_k(),
                scheduler_get_fit_d0_um(), scheduler_get_fit_num_points());
        _beginStroke();
        return;
    }

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
            if (_batchStrokesRemaining > 0) _batchStrokesRemaining--;

            if (_stopRequested || _batchStrokesRemaining == 0) {
                _stopMotorsHold();
                _state = RunState::PLANNING;  // re-measure / re-plan (or stop, checked there)
            } else {
                _beginStroke();  // next stroke in the same batch
            }
        }
        return;
    }
}

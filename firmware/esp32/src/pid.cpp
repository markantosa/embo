#include "pid.h"
#include "config.h"
#include "rpi_uart.h"
#include "motors.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <math.h>

// Particle size follows first-order breakage: D(N) = D_min + (D0 - D_min)*exp(-k*N)
// PID output → additional strokes to schedule before next measurement.

static bool _running = false;
static bool _done    = false;
static uint16_t _target_um = TARGET_SIZE_UM_DEFAULT;

// Tuning — placeholders, calibrate empirically
static const float KP = 1.0f;
static const float KI = 0.1f;
static const float KD = 0.0f;

static float _integral  = 0.0f;
static float _prev_err  = 0.0f;
static unsigned long _last_ms = 0;

void pid_init() {
    _running = false;
    _done    = false;
    _integral = 0.0f;
    _prev_err = 0.0f;
}

void pid_start() {
    pid_init();
    _last_ms = millis();  // seed time so first dt is ~0, not millis() since boot
    _running = true;
    motor_enable(1, true);
    motor_enable(2, true);
}

void pid_stop() {
    _running = false;
    motor_enable(1, false);
    motor_enable(2, false);
}

bool pid_is_running()     { return _running; }
bool pid_target_reached() { return _done; }

void pid_set_target_um(uint16_t target_um) {
    if (_running) return;  // lock the setpoint for the duration of a run
    if (target_um < TARGET_SIZE_UM_MIN) target_um = TARGET_SIZE_UM_MIN;
    if (target_um > TARGET_SIZE_UM_MAX) target_um = TARGET_SIZE_UM_MAX;
    _target_um = target_um;
}

uint16_t pid_get_target_um() { return _target_um; }

void pid_update() {
    if (!_running) return;
    if (!motors_is_homed()) return;

    int16_t median = rpi_get_median_um();
    if (median < 0) return;   // no data yet

    float err = (float)median - (float)_target_um;

    // Check if we're in spec
    if (fabsf(err) <= TARGET_TOLERANCE_UM) {
        ble_log("PID: target reached (median=%d um, target=%u um)\n", median, _target_um);
        pid_stop();
        _done = true;
        return;
    }

    unsigned long now = millis();
    float dt = (now - _last_ms) / 1000.0f;
    _last_ms = now;
    if (dt <= 0.0f) return;

    _integral  += err * dt;
    float deriv = (err - _prev_err) / dt;
    _prev_err   = err;

    float output = KP * err + KI * _integral + KD * deriv;

    // TODO: map PID output → stroke count / motor speed
    ble_log("PID: err=%.1f out=%.1f median=%d\n", err, output, median);
    (void)output;
}

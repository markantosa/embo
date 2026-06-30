#include "pid.h"
#include "config.h"
#include "rpi_uart.h"
#include "motors.h"
#include "ble_debug.h"
#include <Arduino.h>

// Particle size follows first-order breakage: D(N) = D_min + (D0 - D_min)*exp(-k*N)
// PID output → additional strokes to schedule before next measurement.

static bool _running = false;
static bool _done    = false;

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

void pid_update() {
    if (!_running) return;
    if (!motors_is_homed()) return;

    int16_t median = rpi_get_median_um();
    if (median < 0) return;   // no data yet

    // Error: how far above the target window centre we are
    float target = (TARGET_SIZE_UM_MIN + TARGET_SIZE_UM_MAX) / 2.0f;
    float err    = (float)median - target;

    // Check if we're in spec
    if (median >= TARGET_SIZE_UM_MIN && median <= TARGET_SIZE_UM_MAX) {
        ble_log("PID: target reached (median=%d um)\n", median);
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

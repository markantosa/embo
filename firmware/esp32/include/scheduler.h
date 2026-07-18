#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mixing control loop — replaces the old PID (pid.h/pid.cpp).
//
// WHY NOT PID: the breakage process D(N) = D_min + (D0-D_min)*exp(-k*N) is
// monotonic and IRREVERSIBLE — mixing can only break particles smaller,
// never larger. Overshoot isn't "error to correct from the other side," it's
// a ruined batch, which rules out an integral term (accumulated windup would
// push MORE strokes on a process that can't undo damage already done). The
// CV measurement is also slow/quantized/noisy relative to the control
// action, which makes a derivative term a textbook noise amplifier. See the
// control-loop discussion referenced from firmware/CALIBRATION.md §5-6.
//
// INSTEAD: a receding-horizon batch/measure/refit scheduler —
//   1. Run a batch of strokes (size scaled to distance from target).
//   2. Take a CV measurement.
//   3. Refit the breakage model (k, D0) from all real data points so far
//      (calibration.h's calib_breakage_add_point / calib_breakage_get_fit).
//   4. Predict strokes-remaining from the fitted model, but command less
//      than the full predicted remainder (calibration.h's
//      SCHED_UNDERSHOOT_FRACTION) — deliberately re-measure before the
//      model could possibly authorize an overshoot.
//   5. Stop the instant a real measurement lands inside tolerance.
//
// StallGuard and UAS/turbidity are NOT scheduler inputs — per
// docs/EMBO_UAS_CV_Technical_Advisory.txt, the CV pipeline is the sole
// authoritative size signal until an empirical correlation check justifies
// promoting anything else. Force sensing (HX711) is a safety input (e-stop),
// not a scheduler input either — see calibration.h §8.

void scheduler_init();
void scheduler_update();   // call every loop()

// Start a mixing run targeting the current setpoint (see
// scheduler_set_target_um). Resets the online breakage-model fit — a fresh
// syringe/material may have a different k (CALIBRATION.md §5).
void scheduler_start();

// Graceful stop: finishes the in-progress stroke, then holds. Use for a
// normal doctor-initiated stop.
void scheduler_stop();

// Immediate stop: kills motor power mid-step, does not wait for the current
// stroke to finish. Use for the button e-stop and the automatic HX711 force
// e-stop (see force_sensor.h) — both should call this same function so
// there is exactly one "kill everything now" path, not two.
void scheduler_emergency_stop();

bool scheduler_is_running();
bool scheduler_target_reached();

// Setpoint, clamped to [TARGET_SIZE_UM_MIN, TARGET_SIZE_UM_MAX] from
// config.h. Rejected (no-op) while a run is in progress.
void     scheduler_set_target_um(uint16_t target_um);
uint16_t scheduler_get_target_um();

// Latest fitted model, for UI/BLE diagnostics — see calibration.h's
// BreakageFit for field meanings.
float    scheduler_get_fit_k();
float    scheduler_get_fit_d0_um();
uint8_t  scheduler_get_fit_num_points();

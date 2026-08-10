#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mixing control loop — replaces the old PID (pid.h/pid.cpp).
//
// STOP CONDITION: a direct UAS-voltage-to-size equation
// (calib_estimate_particle_size_from_uas_voltage_um(), calibration.h) —
// size_um = UAS_SIZE_EQ_COEFFICIENT * voltage_volts ^ UAS_SIZE_EQ_EXPONENT,
// real measured calibration. Checked EVERY scheduler_update() call while
// stroking — "always," per product decision — not just once per completed
// stroke, so mixing can stop as soon as the target is CONFIRMED (debounced
// by UAS_SIZE_IN_SPEC_HOLD_MS, config.h) rather than waiting on the current
// stroke and risking overshoot on this irreversible process. This
// REPLACED an earlier 4-sensor fusion approach (UAS attenuation + APDS9960
// + MAX30102 turbidity + load-cell force, each calibrated against a
// 9-point bench dataset and fused by median) as this scheduler's actual
// stop decision. That fusion function
// (calib_estimate_particle_size_um(), calibration.h) still exists and is
// still used — just for bench calibration data collection (BLE
// FUSION/FIT commands, ble_debug.cpp) rather than driving mixing itself.
//
// CV (the RPi camera) is deliberately NOT part of the stop condition
// either way. It's an optional, operator-triggered, single-shot
// VERIFICATION (rpi_uart.h) the doctor can run before/after a run to
// double-check the achieved size — slower and historically less
// trustworthy as a continuous signal (see
// docs/EMBO_UAS_CV_Technical_Advisory.txt), but a good independent check
// precisely because it counts real particles rather than inferring from a
// bulk proxy. A verification result also feeds the (diagnostic-only)
// breakage-model fit in calibration.h, but never the stop condition.
//
// WHY NOT PID: the breakage process is monotonic and IRREVERSIBLE — mixing
// only breaks particles smaller, never larger. Overshoot isn't "error to
// correct from the other side," it's a ruined batch. Even with a live,
// fast, continuous estimate now available, this still argues against a
// PID: the only real "control action" is "keep stroking or stop," not a
// proportional correction, and an integral term in particular risks
// pushing past a target that can't be un-passed. Instead:
//   1. Stroke continuously once a run starts — motor 1 (left) and motor 2
//      (right) move concurrently in opposite directions, driven to actual
//      soft-limit positions (not a fixed elapsed time), alternating who's
//      headed to max vs returning to min each half-stroke. Same mechanism
//      as Stroke Testing (stroke_test_screen.cpp), which this scheduler's
//      stroke pattern was deliberately made to match.
//   2. Every scheduler_update() call, compute the UAS-voltage size
//      estimate and compare to target — "always," not gated on stroke
//      completion.
//   3. Stop (wherever the motors currently are, not waiting for a stroke
//      boundary) once the estimate reads within TARGET_TOLERANCE_UM
//      (config.h) of the target continuously for UAS_SIZE_IN_SPEC_HOLD_MS
//      — debounced so a single noisy reading can't stop an irreversible
//      process early.
//   4. A hard MIXING_MAX_STROKES_SAFETY_CAP (calibration.h) stops the run
//      regardless, logging a warning, if the estimate never converges —
//      guards against a miscalibrated equation running forever. Still
//      counted per completed full stroke, unlike the continuous check
//      above — a coarse backstop doesn't need finer granularity.
//
// StallGuard is NOT part of the stop condition (never validated as a size
// proxy, see the technical advisory) — it's a separate stall/jam FAULT
// detector (scheduler_hit_fault()) with its own, unrelated purpose.

void scheduler_init();
void scheduler_update();   // call every loop()

// Start a mixing run targeting the current setpoint (see
// scheduler_set_target_um()). Resets the continuous in-spec debounce
// state. Does NOT reset the breakage-model fit (calibration.h) — that
// accumulates across runs, see calib_breakage_reset().
//
// Auto-homes first if not already homed (blocking, like every other
// homing call site in this firmware) — returns false without starting if
// that homing attempt fails, so the caller can show a fault rather than
// silently doing nothing.
bool scheduler_start();

// Graceful stop: finishes the in-progress stroke, then holds. Use for a
// normal doctor-initiated stop.
void scheduler_stop();

// Immediate stop: kills motor power mid-step, does not wait for the current
// stroke to finish. Use for the button e-stop and the automatic HX711 force
// e-stop (see force_sensor.h) — both should call this same function so
// there is exactly one "kill everything now" path, not two.
void scheduler_emergency_stop();

// Pause: holds the motors mid-stroke (like emergency stop, motor-power-wise)
// but remembers exactly where in the stroke it was, so scheduler_resume()
// can pick back up instead of the run being over — this is the mixing
// screen's "Pause" touch button, distinct from Stop/e-stop, which end the
// run. No-op if not currently running a stroke. See scheduler_is_paused().
void scheduler_pause();

// Resumes a paused run from exactly where scheduler_pause() left off
// (same phase, same remaining time in that phase). No-op if not paused.
void scheduler_resume();

bool scheduler_is_paused();

// True while actively stroking OR paused — a paused run is still "in
// progress" for the purposes of every guard that checks this (BLE bench
// commands, starting a second run, etc.), even though the motors are
// currently held.
bool scheduler_is_running();

// True once the UAS-voltage size estimate has read within tolerance of
// target continuously for UAS_SIZE_IN_SPEC_HOLD_MS (a real, sensor-
// confirmed stop) OR the safety-cap stroke count was hit (check
// scheduler_hit_safety_cap() to distinguish the two).
bool scheduler_target_reached();

// True if the run stopped because MIXING_MAX_STROKES_SAFETY_CAP was hit
// rather than the UAS voltage equation actually converging — a sign the
// calibration/equation needs attention, not a normal completion.
bool scheduler_hit_safety_cap();

// True if a stall (StallGuard) or a motor timing out mid-half-stroke
// stopped the run unexpectedly — a genuine hardware fault, not a normal
// completion or a graceful/emergency stop. Motors are already stopped by
// the time this is true; the caller (mixing_running_screen.cpp) is
// responsible for surfacing this to the operator (ui_show_error()).
bool scheduler_hit_fault();

// Setpoint, clamped to [TARGET_SIZE_UM_MIN, TARGET_SIZE_UM_MAX] from
// config.h. Rejected (no-op) while a run is in progress.
void     scheduler_set_target_um(uint16_t target_um);
uint16_t scheduler_get_target_um();

uint32_t scheduler_get_strokes_done();

// Latest UAS-voltage size estimate, and whether a reading exists yet (0 or
// 1 — kept as a "channels" count for API compatibility with existing
// callers, not because multiple channels are fused anymore) — for UI/BLE
// diagnostics.
float    scheduler_get_last_fused_size_um();
uint8_t  scheduler_get_last_fused_num_channels();

// Diagnostic-only breakage-model fit, for UI/BLE cross-check against the
// live UAS-voltage estimate — see calibration.h's BreakageFit for field
// meanings. This does NOT drive the stop condition (see header comment
// above).
float    scheduler_get_fit_k();
float    scheduler_get_fit_d0_um();
uint8_t  scheduler_get_fit_num_points();

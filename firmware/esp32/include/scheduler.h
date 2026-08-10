#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mixing control loop — replaces the old PID (pid.h/pid.cpp).
//
// CLOSED LOOP, on four live sensors — NOT on CV. UAS attenuation, APDS9960
// turbidity, MAX30102 turbidity, and load-cell force are each calibrated
// against a 9-point bench dataset of known particle sizes (see
// calibration.h's SensorCalibrationPoint / SENSOR_CAL_TABLE and
// CALIBRATION.md §5). Each live reading is inverted through its own
// channel's calibration curve into a size estimate; the channels that pass
// a monotonicity sanity check are fused (median) into one number
// (calib_estimate_particle_size_um()) that this scheduler compares against
// the operator's target every stroke.
//
// CV (the RPi camera) is deliberately NOT a fusion input. It's an optional,
// operator-triggered, single-shot VERIFICATION (rpi_uart.h) the doctor can
// run before/after a run to double-check the achieved size — slower and
// historically less trustworthy as a continuous signal (see
// docs/EMBO_UAS_CV_Technical_Advisory.txt), but a good independent check
// precisely because it counts real particles rather than inferring from a
// bulk proxy. A verification result also feeds the (diagnostic-only)
// breakage-model fit in calibration.h, but never the stop condition itself.
//
// WHY NOT PID: the breakage process is monotonic and IRREVERSIBLE — mixing
// only breaks particles smaller, never larger. Overshoot isn't "error to
// correct from the other side," it's a ruined batch. Even with a live,
// fast, continuous fused estimate now available, this still argues against
// a PID: the only real "control action" is "keep stroking or stop," not a
// proportional correction, and an integral term in particular risks
// pushing past a target that can't be un-passed. Instead:
//   1. Stroke continuously once a run starts — motor 1 (left) and motor 2
//      (right) move concurrently in opposite directions, driven to actual
//      soft-limit positions (not a fixed elapsed time), alternating who's
//      headed to max vs returning to min each half-stroke. Same mechanism
//      as Stroke Testing (stroke_test_screen.cpp), which this scheduler's
//      stroke pattern was deliberately made to match.
//   2. After every completed stroke, compute the fused size estimate.
//   3. Stop once the estimate reads within TARGET_TOLERANCE_UM (config.h)
//      of the target for FUSION_CONSECUTIVE_CHECKS_REQUIRED checks in a
//      row (calibration.h) — debounced so a single noisy reading can't
//      stop an irreversible process early.
//   4. A hard MIXING_MAX_STROKES_SAFETY_CAP (calibration.h) stops the run
//      regardless, logging a warning, if the estimate never converges —
//      guards against a miscalibrated fusion setup running forever.
//
// StallGuard is NOT a fusion input (never validated as a size proxy, see
// the technical advisory). Force sensing (HX711) is BOTH a fusion input
// AND an independent safety e-stop (calibration.h's
// calib_force_estop_tripped()) — the e-stop threshold applies regardless
// of what the fused estimate says.

void scheduler_init();
void scheduler_update();   // call every loop()

// Start a mixing run targeting the current setpoint (see
// scheduler_set_target_um()). Resets the consecutive-in-spec debounce
// counter. Does NOT reset the breakage-model fit (calibration.h) — that
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

// True once the fused sensor estimate has read within tolerance of target
// for enough consecutive checks (a real, sensor-confirmed stop) OR the
// safety-cap stroke count was hit (check scheduler_hit_safety_cap() to
// distinguish the two).
bool scheduler_target_reached();

// True if the run stopped because MIXING_MAX_STROKES_SAFETY_CAP was hit
// rather than the fused estimate actually converging — a sign the
// calibration or fusion setup needs attention, not a normal completion.
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

// Latest fused sensor estimate and how many channels it's built from — for
// UI/BLE diagnostics. Only meaningful while running (0 channels used before
// the first post-stroke check of a run).
float    scheduler_get_last_fused_size_um();
uint8_t  scheduler_get_last_fused_num_channels();

// Diagnostic-only breakage-model fit, for UI/BLE cross-check against the
// live fused estimate — see calibration.h's BreakageFit for field meanings.
// This does NOT drive the stop condition (see header comment above).
float    scheduler_get_fit_k();
float    scheduler_get_fit_d0_um();
uint8_t  scheduler_get_fit_num_points();

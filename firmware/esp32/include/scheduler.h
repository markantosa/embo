#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mixing control loop — replaces the old PID (pid.h/pid.cpp).
//
// STOP CONDITION (v0.7.0): OPEN-LOOP, by product decision — replaces the
// previous continuous UAS-voltage closed-loop check. The required stroke
// count is computed ONCE from the target size at the start of a run
// (calib_estimate_stroke_count_for_target(), calibration.h):
//   stroke_count = (target_size_um / STROKE_COUNT_EQ_COEFFICIENT) ^ (1 / STROKE_COUNT_EQ_EXPONENT) - 1
// real measured calibration, not a placeholder. The run then simply
// executes that many strokes and stops — nothing is measured and compared
// against target during the run to decide when to stop anymore. This is a
// genuine change in stopping philosophy, not just a different formula.
//
// The UAS delta-V size equation
// (calib_estimate_particle_size_from_uas_delta_v_um(), calibration.h —
// size_um = UAS_SIZE_EQ_COEFFICIENT * deltaV_volts ^ UAS_SIZE_EQ_EXPONENT)
// and the live voltage/size reading it drives are BOTH still kept running
// continuously every scheduler_update() call, and still shown live on
// MixingRunningScreen and at the end on EndScreen — but purely for
// display/diagnostics now, not control. That equation itself replaced an
// even earlier 4-sensor fusion approach (calib_estimate_particle_size_um(),
// calibration.h) as this scheduler's stop decision; that fusion function
// still exists and is still used, just for bench calibration data
// collection (BLE FUSION/FIT commands, ble_debug.cpp) rather than driving
// mixing itself — same reason it survived the switch to delta-V, and the
// same reason delta-V itself survives the switch to stroke count now: none
// of these measurement/estimation paths get deleted when they stop being
// the control input, only when they stop being useful for anything.
//
// CV (the RPi camera) is deliberately NOT part of the stop condition
// either way, and never has been. It's an optional, operator-triggered,
// single-shot VERIFICATION (rpi_uart.h) the doctor can run before/after a
// run to double-check the achieved size — slower and historically less
// trustworthy as a continuous signal (see
// docs/EMBO_UAS_CV_Technical_Advisory.txt), but a good independent check
// precisely because it counts real particles rather than inferring from a
// bulk proxy. A verification result also feeds the (diagnostic-only)
// breakage-model fit in calibration.h, but never the stop condition.
//
// WHY NOT PID: the breakage process is monotonic and IRREVERSIBLE — mixing
// only breaks particles smaller, never larger. Overshoot isn't "error to
// correct from the other side," it's a ruined batch. This is true
// regardless of which stop condition drives the loop — closed-loop
// UAS-voltage or open-loop stroke count — the only real "control action"
// was always "keep stroking or stop," never a proportional correction.
// Sequence, as of v0.7.0:
//   1. At scheduler_start(), compute the target stroke count once from
//      the target size — see the equation above.
//   2. Stroke continuously — motor 1 (left) and motor 2 (right) move
//      concurrently in opposite directions, driven to actual soft-limit
//      positions (not a fixed elapsed time), alternating who's headed to
//      max vs returning to min each half-stroke. Same mechanism as
//      Stroke Testing (stroke_test_screen.cpp), which this scheduler's
//      stroke pattern was deliberately made to match.
//   3. After each completed full stroke, check _strokesDone against the
//      target stroke count from step 1 — stop (motors already at rest at
//      a stroke boundary either way, unlike the old continuous check
//      which could stop mid-half-stroke) once reached.
//   4. A hard MIXING_MAX_STROKES_SAFETY_CAP (calibration.h) stops the run
//      regardless, logging a warning, if the target stroke count is
//      somehow never reached (shouldn't normally happen now that the
//      count is computed rather than measured, but this backstop is kept
//      as defense-in-depth) — or if the target stroke count itself
//      exceeds the cap for a small enough target size (currently true
//      below ~56um at the current cap/equation values — worth knowing,
//      not something this file silently works around).
//
// StallGuard is NOT part of the stop condition (never validated as a size
// proxy, see the technical advisory) — it's a separate stall/jam FAULT
// detector (scheduler_hit_fault()) with its own, unrelated purpose.

void scheduler_init();
void scheduler_update();   // call every loop()

// Start a mixing run targeting the current setpoint (see
// scheduler_set_target_um()). Computes the target stroke count fresh from
// that setpoint (see the file header comment) — does NOT reset the
// breakage-model fit (calibration.h), that accumulates across runs, see
// calib_breakage_reset().
//
// Homes first, UNCONDITIONALLY — every single time, regardless of any
// homing already done via another function earlier in the session
// (blocking, like every other homing call site in this firmware). Returns
// false without starting if that homing attempt fails, so the caller can
// show a fault rather than silently doing nothing.
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

// True once the computed target stroke count (see the file header
// comment) has been reached OR the safety-cap stroke count was hit first
// (check scheduler_hit_safety_cap() to distinguish the two).
bool scheduler_target_reached();

// True if the run stopped because MIXING_MAX_STROKES_SAFETY_CAP was hit
// before the target stroke count was reached — a sign the target size,
// equation, or safety cap itself needs a look, not a normal completion.
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

// The stroke count computed once at scheduler_start() from the target
// size — this is the actual stop condition as of v0.7.0, see the file
// header comment. 0 before any run has started this session.
uint32_t scheduler_get_target_stroke_count();

// Latest UAS-voltage size estimate, and whether a reading exists yet (0 or
// 1 — kept as a "channels" count for API compatibility with existing
// callers, not because multiple channels are fused anymore) — for UI/BLE
// diagnostics.
float    scheduler_get_last_fused_size_um();
uint8_t  scheduler_get_last_fused_num_channels();

// Raw voltage readings behind the equation above (volts) — for display
// (EndScreen shows the final measured value) and diagnostics.
// scheduler_get_baseline_voltage() is captured fresh at the start of each
// run (scheduler_start()); scheduler_get_last_measured_voltage() is
// whatever the continuous check last read. Both stay at their last run's
// values after a run ends, until the next scheduler_start() resets them —
// exactly what a "final reading" display needs.
float scheduler_get_last_measured_voltage();
float scheduler_get_baseline_voltage();

// Live viscosity reading (Pa*s) — Viscosity-targeted runs only, see
// scheduler.cpp's file header comment. Position-gated (only updated while
// a motor is within the force-read window), stays at its last value
// between updates. Always 0 for a Size-targeted run.
float scheduler_get_last_measured_viscosity_pa_s();

// The equation's raw input (grams, right load cell only) behind the value
// above — for display alongside it, same "show input and output"
// pairing as scheduler_get_last_measured_voltage()/
// scheduler_get_last_fused_size_um() does for Size. Always 0 for a
// Size-targeted run.
float scheduler_get_last_measured_force_grams();

// Diagnostic-only breakage-model fit, for UI/BLE cross-check against the
// live UAS-voltage estimate — see calibration.h's BreakageFit for field
// meanings. This does NOT drive the stop condition (see header comment
// above).
float    scheduler_get_fit_k();
float    scheduler_get_fit_d0_um();
uint8_t  scheduler_get_fit_num_points();

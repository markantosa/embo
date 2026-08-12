#pragma once
#include <stdint.h>
#include <stdbool.h>

// Mixing control loop — replaces the old PID (pid.h/pid.cpp).
//
// Two genuinely different stop conditions, chosen by target type
// (mixing_options_get_target_type(), cached once at scheduler_start() as
// _isViscosityRun) — Size and Viscosity runs stop for entirely different
// reasons, using different equations and different physical measurements.
//
// SIZE STOP CONDITION: CLOSED-LOOP, via the UAS delta-V size equation
// (calib_estimate_particle_size_from_uas_delta_v_um(), calibration.h):
//   size_um = UAS_SIZE_EQ_COEFFICIENT * deltaV_volts ^ UAS_SIZE_EQ_EXPONENT
//   deltaV_volts = V_current - V_baseline
// V_baseline is captured once at the start of the run (right after
// homing, before the first stroke) — real measured calibration, not a
// placeholder. Checked EVERY scheduler_update() call while stroking —
// not just once per completed stroke — so mixing can stop as soon as the
// target is CONFIRMED rather than waiting on the current stroke and
// risking overshoot on this irreversible process. "Confirmed" means
// debounced (UAS_SIZE_IN_SPEC_HOLD_MS, config.h) — a single
// instantaneous in-tolerance reading isn't trusted to stop on its own.
// This CAN stop mid-half-stroke, unlike Viscosity below.
//
// This was briefly OPEN-LOOP in v0.7.0-v0.7.5 (a stroke count computed
// once from target size, executed blindly with nothing measured during
// the run) — reverted back to this closed-loop check in v0.7.6, by
// product decision. The stroke-count equation
// (calib_estimate_stroke_count_for_target(), calibration.h) is STILL
// computed at scheduler_start() and exposed via
// scheduler_get_target_stroke_count() — but now purely as a diagnostic
// estimate, not consulted anywhere in the actual stop decision.
//
// VISCOSITY STOP CONDITION: also CLOSED-LOOP, but via a completely
// different equation, measurement, and gating — see
// calib_estimate_viscosity_pa_s() (calibration.h) and the position-gated
// force check in the STROKING case below. No stroke-count equivalent
// exists for Viscosity.
//
// The now-superseded 4-sensor fusion approach
// (calib_estimate_particle_size_um(), calibration.h) that the UAS delta-V
// equation itself replaced as Size's stop decision still exists and is
// still used, just for bench calibration data collection (BLE FUSION/FIT
// commands, ble_debug.cpp) rather than driving mixing — same reason nothing
// here gets deleted when it stops being the control input, only when it
// stops being useful for anything.
//
// CV (the RPi camera) is deliberately NOT part of either stop condition,
// and never has been. It's an optional, operator-triggered, single-shot
// VERIFICATION (rpi_uart.h) the doctor can run before/after a run to
// double-check the achieved size — slower and historically less
// trustworthy as a continuous signal (see
// docs/EMBO_UAS_CV_Technical_Advisory.txt), but a good independent check
// precisely because it counts real particles rather than inferring from a
// bulk proxy. A verification result also feeds the (diagnostic-only)
// breakage-model fit in calibration.h, but never either stop condition.
//
// WHY NOT PID: the breakage process is monotonic and IRREVERSIBLE — mixing
// only breaks particles smaller (or thins viscosity), never the reverse.
// Overshoot isn't "error to correct from the other side," it's a ruined
// batch. Even with live, fast, continuous estimates available for both
// target types, this still argues against a PID: the only real "control
// action" is "keep stroking or stop," not a proportional correction, and
// an integral term in particular risks pushing past a target that can't
// be un-passed.
//
// MOTION PATTERN (shared by both target types): matches Stroke Testing's
// mechanism (stroke_test_screen.cpp) — motor 1 (left) and motor 2 (right)
// move CONCURRENTLY in OPPOSITE directions, driven to actual soft-limit
// POSITIONS rather than a fixed elapsed time, alternating who's headed to
// max vs returning to min each half-stroke. scheduler_start() also homes
// unconditionally every time, regardless of any prior homing.
//
// One "stroke" (for MIXING_MAX_STROKES_SAFETY_CAP / motor_increment_stroke())
// = one full left round-trip (0->max->0), with right doing the opposite
// each time — same accounting as Stroke Testing. The very first
// half-stroke of a run is special: right is already at its home position
// (0) with nothing to do concurrently, so left runs alone for that one
// half only.
//
// A hard MIXING_MAX_STROKES_SAFETY_CAP (calibration.h) stops either kind
// of run regardless, logging a warning, if its stop condition never
// converges — guards against a miscalibrated equation running forever.
// Checked per completed full stroke, unlike the continuous checks above —
// a coarse backstop doesn't need finer granularity.
//
// StallGuard is NOT part of the stop condition (never validated as a size
// proxy, see the technical advisory) — it's a separate stall/jam FAULT
// detector (scheduler_hit_fault()) with its own, unrelated purpose.

void scheduler_init();
void scheduler_update();   // call every loop()

// Start a mixing run targeting the current setpoint (see
// scheduler_set_target_um()) — homes unconditionally, captures a fresh
// UAS baseline, and resets the in-spec debounce (see the file header
// comment). Does NOT reset the breakage-model fit (calibration.h) — that
// accumulates across runs, see calib_breakage_reset().
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

// True once the run's stop condition (see the file header comment — UAS
// delta-V for Size, force-based for Viscosity, both closed-loop) has
// confirmed target OR the safety-cap stroke count was hit first (check
// scheduler_hit_safety_cap() to distinguish the two).
bool scheduler_target_reached();

// True if the run stopped because MIXING_MAX_STROKES_SAFETY_CAP was hit
// before the actual stop condition ever confirmed target — a sign the
// target, equation, or safety cap itself needs a look, not a normal
// completion.
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
// size — a DIAGNOSTIC ESTIMATE only (as of v0.7.6), not the stop
// condition (that's the UAS delta-V closed-loop check again, see the
// file header comment). 0 before any run has started this session, and 0
// for a Viscosity run (no stroke-count equation exists for that target
// type).
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

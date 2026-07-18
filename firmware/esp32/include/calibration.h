#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// EMBO SENSOR-TO-PHYSICAL-UNIT CALIBRATION
// ============================================================================
// This is the ONE file to edit when new calibration data comes in — every
// constant below is a placeholder until measured on real hardware, and
// nothing else in the firmware should need to change to update a
// raw-sensor-to-physical mapping or the breakage model. Full measurement
// procedure for each value: firmware/CALIBRATION.md.
//
// If a value here and firmware/CALIBRATION.md ever disagree, THIS file is
// the one actually running — go fix the doc, not the other way round.
// ============================================================================

// ---------------------------------------------------------------------------
// Force sensing (HX711 x2) — see CALIBRATION.md §4
// ---------------------------------------------------------------------------

// Tare: raw 24-bit signed count with the plunger mechanically at rest,
// unloaded. Re-measure per channel — load cells are rarely identical.
#define HX711_TARE_1                    0
#define HX711_TARE_2                    0

// Scale: raw counts per gram, i.e. grams = (raw - tare) / scale.
// PLACEHOLDER (1.0 = no conversion) — measure against a known reference
// load at the load cell's real mounting point before trusting this.
#define HX711_SCALE_COUNTS_PER_GRAM_1    1.0f
#define HX711_SCALE_COUNTS_PER_GRAM_2    1.0f

// Automatic e-stop trigger, independent of the mixing schedule. Set with
// headroom above the peak force observed across several NORMAL mixing
// runs — must never trip in routine operation, but must catch a genuine
// jam/obstruction fast. See CALIBRATION.md §4 and §8.
#define HX711_ESTOP_GRAMS               5000.0f

float calib_hx711_to_grams(uint8_t channel, int32_t rawCount);

// True if either channel is over the e-stop threshold. Caller (motors.cpp /
// main.cpp) should route this into the same kill path as the button e-stop
// — see CALIBRATION.md §8.
bool calib_force_estop_tripped(float grams1, float grams2);

// ---------------------------------------------------------------------------
// Turbidity (APDS9960 ALS + MAX30102 backscatter) — see CALIBRATION.md §3
// ---------------------------------------------------------------------------

// 0%-turbidity ("pure saline, no particles") baselines. Re-sample whenever
// transducer/LED coupling or enclosure light-sealing changes. Must be
// re-sampled to a real non-zero value before these ratios mean anything —
// left at 1.0 here only so a divide-by-zero can't happen by accident.
#define APDS9960_BASELINE_CLEAR          1.0f
#define MAX30102_BASELINE_IR             1.0f
#define MAX30102_BASELINE_RED            1.0f

// Returns raw/baseline: 1.0 = matches the saline baseline, <1.0 or >1.0 =
// more/less transmission or backscatter than baseline depending on sensor.
// NOT yet validated as a particle-size proxy — diagnostic/trend signal only
// until an empirical CV correlation check passes, same discipline as UAS
// (see docs/EMBO_UAS_CV_Technical_Advisory.txt and CALIBRATION.md §3).
float calib_turbidity_ratio_als(uint16_t alsClearRaw);
float calib_turbidity_ratio_backscatter_ir(uint32_t irRaw);
float calib_turbidity_ratio_backscatter_red(uint32_t redRaw);

// ---------------------------------------------------------------------------
// Breakage kinetics: D(N) = D_min + (D0 - D_min) * exp(-k * N)
// See CALIBRATION.md §5. This is the model the mixing scheduler (§6) uses
// instead of a PID error term — see docs/EMBO_UAS_CV_Technical_Advisory.txt
// and the scheduler's own header for why.
// ---------------------------------------------------------------------------

// Fallback values used only until calib_breakage_get_fit() has at least 2
// real (stroke, measured_um) data points of its own from the current run.
#define BREAKAGE_K_DEFAULT               0.01f    // per stroke, PLACEHOLDER
#define BREAKAGE_D_MIN_UM                50.0f     // PLACEHOLDER
#define BREAKAGE_D0_UM_DEFAULT           1000.0f   // PLACEHOLDER — initial/unmixed size

// Predicts particle size at a given stroke count under the given model
// parameters. Pure function — does not touch the online fit below.
float calib_predict_size_um(uint32_t strokeCount, float k, float d0Um, float dMinUm);

struct BreakageFit {
    float k;
    float d0Um;
    uint8_t numPoints;  // how many real data points the current fit is based on
};

// Resets the online fit — call at the start of every new mixing run (a
// fresh syringe/material may have a different k, see CALIBRATION.md §5).
void calib_breakage_reset();

// Feed it every (strokeCount, measured_um) pair as CV packets arrive.
// Maintains a rolling linear-regression fit of ln(D - D_min) vs N — the
// linearized form of the exponential model — over the last few points.
// Points where measuredUm <= BREAKAGE_D_MIN_UM are silently rejected (would
// require a log of a non-positive number; also physically implausible).
void calib_breakage_add_point(uint32_t strokeCount, float measuredUm);

BreakageFit calib_breakage_get_fit();

// ---------------------------------------------------------------------------
// Mixing scheduler tuning — see CALIBRATION.md §6
// ---------------------------------------------------------------------------

// Deliberately commands fewer strokes than the model predicts are needed,
// so a fresh CV measurement always happens before the model could possibly
// overshoot the (irreversible) target. Tighten toward 1.0 only once the
// fit's run-to-run variance is well characterized.
#define SCHED_UNDERSHOOT_FRACTION        0.75f

#define SCHED_MIN_BATCH_STROKES          5
#define SCHED_MAX_BATCH_STROKES          200

// How long to trust a CV reading before treating it as "no data" and
// holding position rather than scheduling blind.
#define SCHED_STALE_MEASUREMENT_MS       10000

// Predicts how many strokes to run in the NEXT batch, given the current
// fitted model (or the fallback constants above if fewer than 2 points are
// in yet), the most recent (strokeCount, measured_um) data point on file,
// and targetUm/toleranceUm. Already reduced by SCHED_UNDERSHOOT_FRACTION
// and clamped to [SCHED_MIN_BATCH_STROKES, SCHED_MAX_BATCH_STROKES].
// Returns 0 if the most recent measurement is already within tolerance.
uint32_t calib_predict_next_batch_strokes(float targetUm, float toleranceUm);

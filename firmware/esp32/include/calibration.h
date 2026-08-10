#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// EMBO SENSOR-TO-PHYSICAL-UNIT CALIBRATION
// ============================================================================
// This is the ONE file to edit when new calibration data comes in — every
// constant below is a placeholder until measured on real hardware, and
// nothing else in the firmware should need to change to update a
// raw-sensor-to-physical mapping or the fused size estimate. Full
// measurement procedure for each value: firmware/CALIBRATION.md.
//
// If a value here and firmware/CALIBRATION.md ever disagree, THIS file is
// the one actually running — go fix the doc, not the other way round.
//
// The 9-point sensor-fusion calibration table itself (§ below) lives in
// calibration.cpp, not here, since it's a data table rather than a scalar
// constant — see that file's SENSOR_CAL_TABLE.
// ============================================================================

// ---------------------------------------------------------------------------
// Force sensing (HX711 x2) — see CALIBRATION.md §4
// ---------------------------------------------------------------------------

// Tare: raw 24-bit signed count with the plunger mechanically at rest,
// unloaded. These two are now just the STARTUP FALLBACK, used only if
// force_sensor_tare() (force_sensor.h) can't complete in time — the real
// tare is measured fresh every boot instead of relying on a stale
// bench-measured constant, since load cells drift and mounting varies
// run to run. See calib_hx711_set_tare().
#define HX711_TARE_1                    0
#define HX711_TARE_2                    0

// Scale: raw counts per gram, i.e. grams = (raw - tare) / scale.
// Measured 2-point calibration (0g and 285.2g reference weight):
//   Ch1: raw=38000 @ 0g, raw=-77500 @ 285.2g -> (-77500-38000)/285.2 = -404.979
//   Ch2: raw=-129500 @ 0g, raw=-244000 @ 285.2g -> (-244000-(-129500))/285.2 = -401.473
// Negative is correct, not a sign error — these channels' raw count
// decreases as weight increases (wiring/polarity), and the formula above
// handles a negative scale correctly either way.
#define HX711_SCALE_COUNTS_PER_GRAM_1    -404.979f
#define HX711_SCALE_COUNTS_PER_GRAM_2    -401.473f

// Automatic e-stop trigger, independent of the mixing schedule. Set with
// headroom above the peak force observed across several NORMAL mixing
// runs — must never trip in routine operation, but must catch a genuine
// jam/obstruction fast. See CALIBRATION.md §4 and §8. This is a SAFETY
// threshold, separate from force's role as one of the four fusion inputs
// below — it applies regardless of what the fused size estimate says.
#define HX711_ESTOP_GRAMS               5000.0f

float calib_hx711_to_grams(uint8_t channel, int32_t rawCount);
// Overrides the tare point used by calib_hx711_to_grams() for one channel
// (0 or 1) at runtime — called once per boot by force_sensor_tare(), not
// meant to be called mid-run. Until called, HX711_TARE_1/2 above are used.
void calib_hx711_set_tare(uint8_t channel, int32_t tareCount);

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
// These ratios are two of the four sensor-fusion inputs below (§ Sensor
// fusion) — see CALIBRATION.md §3 for the baseline re-sampling procedure.
float calib_turbidity_ratio_als(uint16_t alsClearRaw);
float calib_turbidity_ratio_backscatter_ir(uint32_t irRaw);
float calib_turbidity_ratio_backscatter_red(uint32_t redRaw);

// ---------------------------------------------------------------------------
// Sensor fusion — closed-loop particle-size estimate driving the mixing
// stop condition. See CALIBRATION.md §5 for the full bench procedure.
// ---------------------------------------------------------------------------
// Four independent live sensor channels — UAS attenuation, APDS9960
// turbidity, MAX30102 turbidity, load-cell force — are each calibrated
// against a bench dataset of known particle sizes (nominally 9 syringes of
// slurry at set sizes spanning the target range). Each channel's own
// 9-point (sensor reading -> known size) curve is used to invert a LIVE
// reading into a size estimate; the four per-channel estimates are then
// fused (median of whichever channels pass a monotonicity sanity check —
// see calib_estimate_particle_size_um()) into one number the scheduler
// compares against the operator's target.
//
// CV (RPi camera) is deliberately NOT one of these four channels — it's an
// optional, on-demand, single-shot VERIFICATION the operator can trigger
// separately (see rpi_uart.h), not a live fusion input. It's slower and
// historically less trustworthy for a continuous reading (see
// docs/EMBO_UAS_CV_Technical_Advisory.txt) than these four sensors — but
// unlike a rushed fusion, it directly counts real particles, which is
// exactly what makes it a good independent double-check.

struct SensorCalibrationPoint {
    float knownSizeUm;      // ground truth for this bench syringe
    float uasAttenuation;   // e.g. mean attenuation across the swept frequencies
    float turbApdsRatio;    // calib_turbidity_ratio_als() at this size
    float turbMaxRatio;     // calib_turbidity_ratio_backscatter_ir() at this size
    float forceGrams;       // mean of both HX711 channels at this size
};

// The bench dataset itself — see calibration.cpp's SENSOR_CAL_TABLE. This
// is a placeholder (all zeros) until the 9-syringe bench session runs; see
// CALIBRATION.md §5 and ble_debug.cpp's `FUSION` command, which prints the
// exact live values to copy into each row.
#define SENSOR_CAL_NUM_POINTS 9
extern const SensorCalibrationPoint SENSOR_CAL_TABLE[SENSOR_CAL_NUM_POINTS];

struct FusedSizeEstimate {
    float sizeUm;             // fused estimate — meaningless if numChannelsUsed == 0
    uint8_t numChannelsUsed;  // how many of the 4 channels passed their sanity check and were used
    bool uasTrusted;
    bool turbApdsTrusted;
    bool turbMaxTrusted;
    bool forceTrusted;
};

// Takes the CURRENT live reading from each channel (caller reads these from
// uas.h/turbidity.h/force_sensor.h — this function stays hardware-agnostic,
// consistent with the rest of this file) and returns a fused size estimate.
// Each channel is independently checked for monotonicity against
// SENSOR_CAL_TABLE (see CALIBRATION.md §5) — a channel whose calibration
// data isn't monotonic is excluded rather than trusted, same discipline the
// project has held UAS/turbidity to since the very first advisory. The
// fused value is the median of whichever channels pass.
FusedSizeEstimate calib_estimate_particle_size_um(float uasAttenuation, float turbApdsRatio,
                                                   float turbMaxRatio, float forceGrams);

// ── Direct UAS-voltage-to-size equation ──────────────────────────────────────
// Replaces the 4-sensor fusion above as the MIXING LOOP's actual stop
// condition (scheduler.cpp) — the function above stays available for bench
// calibration data collection (BLE FUSION/FIT commands, ble_debug.cpp),
// unrelated to this. Real measured calibration, not a placeholder:
//   size_um = UAS_SIZE_EQ_COEFFICIENT * voltage_volts ^ UAS_SIZE_EQ_EXPONENT
// voltage is in VOLTS — uas_read_mv() (uas.h) returns millivolts, divide by
// 1000 before calling this.
#define UAS_SIZE_EQ_COEFFICIENT   8476.5f
#define UAS_SIZE_EQ_EXPONENT      -2.75f

float calib_estimate_particle_size_from_uas_voltage_um(float voltageVolts);

// ---------------------------------------------------------------------------
// Mixing stop condition — debounce, see CALIBRATION.md §6
// ---------------------------------------------------------------------------

// The fused estimate must read within TARGET_TOLERANCE_UM (config.h) of the
// target for this many CONSECUTIVE post-stroke checks before the scheduler
// actually stops — protects against a single noisy reading stopping early
// on an irreversible process. See scheduler.h.
#define FUSION_CONSECUTIVE_CHECKS_REQUIRED   3

// Refuses to trust a fused estimate built from fewer than this many
// channels — e.g. if only one sensor currently passes its sanity check,
// that's too thin a basis to stop an irreversible process on.
#define FUSION_MIN_CHANNELS_REQUIRED         2

// Safety cap: stops the run after this many strokes regardless of what the
// fused estimate says, logging a warning — guards against a miscalibrated
// or degenerate fusion setup running forever. Not a calibration value in
// itself; a sanity backstop. See CALIBRATION.md §6.
#define MIXING_MAX_STROKES_SAFETY_CAP        2000

// ---------------------------------------------------------------------------
// Breakage kinetics: D(N) = D_min + (D0 - D_min) * exp(-k * N)
// DIAGNOSTIC ONLY as of the sensor-fusion redesign — see CALIBRATION.md §7.
// This does NOT drive the stop condition any more (calib_estimate_particle_
// size_um() above does). It's kept because it's still a useful independent
// cross-check ("the model expects roughly N more strokes; the live sensors
// say we're already there — do they agree?") and because operator-triggered
// CV verifications (rpi_uart.h) still feed it, same as before.
// ---------------------------------------------------------------------------

// Fallback values used until calib_breakage_get_fit() has at least 2 real
// (stroke, measured_um) data points of its own — which accumulate only
// when an operator explicitly runs a camera verification (any number of
// times, across any number of runs; see calib_breakage_add_point below).
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

// Resets the online fit. NOT called automatically at the start of every run
// (verification data accumulates across runs by default) — call this
// manually (e.g. the BLE `FIT RESET` command, ble_debug.cpp) when the
// material genuinely changes, per CALIBRATION.md §7.
void calib_breakage_reset();

// Feed it a (strokeCount, measured_um) pair whenever an operator-triggered
// camera verification returns a result (see rpi_uart.h). Maintains a
// rolling linear-regression fit of ln(D - D_min) vs N — the linearized form
// of the exponential model — over the last few points. Points where
// measuredUm <= BREAKAGE_D_MIN_UM are silently rejected (would require a
// log of a non-positive number; also physically implausible).
void calib_breakage_add_point(uint32_t strokeCount, float measuredUm);

BreakageFit calib_breakage_get_fit();

// Diagnostic-only prediction of total strokes (from a fresh D0) to reach
// targetUm under the current fit — for comparison against the live fused
// estimate, NOT itself a stop condition. See CALIBRATION.md §7.
uint32_t calib_predict_total_strokes(float targetUm);

#include "calibration.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Force sensing
// ---------------------------------------------------------------------------

float calib_hx711_to_grams(uint8_t channel, int32_t rawCount) {
    int32_t tare = (channel == 0) ? HX711_TARE_1 : HX711_TARE_2;
    float scale = (channel == 0) ? HX711_SCALE_COUNTS_PER_GRAM_1 : HX711_SCALE_COUNTS_PER_GRAM_2;
    if (scale == 0.0f) return 0.0f;
    return (float)(rawCount - tare) / scale;
}

bool calib_force_estop_tripped(float grams1, float grams2) {
    return fabsf(grams1) > HX711_ESTOP_GRAMS || fabsf(grams2) > HX711_ESTOP_GRAMS;
}

// ---------------------------------------------------------------------------
// Turbidity
// ---------------------------------------------------------------------------

float calib_turbidity_ratio_als(uint16_t alsClearRaw) {
    if (APDS9960_BASELINE_CLEAR == 0.0f) return 0.0f;
    return (float)alsClearRaw / APDS9960_BASELINE_CLEAR;
}

float calib_turbidity_ratio_backscatter_ir(uint32_t irRaw) {
    if (MAX30102_BASELINE_IR == 0.0f) return 0.0f;
    return (float)irRaw / MAX30102_BASELINE_IR;
}

float calib_turbidity_ratio_backscatter_red(uint32_t redRaw) {
    if (MAX30102_BASELINE_RED == 0.0f) return 0.0f;
    return (float)redRaw / MAX30102_BASELINE_RED;
}

// ---------------------------------------------------------------------------
// Breakage kinetics — online linear-regression fit of the linearized model
// ---------------------------------------------------------------------------
// D(N) = D_min + (D0 - D_min)*exp(-k*N)
//   =>  ln(D - D_min) = ln(D0 - D_min) - k*N
// which is linear in N with slope -k and intercept ln(D0 - D_min). We keep
// a small rolling window of real (N, D) points and re-fit by ordinary
// least squares every time a point is added — cheap enough to do on every
// CV packet given how infrequently those arrive relative to the CPU budget.

static constexpr uint8_t BREAKAGE_MAX_POINTS = 16;
static float _ptN[BREAKAGE_MAX_POINTS];
static float _ptLnD[BREAKAGE_MAX_POINTS];
static uint8_t _numPoints = 0;
static uint8_t _nextSlot = 0;

static uint32_t _lastStrokeCount = 0;
static float _lastMeasuredUm = BREAKAGE_D0_UM_DEFAULT;
static bool _haveMeasurement = false;

void calib_breakage_reset() {
    _numPoints = 0;
    _nextSlot = 0;
    _lastStrokeCount = 0;
    _lastMeasuredUm = BREAKAGE_D0_UM_DEFAULT;
    _haveMeasurement = false;
}

void calib_breakage_add_point(uint32_t strokeCount, float measuredUm) {
    if (measuredUm <= BREAKAGE_D_MIN_UM) return;  // would need log of a non-positive number

    _ptN[_nextSlot] = (float)strokeCount;
    _ptLnD[_nextSlot] = logf(measuredUm - BREAKAGE_D_MIN_UM);
    _nextSlot = (_nextSlot + 1) % BREAKAGE_MAX_POINTS;
    if (_numPoints < BREAKAGE_MAX_POINTS) _numPoints++;

    _lastStrokeCount = strokeCount;
    _lastMeasuredUm = measuredUm;
    _haveMeasurement = true;
}

BreakageFit calib_breakage_get_fit() {
    BreakageFit fit{BREAKAGE_K_DEFAULT, BREAKAGE_D0_UM_DEFAULT, _numPoints};
    if (_numPoints < 2) return fit;  // not enough data — caller uses the fallback constants

    // Ordinary least squares: y = a + b*x, here y=ln(D-Dmin), x=N, b=-k.
    float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
    for (uint8_t i = 0; i < _numPoints; i++) {
        sumX += _ptN[i];
        sumY += _ptLnD[i];
        sumXY += _ptN[i] * _ptLnD[i];
        sumXX += _ptN[i] * _ptN[i];
    }
    float n = (float)_numPoints;
    float denom = n * sumXX - sumX * sumX;
    if (fabsf(denom) < 1e-6f) return fit;  // degenerate (e.g. all points at the same N)

    float b = (n * sumXY - sumX * sumY) / denom;      // slope = -k
    float a = (sumY - b * sumX) / n;                  // intercept = ln(D0 - Dmin)

    float k = -b;
    if (k <= 0.0f) return fit;  // non-decreasing fit isn't physical — fall back rather than trust it

    fit.k = k;
    fit.d0Um = BREAKAGE_D_MIN_UM + expf(a);
    fit.numPoints = _numPoints;
    return fit;
}

float calib_predict_size_um(uint32_t strokeCount, float k, float d0Um, float dMinUm) {
    return dMinUm + (d0Um - dMinUm) * expf(-k * (float)strokeCount);
}

uint32_t calib_predict_next_batch_strokes(float targetUm, float toleranceUm) {
    if (!_haveMeasurement) {
        // No data yet this run — command a conservative first batch and
        // let the first real measurement drive the fit from here.
        return SCHED_MIN_BATCH_STROKES;
    }

    if (fabsf(_lastMeasuredUm - targetUm) <= toleranceUm) return 0;  // already in spec

    BreakageFit fit = calib_breakage_get_fit();

    // Invert the model for the absolute stroke count at which size ==
    // targetUm, using the fit's own D0 as the model's origin.
    float dMin = BREAKAGE_D_MIN_UM;
    float numerator = fit.d0Um - dMin;
    float denominator = targetUm - dMin;
    if (numerator <= 0.0f || denominator <= 0.0f) {
        // Target is at/below D_min, or the fit is nonsensical — refuse to
        // extrapolate into a regime the model can't represent.
        return SCHED_MIN_BATCH_STROKES;
    }

    float nTargetAbs = logf(numerator / denominator) / fit.k;
    float remaining = nTargetAbs - (float)_lastStrokeCount;
    if (remaining <= 0.0f) return SCHED_MIN_BATCH_STROKES;  // model thinks we're past target but measurement disagrees — take a small step and re-measure

    uint32_t batch = (uint32_t)(remaining * SCHED_UNDERSHOOT_FRACTION);
    if (batch < SCHED_MIN_BATCH_STROKES) batch = SCHED_MIN_BATCH_STROKES;
    if (batch > SCHED_MAX_BATCH_STROKES) batch = SCHED_MAX_BATCH_STROKES;
    return batch;
}

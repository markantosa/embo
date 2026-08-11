#include "calibration.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Force sensing
// ---------------------------------------------------------------------------

static int32_t _tare1 = HX711_TARE_1;
static int32_t _tare2 = HX711_TARE_2;

void calib_hx711_set_tare(uint8_t channel, int32_t tareCount) {
    if (channel == 0) _tare1 = tareCount;
    else _tare2 = tareCount;
}

float calib_hx711_to_grams(uint8_t channel, int32_t rawCount) {
    int32_t tare = (channel == 0) ? _tare1 : _tare2;
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
// Sensor fusion — 9-point bench calibration table + live estimate
// ---------------------------------------------------------------------------
// PLACEHOLDER — all zeros. Fill in one row per bench syringe: set knownSizeUm
// to that syringe's actual particle size, load/mix it, then read the live
// values via the BLE `FUSION` command (ble_debug.cpp) and copy them in.
// A channel whose column is all the same value (e.g. all zero) is correctly
// treated as untrustworthy by the monotonicity check below, so an
// unfilled-in table safely disables fusion entirely rather than producing a
// silently-wrong estimate — see CALIBRATION.md §5.
const SensorCalibrationPoint SENSOR_CAL_TABLE[SENSOR_CAL_NUM_POINTS] = {
    // knownSizeUm, uasAttenuation, turbApdsRatio, turbMaxRatio, forceGrams
    {   50.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  150.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  250.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  350.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  450.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  550.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  650.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    {  800.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 1000.0f, 0.0f, 0.0f, 0.0f, 0.0f },
};

static constexpr float FUSION_MONOTONIC_EPS = 1e-6f;

// Sorts a local copy of (size, value) pairs by SIZE ascending, then checks
// whether value moves in one consistent direction as size increases
// (allowing exactly one flat/noisy step out of 8, but no direction
// reversals) — see CALIBRATION.md §5 for why this is the trust gate.
static bool _isMonotonicVsSize(const float sizesIn[SENSOR_CAL_NUM_POINTS],
                               const float valuesIn[SENSOR_CAL_NUM_POINTS]) {
    const int n = SENSOR_CAL_NUM_POINTS;
    float size[SENSOR_CAL_NUM_POINTS], val[SENSOR_CAL_NUM_POINTS];
    for (int i = 0; i < n; i++) { size[i] = sizesIn[i]; val[i] = valuesIn[i]; }

    for (int i = 1; i < n; i++) {  // insertion sort by size ascending
        float s = size[i], v = val[i];
        int j = i - 1;
        while (j >= 0 && size[j] > s) { size[j + 1] = size[j]; val[j + 1] = val[j]; j--; }
        size[j + 1] = s; val[j + 1] = v;
    }

    int inc = 0, dec = 0;
    for (int i = 1; i < n; i++) {
        float d = val[i] - val[i - 1];
        if (d > FUSION_MONOTONIC_EPS) inc++;
        else if (d < -FUSION_MONOTONIC_EPS) dec++;
    }
    int total = n - 1;
    return (inc >= total - 1 && dec == 0) || (dec >= total - 1 && inc == 0);
}

// Sorts a local copy of (size, value) pairs by VALUE ascending (the axis
// being inverted), then piecewise-linearly interpolates/clamps liveValue
// against it to estimate size.
static float _interpolateSizeFromValue(float liveValue,
                                       const float sizesIn[SENSOR_CAL_NUM_POINTS],
                                       const float valuesIn[SENSOR_CAL_NUM_POINTS]) {
    const int n = SENSOR_CAL_NUM_POINTS;
    float size[SENSOR_CAL_NUM_POINTS], val[SENSOR_CAL_NUM_POINTS];
    for (int i = 0; i < n; i++) { size[i] = sizesIn[i]; val[i] = valuesIn[i]; }

    for (int i = 1; i < n; i++) {  // insertion sort by value ascending
        float v = val[i], s = size[i];
        int j = i - 1;
        while (j >= 0 && val[j] > v) { val[j + 1] = val[j]; size[j + 1] = size[j]; j--; }
        val[j + 1] = v; size[j + 1] = s;
    }

    if (liveValue <= val[0]) return size[0];
    if (liveValue >= val[n - 1]) return size[n - 1];
    for (int i = 1; i < n; i++) {
        if (liveValue <= val[i]) {
            float t = (val[i] > val[i - 1]) ? (liveValue - val[i - 1]) / (val[i] - val[i - 1]) : 0.0f;
            return size[i - 1] + t * (size[i] - size[i - 1]);
        }
    }
    return size[n - 1];  // unreachable, satisfies compilers that want a return here
}

static float _medianOf(float *values, uint8_t n) {
    for (uint8_t i = 1; i < n; i++) {  // insertion sort, n <= 4
        float v = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > v) { values[j + 1] = values[j]; j--; }
        values[j + 1] = v;
    }
    if (n % 2 == 1) return values[n / 2];
    return (values[n / 2 - 1] + values[n / 2]) / 2.0f;
}

FusedSizeEstimate calib_estimate_particle_size_um(float uasAttenuation, float turbApdsRatio,
                                                   float turbMaxRatio, float forceGrams) {
    FusedSizeEstimate result{};

    float sizes[SENSOR_CAL_NUM_POINTS];
    float uasVals[SENSOR_CAL_NUM_POINTS], apdsVals[SENSOR_CAL_NUM_POINTS];
    float maxVals[SENSOR_CAL_NUM_POINTS], forceVals[SENSOR_CAL_NUM_POINTS];
    for (int i = 0; i < SENSOR_CAL_NUM_POINTS; i++) {
        sizes[i]    = SENSOR_CAL_TABLE[i].knownSizeUm;
        uasVals[i]  = SENSOR_CAL_TABLE[i].uasAttenuation;
        apdsVals[i] = SENSOR_CAL_TABLE[i].turbApdsRatio;
        maxVals[i]  = SENSOR_CAL_TABLE[i].turbMaxRatio;
        forceVals[i]= SENSOR_CAL_TABLE[i].forceGrams;
    }

    float estimates[4];
    uint8_t n = 0;

    if (_isMonotonicVsSize(sizes, uasVals)) {
        estimates[n++] = _interpolateSizeFromValue(uasAttenuation, sizes, uasVals);
        result.uasTrusted = true;
    }
    if (_isMonotonicVsSize(sizes, apdsVals)) {
        estimates[n++] = _interpolateSizeFromValue(turbApdsRatio, sizes, apdsVals);
        result.turbApdsTrusted = true;
    }
    if (_isMonotonicVsSize(sizes, maxVals)) {
        estimates[n++] = _interpolateSizeFromValue(turbMaxRatio, sizes, maxVals);
        result.turbMaxTrusted = true;
    }
    if (_isMonotonicVsSize(sizes, forceVals)) {
        estimates[n++] = _interpolateSizeFromValue(forceGrams, sizes, forceVals);
        result.forceTrusted = true;
    }

    result.numChannelsUsed = n;
    result.sizeUm = (n > 0) ? _medianOf(estimates, n) : 0.0f;
    return result;
}

float calib_estimate_particle_size_from_uas_delta_v_um(float deltaVoltageVolts) {
    // pow() with a negative fractional exponent is undefined for a
    // non-positive base. Unlike the previous absolute-voltage version of
    // this function, deltaV <= 0 here is EXPECTED/NORMAL early in a run —
    // before mixing has caused enough change from baseline yet — not
    // necessarily a sensor fault. Returning 0 (no valid size estimate
    // yet) is still the right behavior either way, just for a different
    // reason than before.
    if (deltaVoltageVolts <= 0.0f) return 0.0f;
    return UAS_SIZE_EQ_COEFFICIENT * powf(deltaVoltageVolts, UAS_SIZE_EQ_EXPONENT);
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

void calib_breakage_reset() {
    _numPoints = 0;
    _nextSlot = 0;
}

void calib_breakage_add_point(uint32_t strokeCount, float measuredUm) {
    if (measuredUm <= BREAKAGE_D_MIN_UM) return;  // would need log of a non-positive number

    _ptN[_nextSlot] = (float)strokeCount;
    _ptLnD[_nextSlot] = logf(measuredUm - BREAKAGE_D_MIN_UM);
    _nextSlot = (_nextSlot + 1) % BREAKAGE_MAX_POINTS;
    if (_numPoints < BREAKAGE_MAX_POINTS) _numPoints++;
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

uint32_t calib_predict_total_strokes(float targetUm) {
    BreakageFit fit = calib_breakage_get_fit();  // fallback constants if <2 verification points on file

    // Invert the model for the stroke count (from a fresh D0) at which
    // size == targetUm.
    float dMin = BREAKAGE_D_MIN_UM;
    float numerator = fit.d0Um - dMin;
    float denominator = targetUm - dMin;
    if (numerator <= 0.0f || denominator <= 0.0f) {
        // Target at/below D_min, or a nonsensical fit — refuse to
        // extrapolate into a regime the model can't represent.
        return 0;
    }

    float nTarget = logf(numerator / denominator) / fit.k;
    if (nTarget <= 0.0f) return 0;  // target already at/above the unmixed size — nothing to do

    uint32_t strokes = (uint32_t)(nTarget + 0.5f);  // round to nearest
    if (strokes > MIXING_MAX_STROKES_SAFETY_CAP) strokes = MIXING_MAX_STROKES_SAFETY_CAP;
    return strokes;
}

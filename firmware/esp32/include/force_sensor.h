#pragma once
#include <stdint.h>
#include <stdbool.h>

// 2x HX711 load-cell amplifiers on a shared clock line (design brief §10) —
// custom bit-bang driver, since standard HX711 libraries assume one
// dedicated SCK per chip. Both DT lines are sampled within the same pulse
// burst so the two channels stay in lock-step.
//
// Raw counts are converted to grams via calibration.h (calib_hx711_to_grams)
// — see firmware/CALIBRATION.md §4 for the tare/scale measurement procedure.
// This is also the automatic e-stop input, independent of the mixing
// scheduler — see calib_force_estop_tripped() and CALIBRATION.md §8.

void force_sensor_init();

// Non-blocking: returns false (leaves outputs unchanged) if either channel
// isn't ready yet. Call every loop().
bool force_sensor_update();

float force_sensor_get_grams_1();
float force_sensor_get_grams_2();

// True the instant either channel crosses HX711_ESTOP_GRAMS (calibration.h).
// Caller (main.cpp) must route this into the same immediate motor-kill path
// as the button e-stop — see firmware/CALIBRATION.md §8.
bool force_sensor_estop_tripped();
// Raw ADC counts (pre-calibration) — for diagnostics/telemetry only, not
// used anywhere in the fusion/e-stop path, which works entirely in grams.
int32_t force_sensor_get_raw_1();
int32_t force_sensor_get_raw_2();
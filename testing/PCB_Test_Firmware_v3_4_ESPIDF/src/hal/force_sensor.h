#pragma once
#include "port_compat.h"

// 2x HX711 load-cell amplifiers on a shared clock line (brief §10.2) — a
// custom bit-bang driver because standard HX711 libraries assume one
// dedicated SCK per chip. Both DT lines are sampled within the same pulse
// burst so the two channels stay in lock-step.
//
// Gain/channel is selected by the pulse count *after* the 24 data bits, for
// the *next* conversion (§10.1): 25 = channel A / gain 128 (used here).

void forceSensorInit();

// Non-blocking: returns false (leaves outputs unchanged) if either channel
// isn't ready yet. Call at FORCE_SAMPLE_PERIOD_MS or faster.
bool forceSensorRead(int32_t &raw1, int32_t &raw2);

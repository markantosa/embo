#pragma once
#include "port_compat.h"

// Turbidity sensing pair, shared I2C bus (§9): APDS9960 (0x39, ALS
// transmission-mode clear channel, external LED hardwired on) and MAX30102
// (0x57, backscatter mode, own onboard 660/880nm LEDs). Minimal
// register-level drivers — just enough to stream raw counts for bench
// bring-up, not a full feature driver for either part.

void turbidityInit();

struct TurbidityReading {
	uint16_t alsClear;   // APDS9960 clear-channel ALS count (transmission path)
	uint32_t irRaw;      // MAX30102 IR channel (backscatter)
	uint32_t redRaw;     // MAX30102 RED channel (backscatter)
	bool apdsOk;
	bool maxOk;
};

// Polls both devices' latest sample over I2C. Safe to call at
// TURBIDITY_SAMPLE_PERIOD_MS; each device's own ADC integration time gates
// how often a genuinely new value shows up.
TurbidityReading turbidityRead();

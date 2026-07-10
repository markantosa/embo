#include "uas_sensor.h"
#include "config.h"

void uasSensorInit() {
	analogReadResolution(12);
	analogSetPinAttenuation(PIN_UAS_ADC, ADC_11db); // full 0-3.3V range
}

float uasReadEnvelope() {
	uint32_t acc = 0;
	for (uint32_t i = 0; i < UAS_OVERSAMPLE_COUNT; i++) {
		acc += analogRead(PIN_UAS_ADC);
	}
	float raw = (float)acc / UAS_OVERSAMPLE_COUNT;
	return raw * 3.3f / 4095.0f;
}

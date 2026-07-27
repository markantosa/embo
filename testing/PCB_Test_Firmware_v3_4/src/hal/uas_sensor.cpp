#include "uas_sensor.h"
#include "config.h"
#include "../ble/ble_service.h"
#include <SPI.h>
#include <AD9833.h>

// AD9833 on GPIO38 (FSYNC), SPI Mode 2. Shares MOSI/CLK (35/36) with the TFT
// on a different CS line. LovyanGFX's Bus_SPI claims SPI2_HOST (== the
// default Arduino `SPI` object's host on ESP32-S3, i.e. FSPI) directly via
// the low-level ESP-IDF driver, so reusing the global `SPI` object here
// collided with it (blank screen). Using a dedicated SPIClass on HSPI
// (SPI3_HOST) puts the AD9833 on a genuinely separate host controller.
static SPIClass _ddsSpi(HSPI);
static AD9833 _dds(PIN_AD9833_CS, &_ddsSpi);
static bool _ddsReady = false;

void uasSensorInit() {
	analogReadResolution(12);
	analogSetPinAttenuation(PIN_UAS_ADC, ADC_11db); // full 0-3.3V range

	// No MISO: the AD9833 has no readback register.
	_ddsSpi.begin(PIN_SPI_CLK, -1, PIN_SPI_MOSI, -1);

	_dds.begin();
	_dds.setFrequency(UAS_DDS_FREQ_HZ, 0);
	_dds.setPhase(0, 0);
	_dds.setWave(AD9833_SINE);
	_ddsReady = true;

	// Let the signal chain (transducer, envelope detector RC tau=100us)
	// settle before anything reads the ADC.
	delay(UAS_DDS_SETTLE_MS);

	bleLog("UAS: AD9833 init OK, %.0f Hz tone", UAS_DDS_FREQ_HZ);
}

bool uasDdsReady() {
	return _ddsReady;
}

float uasReadEnvelope() {
	uint32_t acc = 0;
	for (uint32_t i = 0; i < UAS_OVERSAMPLE_COUNT; i++) {
		acc += analogRead(PIN_UAS_ADC);
	}
	float raw = (float)acc / UAS_OVERSAMPLE_COUNT;
	return raw * 3.3f / 4095.0f;
}

#include "uas.h"
#include "config.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <SPI.h>
#include <AD9833.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// AD9833 on GPIO38 (FSYNC). SPI bus is shared with ILI9341 and XPT2046.
// AD9833 uses Mode 2; ILI9341/XPT2046 use Mode 0. The bill2462 library calls
// SPI.beginTransaction(SPISettings(freq, MSBFIRST, SPI_MODE2)) on each access,
// so the mode switches correctly around every transaction.
// HARDWARE NOTE: MCLK source for AD9833 is assumed 25MHz. Verify on first bring-up.
static AD9833 _dds(PIN_AD9833_CS, 25000000UL);

static esp_adc_cal_characteristics_t _adc_chars;
static float _baseline_mv = 0.0f;
static float _last_attenuation = 1.0f;

static uint32_t _adc_read_mv() {
    int raw = adc1_get_raw(UAS_ADC_CHANNEL);
    // Calibrated conversion using ESP32-S3 characterization curve.
    // Compensates for ADC non-linearity — especially important near rails.
    // For a ratio measurement (attenuation) most error cancels, but good practice.
    return esp_adc_cal_raw_to_voltage((uint32_t)raw, &_adc_chars);
}

void uas_init() {
    // ADC1 CH0 (GPIO1) — 12dB attenuation, 0–3.1V, 12-bit.
    // ADC1 is safe during BLE; ADC2 cannot be used while the radio is active.
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(UAS_ADC_CHANNEL, ADC_ATTEN_DB_12);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12,
                             ADC_WIDTH_BIT_12, 0, &_adc_chars);

    // Bring up SPI bus with explicit pins before AD9833.begin() calls SPI.begin().
    // TFT_eSPI (ui_init) runs after uas_init and will adopt the same bus.
    SPI.begin(PIN_SPI_CLK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

    // AD9833 init sequence:
    //   begin() — full reset, zero freq/phase registers
    //   setFrequency / setPhase — program REG0 while still in reset
    //   setOutputSource — select REG0 as active register
    //   setMode(SINE_WAVE) — writes control word with RESET=0, output starts
    _dds.begin();
    _dds.setFrequency(REG0, 1000000.0f);
    _dds.setPhase(REG0, 0);
    _dds.setOutputSource(REG0);
    _dds.setMode(SINE_WAVE);

    // Allow signal chain (Tx amp, envelope detector, RC filter τ=100µs) to settle
    // before reading baseline. 10ms >> 5× τ — well beyond any transient.
    delay(10);

    uas_calibrate_baseline();
    ble_log("UAS: init OK, baseline=%.1f mV", _baseline_mv);
}

void uas_calibrate_baseline() {
    delayMicroseconds(UAS_SETTLE_US);
    _baseline_mv = (float)_adc_read_mv();
    _last_attenuation = 1.0f;
}

void uas_update() {
    delayMicroseconds(UAS_SETTLE_US);
    float mv = (float)_adc_read_mv();
    if (_baseline_mv > 0.0f) {
        _last_attenuation = mv / _baseline_mv;
    }
}

float uas_get_attenuation() {
    return _last_attenuation;
}

float uas_get_baseline_mv() {
    return _baseline_mv;
}

uint32_t uas_read_mv() {
    return _adc_read_mv();
}

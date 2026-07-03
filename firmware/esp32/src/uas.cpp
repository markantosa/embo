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

static const float _freq_hz[UAS_NUM_FREQUENCIES] = {
    UAS_FREQ_HZ_0, UAS_FREQ_HZ_1, UAS_FREQ_HZ_2
};
static float _baseline_mv[UAS_NUM_FREQUENCIES]    = {0};
static float _last_attenuation[UAS_NUM_FREQUENCIES] = {1.0f, 1.0f, 1.0f};

static uint32_t _adc_read_mv() {
    int raw = adc1_get_raw(UAS_ADC_CHANNEL);
    // Calibrated conversion using ESP32-S3 characterization curve.
    // Compensates for ADC non-linearity — especially important near rails.
    // For a ratio measurement (attenuation) most error cancels, but good practice.
    return esp_adc_cal_raw_to_voltage((uint32_t)raw, &_adc_chars);
}

// Retune the DDS to a given frequency and wait for the signal chain
// (Tx amp, envelope detector, RC filter τ=100µs) to settle before reading.
static uint32_t _read_at_frequency(float freq_hz) {
    _dds.setFrequency(REG0, freq_hz);
    delayMicroseconds(UAS_SETTLE_US);
    return _adc_read_mv();
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
    _dds.setFrequency(REG0, _freq_hz[0]);
    _dds.setPhase(REG0, 0);
    _dds.setOutputSource(REG0);
    _dds.setMode(SINE_WAVE);

    // Allow signal chain to settle before reading baseline. 10ms >> 5x tau —
    // well beyond any transient. Frequency retunes later only need UAS_SETTLE_US
    // since the DDS output itself starts clean; only the analog chain settles.
    delay(10);

    uas_calibrate_baseline();
    ble_log("UAS: init OK, %u-freq sweep, baseline[0]=%.1f mV",
            UAS_NUM_FREQUENCIES, _baseline_mv[0]);
}

void uas_calibrate_baseline() {
    for (uint8_t i = 0; i < UAS_NUM_FREQUENCIES; i++) {
        _baseline_mv[i] = (float)_read_at_frequency(_freq_hz[i]);
        _last_attenuation[i] = 1.0f;
    }
}

void uas_update() {
    for (uint8_t i = 0; i < UAS_NUM_FREQUENCIES; i++) {
        uint32_t mv = _read_at_frequency(_freq_hz[i]);
        if (_baseline_mv[i] > 0.0f) {
            _last_attenuation[i] = (float)mv / _baseline_mv[i];
        }
    }
    // Leaves the DDS tuned to the last swept frequency (UAS_FREQ_HZ_2) until
    // the next uas_update() call — uas_read_mv() reads whatever that is.
}

uint8_t uas_get_num_frequencies() { return UAS_NUM_FREQUENCIES; }

float uas_get_frequency_hz(uint8_t freq_idx) {
    if (freq_idx >= UAS_NUM_FREQUENCIES) freq_idx = 0;
    return _freq_hz[freq_idx];
}

float uas_get_attenuation(uint8_t freq_idx) {
    if (freq_idx >= UAS_NUM_FREQUENCIES) freq_idx = 0;
    return _last_attenuation[freq_idx];
}

float uas_get_baseline_mv(uint8_t freq_idx) {
    if (freq_idx >= UAS_NUM_FREQUENCIES) freq_idx = 0;
    return _baseline_mv[freq_idx];
}

uint32_t uas_read_mv() {
    return _adc_read_mv();
}

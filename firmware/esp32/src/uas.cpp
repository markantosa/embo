#include "uas.h"
#include "config.h"
#include "ble_debug.h"
#include <Arduino.h>
#include <SPI.h>
#include <AD9833.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

// AD9833 on GPIO38 (FSYNC). SPI bus is shared with ILI9341 and XPT2046.
// Uses robtillaart/AD9833 (see platformio.ini) — hardware-SPI constructor,
// defaults to the global SPI object and its own SPISettings per transaction.
// HARDWARE NOTE: MCLK source for AD9833 is assumed 25MHz, which matches this
// library's own default crystal frequency (see setCrystalFrequency()) — no
// explicit call needed unless bench measurement finds it's actually different.
//
// INTEGRATION NOTE — verify on first bring-up: ui.cpp's LovyanGFX display
// now owns SPI2_HOST directly (its own Bus_SPI instance) rather than
// sharing this Arduino SPIClass object the way TFT_eSPI implicitly did.
// Both target the same physical MOSI/CLK GPIOs (35/36) with different CS
// lines, which is fine electrically, but confirm the two peripheral
// drivers don't fight over the same underlying SPI host — reassign the
// AD9833 to its own host (SPI3_HOST) if any contention shows up on scope.
//
// REVERTED a dedicated-HSPI-host attempt at this (see git history /
// testing/UAS_Testing_with_multiple_frequency_sweep) after a board hung at
// the boot splash following that change — giving the AD9833 its own SPI
// *host* while it's still wired to the *same physical pads* as the TFT's
// SPI2_HOST doesn't obviously avoid contention (only one peripheral's
// signal can drive a given GPIO pad at a time; whichever `begin()` runs
// last — here, `ui_init()`'s `tft.init()`, since it runs after `uas_init()`
// — reclaims the pad, but that reclaiming step is exactly the kind of thing
// that can wedge init if the two driver stacks disagree about pin state).
// Back to the original shared-object approach pending an actual scope
// check — don't re-apply the HSPI change without hardware to verify it on.
static AD9833 _dds(PIN_AD9833_CS);

static esp_adc_cal_characteristics_t _adc_chars;

static const float _freq_hz[UAS_NUM_FREQUENCIES] = {
    UAS_FREQ_HZ_0, UAS_FREQ_HZ_1, UAS_FREQ_HZ_2
};
static float _baseline_mv[UAS_NUM_FREQUENCIES]    = {0};
static float _last_attenuation[UAS_NUM_FREQUENCIES] = {1.0f, 1.0f, 1.0f};

// Manual frequency override, for bench/bring-up use only — see uas.h and
// ble_debug.cpp's UASFREQ/UASSWEEP commands.
static bool  _manualOverrideActive = false;
static float _manualOverrideHz     = 0.0f;

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
    _dds.setFrequency(freq_hz, 0);  // channel 0
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
    // No MISO: v3.3 moved touch's data-out off this bus's old shared MISO pin
    // (see config.h), and the AD9833 is write-only (no readback register).
    // See the INTEGRATION NOTE above `_dds`'s declaration — this shares the
    // global SPI object with LGFX's display bus pending a scope-verified fix.
    SPI.begin(PIN_SPI_CLK, -1, PIN_SPI_MOSI, -1);

    // AD9833 init sequence (robtillaart/AD9833 API):
    //   begin() — full reset, zero freq/phase registers
    //   setFrequency/setPhase(value, channel) — program channel 0
    //   setWave(AD9833_SINE) — writes control word with RESET=0, output starts
    _dds.begin();
    _dds.setFrequency(_freq_hz[0], 0);
    _dds.setPhase(0, 0);
    _dds.setWave(AD9833_SINE);

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
    if (_manualOverrideActive) {
        // Bench mode: hold the manual frequency, don't touch
        // _last_attenuation[] — the production channels just keep
        // reporting whatever they last measured until the override clears.
        return;
    }

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

void uas_set_manual_override_hz(float hz) {
    _dds.setFrequency(hz, 0);
    delayMicroseconds(UAS_SETTLE_US);
    _manualOverrideHz     = hz;
    _manualOverrideActive = true;
}

void uas_clear_manual_override() {
    if (!_manualOverrideActive) return;
    _manualOverrideActive = false;
    // Resume the production cycle from its first tone rather than leaving
    // the DDS sitting at the last bench frequency until the next
    // uas_update() happens to sweep past it.
    _dds.setFrequency(_freq_hz[0], 0);
    delayMicroseconds(UAS_SETTLE_US);
}

bool uas_has_manual_override() { return _manualOverrideActive; }

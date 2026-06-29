#include "uas.h"
#include "config.h"
#include <Arduino.h>
#include <driver/adc.h>

static float _baseline_mv = 0.0f;
static float _last_attenuation = 1.0f;

// TODO: init AD9833 via SPI and set output to 1MHz sine

void uas_init() {
    // ADC1 GPIO1 — 12dB attenuation (0–3.1V range)
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(UAS_ADC_CHANNEL, ADC_ATTEN_DB_12);

    // TODO: init AD9833, enable 1MHz output
    // TODO: wait for signal to settle, then call uas_calibrate_baseline()
}

void uas_calibrate_baseline() {
    delayMicroseconds(UAS_SETTLE_US);
    int raw = adc1_get_raw(UAS_ADC_CHANNEL);
    // TODO: convert raw → mV using ESP32-S3 characterisation curve
    _baseline_mv = (float)raw * (3100.0f / 4095.0f);
}

void uas_update() {
    delayMicroseconds(UAS_SETTLE_US);
    int raw = adc1_get_raw(UAS_ADC_CHANNEL);
    float mv = (float)raw * (3100.0f / 4095.0f);
    if (_baseline_mv > 0.0f) {
        _last_attenuation = mv / _baseline_mv;
    }
}

float uas_get_attenuation() {
    return _last_attenuation;
}

#include "force_sensor.h"
#include "config.h"
#include "calibration.h"
#include "ble_debug.h"
#include <Arduino.h>

static int32_t _raw1 = 0, _raw2 = 0;
static float _grams1 = 0.0f, _grams2 = 0.0f;

void force_sensor_init() {
    pinMode(PIN_HX711_SCK, OUTPUT);
    digitalWrite(PIN_HX711_SCK, LOW);
    pinMode(PIN_HX711_1_DT, INPUT);
    pinMode(PIN_HX711_2_DT, INPUT);
}

static int32_t signExtend24(uint32_t v) {
    if (v & 0x800000) v |= 0xFF000000;
    return (int32_t)v;
}

bool force_sensor_update() {
    // DT reads HIGH while a conversion isn't ready yet.
    if (digitalRead(PIN_HX711_1_DT) == HIGH || digitalRead(PIN_HX711_2_DT) == HIGH) {
        return false;
    }

    // Critical section: PD_SCK must never sit HIGH for more than ~60us or
    // the chip powers itself down mid-read.
    noInterrupts();
    uint32_t v1 = 0, v2 = 0;
    for (int i = 0; i < 24; i++) {
        digitalWrite(PIN_HX711_SCK, HIGH);
        delayMicroseconds(1);
        v1 = (v1 << 1) | digitalRead(PIN_HX711_1_DT);
        v2 = (v2 << 1) | digitalRead(PIN_HX711_2_DT);
        digitalWrite(PIN_HX711_SCK, LOW);
        delayMicroseconds(1);
    }
    // 25th pulse: selects channel A / gain 128 for the next conversion.
    digitalWrite(PIN_HX711_SCK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_HX711_SCK, LOW);
    delayMicroseconds(1);
    interrupts();

    _raw1 = signExtend24(v1);
    _raw2 = signExtend24(v2);
    _grams1 = calib_hx711_to_grams(0, _raw1);
    _grams2 = calib_hx711_to_grams(1, _raw2);
    return true;
}

float force_sensor_get_grams_1() { return _grams1; }
float force_sensor_get_grams_2() { return _grams2; }
int32_t force_sensor_get_raw_1() { return _raw1; }
int32_t force_sensor_get_raw_2() { return _raw2; }

void force_sensor_tare(uint8_t samples) {
    int64_t sum1 = 0, sum2 = 0;
    uint8_t got = 0;
    uint32_t deadline = millis() +2000;
    while (got <samples && millis() <deadline){
        if (force_sensor_update()) {
            sum1 += _raw1;
            sum2 += _raw2;
            got++;
        }
    }
    if (got==0) {
        ble_log("Force sensor: tare FAILED (no readings within 2s) - using fallback tare");
        return;
    }
    int32_t tare1 = (int32_t)(sum1/got);
    int32_t tare2 = (int32_t)(sum2/got);

    calib_hx711_set_tare(0,tare1);
    calib_hx711_set_tare(1,tare2);
    ble_log("Force sensor: tared (raw1=%1d raw2=%1d, %u samples)", (long)tare1, (long)tare2,got);

}

bool force_sensor_estop_tripped() {
    return calib_force_estop_tripped(_grams1, _grams2);
}

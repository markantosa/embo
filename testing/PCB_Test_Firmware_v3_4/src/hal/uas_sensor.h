#pragma once
#include <Arduino.h>

// UAS envelope reading, GPIO1 / ADC1_CH0. Oversampled average — the
// envelope RC (tau=100us, §5.6) already does the analog smoothing, this
// just knocks down ADC quantization noise.
void uasSensorInit();
float uasReadEnvelope(); // returns 0.0-3.3 (volts)

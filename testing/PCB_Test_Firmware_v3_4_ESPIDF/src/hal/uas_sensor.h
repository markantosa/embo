#pragma once
#include "port_compat.h"

// UAS envelope reading, GPIO1 / ADC1_CH0. Oversampled average — the
// envelope RC (tau=100us, §5.6) already does the analog smoothing, this
// just knocks down ADC quantization noise.
//
// Also owns the AD9833 DDS that drives the ultrasonic transducer (§8) —
// programs a fixed 1MHz sine tone at init. This is bench-test firmware, so
// unlike the production multi-frequency sweep (firmware/esp32/src/uas.cpp)
// there's no attenuation-ratio math here, just "is the tone on and does the
// ADC reading respond to it" for bring-up validation.
void uasSensorInit();
float uasReadEnvelope(); // returns 0.0-3.3 (volts)

// True if the AD9833 responded to its init sequence (SPI write-only, no
// readback register — this just reflects whether begin() was called, not a
// hardware confirmation. Verify tone presence with a scope on first bring-up).
bool uasDdsReady();

#pragma once
#include <stdint.h>

void uas_init();
void uas_update();  // call every loop() — sweeps all configured frequencies

// Multi-frequency attenuation spectroscopy (see docs/EMBO_UAS_CV_Technical_Advisory.txt).
// A single-frequency reading cannot reliably resolve a polydisperse particle
// population, so the AD9833 sweeps UAS_NUM_FREQUENCIES discrete tones each
// update cycle instead of holding one fixed 1MHz tone.
uint8_t uas_get_num_frequencies();
float   uas_get_frequency_hz(uint8_t freq_idx);

// Attenuation ratio vs saline baseline at a given frequency index. 1.0 = pure
// saline; <1.0 = particles present. NOT validated as a standalone size proxy —
// per the technical advisory, treat as a secondary/trend signal only (fused
// with StallGuard) until an empirical CV-vs-attenuation correlation check
// confirms a clean, monotonic relationship over the real operating range.
// The CV/camera pipeline remains the authoritative size measurement.
float uas_get_attenuation(uint8_t freq_idx = 0);

// Raw baseline reading in mV at a given frequency index — useful for BLE
// debug / sanity check on first bring-up.
float uas_get_baseline_mv(uint8_t freq_idx = 0);

// Re-sample baseline at all configured frequencies. Call once with saline-only
// in syringe, before mixing starts. Also called automatically at end of uas_init().
void uas_calibrate_baseline();

// Immediate calibrated ADC read in mV at whichever frequency is currently
// tuned — for debug streaming, bypasses settle delay.
uint32_t uas_read_mv();
// Whichever frequency the DDS is tuned to right now — cycles through the
// production set during normal operation, or holds at the bench override
// frequency if uas_has_manual_override() is true. For telemetry/debug only.
float uas_get_current_frequency_hz();

// ── Manual frequency override (bench/bring-up only — see ble_debug.cpp's
// UASFREQ/UASSWEEP commands) ────────────────────────────────────────────────
//
// Retunes the DDS to an arbitrary frequency and holds it there, suspending
// the normal UAS_NUM_FREQUENCIES production cycle (uas_get_attenuation()
// keeps returning whatever it last measured, unchanged) until
// uas_clear_manual_override() is called. scheduler_start() also clears any
// active override itself as a safety net, so a run can never start reading
// attenuation at a leftover bench frequency instead of the calibrated set.
void uas_set_manual_override_hz(float hz);
void uas_clear_manual_override();
bool uas_has_manual_override();

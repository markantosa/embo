#pragma once
#include <stdint.h>

void uas_init();
void uas_update();  // call every loop()

// Attenuation ratio vs saline baseline. 1.0 = pure saline; <1.0 = particles present.
float uas_get_attenuation();

// Raw baseline reading in mV — useful for BLE debug / sanity check on first bring-up.
float uas_get_baseline_mv();

// Re-sample baseline. Call once with saline-only in syringe, before mixing starts.
// Also called automatically at end of uas_init().
void uas_calibrate_baseline();

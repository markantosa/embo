#pragma once
#include <stdint.h>

void uas_init();
void uas_update();          // call every loop()

// Returns latest attenuation ratio vs saline baseline (0.0–1.0).
// 1.0 = same as baseline (no particles / pure saline).
float uas_get_attenuation();

// Re-read baseline (call with saline only before mixing starts).
void uas_calibrate_baseline();

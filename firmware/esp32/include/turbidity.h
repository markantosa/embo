#pragma once
#include <stdint.h>
#include <stdbool.h>

// Turbidity sensing pair, shared I2C bus (design brief §9): APDS9960 (0x39,
// ALS transmission-mode clear channel, external LED hardwired on) and
// MAX30102 (0x57, backscatter mode, own onboard 660/880nm LEDs).
//
// Diagnostic/trend signal only — see docs/EMBO_UAS_CV_Technical_Advisory.txt
// and firmware/CALIBRATION.md §3. Not wired into the mixing scheduler until
// an empirical CV correlation check passes, same discipline as UAS.

void turbidity_init();
void turbidity_update();  // call every loop() — polls both devices

bool turbidity_apds_ok();
bool turbidity_max_ok();

uint16_t turbidity_get_als_clear();
uint32_t turbidity_get_ir_raw();
uint32_t turbidity_get_red_raw();

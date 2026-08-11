#pragma once
#include <stdint.h>
#include <stdbool.h>

// Turbidity sensing pair, shared I2C bus (design brief §9): APDS9960 (0x39,
// ALS transmission-mode clear channel, external LED hardwired on) and
// MAX30102 (0x57, backscatter mode, own onboard 660/880nm LEDs).
//
// Two of the four closed-loop sensor-fusion inputs (scheduler.cpp) once
// SENSOR_CAL_TABLE (calibration.cpp) is populated from the 9-syringe bench
// session — see firmware/CALIBRATION.md §3 and §5. Each channel is
// independently gated by a monotonicity check at runtime
// (calib_estimate_particle_size_um()), so an unfilled or bad calibration
// table safely excludes a channel rather than trusting it blindly.

void turbidity_init();
void turbidity_update();  // call every loop() — polls both devices

bool turbidity_apds_ok();
bool turbidity_max_ok();

uint16_t turbidity_get_als_clear();
uint32_t turbidity_get_ir_raw();
uint32_t turbidity_get_red_raw();

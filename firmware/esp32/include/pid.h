#pragma once
#include <stdint.h>

void pid_init();
void pid_update();          // call every loop()

// Start a mixing run targeting the current setpoint (see pid_set_target_um).
void pid_start();
void pid_stop();

bool pid_is_running();
bool pid_target_reached();  // true when median is within TARGET_TOLERANCE_UM

// Setpoint, clamped to [TARGET_SIZE_UM_MIN, TARGET_SIZE_UM_MAX] from config.h.
// Rejected (no-op) while a run is in progress — change the target between runs only.
void     pid_set_target_um(uint16_t target_um);
uint16_t pid_get_target_um();

#pragma once

void pid_init();
void pid_update();          // call every loop()

// Start a mixing run targeting the global size window in config.h.
void pid_start();
void pid_stop();

bool pid_is_running();
bool pid_target_reached();  // true when median and IQR are in spec

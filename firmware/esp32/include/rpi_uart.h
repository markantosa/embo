#pragma once
#include <stdint.h>

void rpi_uart_init();
void rpi_uart_update();     // call every loop() — parses incoming size packets

// Latest particle size stats received from RPi (µm). -1 = no data yet.
int16_t rpi_get_median_um();
int16_t rpi_get_iqr_um();

// Send a status/command string to RPi.
void rpi_send(const char *msg);

#pragma once

// Tiny Arduino-shaped shims over native ESP-IDF calls, used throughout this
// port so the rest of the codebase (GPIO/timing logic ported from the
// Arduino variant) didn't need a full rewrite of every call site — just the
// framework layer underneath. No Arduino.h anywhere; this is pure IDF/FreeRTOS.

#include <cstdint>
#include <cstdlib>

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef OUTPUT
#define OUTPUT 0x01
#endif
#ifndef INPUT
#define INPUT 0x02
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 0x03
#endif

// pinMode/digitalWrite/digitalRead — plain GPIO only (never used here on
// pins owned by a dedicated peripheral driver: UART/SPI/I2C/LEDC pins are
// configured directly by their own driver init, not through these).
static inline void pinMode(int pin, int mode) {
	gpio_num_t g = (gpio_num_t)pin;
	gpio_reset_pin(g);
	if (mode == OUTPUT) {
		gpio_set_direction(g, GPIO_MODE_OUTPUT);
	} else if (mode == INPUT_PULLUP) {
		gpio_set_direction(g, GPIO_MODE_INPUT);
		gpio_set_pull_mode(g, GPIO_PULLUP_ONLY);
	} else {
		gpio_set_direction(g, GPIO_MODE_INPUT);
		gpio_set_pull_mode(g, GPIO_FLOATING);
	}
}

static inline void digitalWrite(int pin, int level) {
	gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

static inline int digitalRead(int pin) {
	return gpio_get_level((gpio_num_t)pin);
}

// millis()/delay()/delayMicroseconds() — esp_timer/FreeRTOS-backed.
static inline uint32_t millis() {
	return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static inline void delay(uint32_t ms) {
	vTaskDelay(pdMS_TO_TICKS(ms));
}

static inline void delayMicroseconds(uint32_t us) {
	esp_rom_delay_us(us);
}

// noInterrupts()/interrupts() — short critical sections (HX711 bit-bang
// timing, BLE command handoff). portDISABLE/ENABLE_INTERRUPTS() disable
// interrupts on the current core only, matching what arduino-esp32's
// noInterrupts()/interrupts() do under the hood.
static inline void noInterrupts() { portDISABLE_INTERRUPTS(); }
static inline void interrupts() { portENABLE_INTERRUPTS(); }

// constrain() — Arduino macro, reimplemented as a small template.
template <typename T>
static inline T constrain(T x, T lo, T hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

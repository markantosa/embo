#pragma once

// ── GPIO assignments (EMBO v2.4) ────────────────────────────────────────────

// UAS / ADC
#define PIN_UAS_ADC         1   // ADC1_CH0 — envelope detector output

// Status LED
#define PIN_STATUS_LED      2

// Motor driver — shared UART
#define PIN_TMC_UART        4   // half-duplex UART1

// Motor 1 (TMC2209 U5, UART addr 0)
#define PIN_STEP_M1         5
#define PIN_DIR_M1          6
#define PIN_EN_M1           7

// Motor 2 (TMC2209 U6, UART addr 1)
#define PIN_STEP_M2         8
#define PIN_DIR_M2          9
#define PIN_EN_M2           10

// UI buttons (via IDC ribbon)
#define PIN_BTN1            11
#define PIN_BTN2            12
#define PIN_BUZ_PWM         13  // LEDC PWM → 2N7002 gate on breakout

// Limit switches
#define PIN_LIMIT_M1        14
#define PIN_LIMIT_M2        15

// EC11 rotary encoder (via IDC ribbon)
#define PIN_EC11_A          16
#define PIN_EC11_B          17
#define PIN_EC11_SW         18

// USB (fixed PHY — do not reassign)
// GPIO19 = USB D−, GPIO20 = USB D+

// Touch IRQ (via IDC ribbon)
#define PIN_TOUCH_IRQ       21

// SPI bus (shared: ILI9341, XPT2046, AD9833)
#define PIN_SPI_MOSI        35
#define PIN_SPI_MISO        37
#define PIN_SPI_CLK         36

// SPI chip selects
#define PIN_AD9833_CS       38  // AD9833 FSYNC — SPI Mode 2
#define PIN_TFT_CS          39  // ILI9341 CS   — SPI Mode 0
#define PIN_TFT_DC          40
#define PIN_TFT_RST         41
#define PIN_TOUCH_CS        42  // XPT2046 CS   — SPI Mode 0

// RPi UART (UART1)
#define PIN_RPI_TX          47
#define PIN_RPI_RX          48

// ── SPI clock speeds ─────────────────────────────────────────────────────────
#define SPI_CLK_TFT         20000000   // 20 MHz max via IDC ribbon
#define SPI_CLK_TOUCH        2000000   // 2 MHz (XPT2046 max)
#define SPI_CLK_AD9833      10000000   // ~10 MHz — short direct trace

// ── UART speeds ───────────────────────────────────────────────────────────────
#define BAUD_RPI            921600
#define BAUD_TMC            115200

// ── TMC2209 UART addresses ────────────────────────────────────────────────────
#define TMC_ADDR_M1         0   // MS1=GND, MS2=GND
#define TMC_ADDR_M2         1   // MS1=3.3V, MS2=GND

// ── LEDC channels ────────────────────────────────────────────────────────────
#define LEDC_CH_STEP_M1     0
#define LEDC_CH_STEP_M2     1
#define LEDC_CH_BUZ         2

// ── ADC ──────────────────────────────────────────────────────────────────────
// GPIO1 = ADC1_CH0. Use ADC_ATTEN_DB_12 (0–3.1V range, ~0.757 mV/LSB).
// Wait ≥500µs after each AD9833 frequency step before reading.
#define UAS_ADC_CHANNEL     ADC1_CHANNEL_0
#define UAS_SETTLE_US       500

// ── Particle size target ─────────────────────────────────────────────────────
#define TARGET_SIZE_UM_MIN  300
#define TARGET_SIZE_UM_MAX  500

# Firmware — ESP32-S3

Arduino/C++ firmware for the EMBO ESP32-S3-WROOM-1-N8 control board.

## Responsibilities

- PID control loop: reads particle size from RPi over UART, schedules remaining motor strokes
- Motor control: LEDC PWM step generation for 2× TMC2209 stepper drivers
- UAS ADC: reads envelope detector on GPIO1 (ADC1_CH0, 11dB attenuation mode), computes attenuation ratio vs saline baseline
- Limit switch ISR: kills STEP PWM immediately on GPIO14/GPIO15 trigger
- TFT UI: SPI display driver (ILI9341 or ST7789V) + XPT2046 touch
- BLE debug: NimBLE UART service for wireless monitoring during development
- UART bridge: relays particle size commands to/from Raspberry Pi (GPIO47/48, 921600 baud)

## Setup

**Toolchain:** Arduino IDE 2.x + arduino-esp32 package, or PlatformIO.

**Board config:** ESP32-S3 Dev Module, USB CDC On Boot: Enabled, Flash Size: 8MB.

**Flash:** Connect USB-C to J13. Arduino IDE auto-resets into bootloader via RTS/DTR. For manual recovery: hold BOOT (SW_boot) then press RESET (SW_reset).

## Critical firmware notes

### SPI bus (GPIO35/36/37) — shared between three devices

| Device | CS GPIO | SPI Mode | Clock |
|---|---|---|---|
| ILI9341/ST7789V display | GPIO39 | Mode 0 | 40 MHz |
| XPT2046 touch | GPIO42 | Mode 0 | 2 MHz |
| AD9833 signal generator | GPIO38 | **Mode 2** | ≤ 25 MHz |

Switch mode and clock speed in firmware before every transaction. Never mix in the same transaction.

### TMC2209 UART (GPIO4 — half-duplex)

UART1 TX and RX are both mapped to GPIO4 via the ESP32-S3 GPIO matrix. A 1kΩ series resistor between GPIO4 and the junction node connects to both U5 (address 0, MS1=GND, MS2=GND) and U6 (address 1, MS1=3.3V, MS2=GND).

### ADC

ADC1 (GPIO1–GPIO10) is fully usable while BLE is active. ADC2 (GPIO11–GPIO20) is unavailable for analog during BLE — no ADC2 pins are used for analog in this design.

Configure GPIO1 as `ADC_ATTEN_DB_11` (input range 0–3.1V). Read saline-only baseline at startup and store for attenuation ratio computation.

### Touch calibration

XPT2046 returns raw 12-bit ADC values (0–4095). Run a three-point calibration routine on first boot; store coefficients in ESP32-S3 NVS (no external EEPROM needed).

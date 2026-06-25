# Software — TFT Touchscreen UI

UI logic and assets for the ILI9341/ST7789V TFT display on the ESP32-S3 board.

## Display hardware

- Module: Shopee SPI TFT 5V/3.3V PCB Adapter (ILI9341 or ST7789V controller)
- Touch: XPT2046 resistive touch controller
- Resolution: 240×320
- Connected via SPI2 (GPIO35/36/37) — powered from 3.3V rail

**Confirm controller variant (ILI9341 vs ST7789V)** from physical module inspection before finalising firmware — initialization sequences differ.

## UI screens

| Screen | Description |
|---|---|
| Home | Device status, current particle size reading, target range display |
| Settings | Target size (µm), stroke speed, number of pre-mix strokes |
| Run | Live particle size graph, motor status, stop button |
| Calibration | Touch calibration routine (three-point, stores to NVS) |

## Library

TFT_eSPI configured for ILI9341/ST7789V. LVGL optional for future widget-based UI.

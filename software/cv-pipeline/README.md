# Software — Computer Vision Pipeline (Raspberry Pi 5)

Python pipeline that captures images of the gelatin foam slurry through the syringe, runs YOLOv8 inference, and reports particle size statistics to the ESP32-S3 over UART.

## Responsibilities

- Capture frames from Raspberry Pi Global Shutter Camera (CSI ribbon)
- Run YOLOv8 model (trained on gelatin foam particle images) to detect and measure individual particles
- Compute median particle diameter (µm) and IQR per frame
- Transmit statistics to ESP32-S3 over UART (GPIO14/15 on RPi → GPIO47/48 on ESP32-S3, 921600 baud, 3.3V)

## Particle size model

Mixing follows first-order breakage kinetics:

```
D(N) = D_min + (D₀ − D_min) × e^(−kN)
```

The CV pipeline's job is to measure `D(N)` accurately so the PID loop on the ESP32-S3 can compute remaining strokes.

## Setup

```bash
pip install -r requirements.txt
python main.py
```

**Camera:** Raspberry Pi Global Shutter Camera connected via CSI ribbon. LED backlight panel (powered from PCB J10 at 5V) illuminates the syringe from behind for particle contrast.

**UART:** RPi GPIO14 (TX) → ESP32-S3 GPIO48 (RX), RPi GPIO15 (RX) → ESP32-S3 GPIO47 (TX). Both sides 3.3V — no level shifter required.

**Raspberry Pi power:** Separate USB-C supply. Do not draw power from the UART connector to the PCB.

## Model training

YOLOv8 model trained specifically on gelatin foam particle images. Training data and weights stored separately (see `weights/` — gitignored; obtain from team drive).

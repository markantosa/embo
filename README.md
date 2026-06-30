# EMBO — Embolization Particle Sizing System HEHE

> SUTD 30.007 Engineering Design Innovation | Team EMBO | Week 5 of 13

EMBO is a closed-loop medical device that automates and quality-controls the preparation of gelatin foam embolic agents for interventional radiology procedures. It is the first device of its kind to measure and control particle size during embolic agent preparation in real time.

| | |
|:---:|:---:|
| ![EMBO isometric view](assets/embo%20iso.png) | ![EMBO side view](assets/embo%20side.png) |
| *Isometric render* | *Front view* |

---

## The Problem

Embolization procedures use tiny gelatin foam particles (embolic agents) to deliberately block blood vessels — treating tumours, fibroids, and bleeding. Before every procedure, a clinician manually prepares a particle slurry by pumping two syringes back and forth until the mixture reaches a "pudding consistency." There is no measurement, no standard, and no quality check. Two clinicians doing this side by side produce slurries with completely different particle size distributions. Particle size directly affects clinical outcomes: wrong sizes can cause tissue death in unintended areas or fail to block the target vessel.

**No device currently exists to measure or control particle size during this preparation step.** Gelfoam has been used since the 1970s with the preparation method unchanged.

---

## What EMBO Does

EMBO replaces manual syringe pumping with a controlled, sensor-guided system that:

1. **Automates mixing** — two stepper motors drive syringe plungers back and forth with precise speed, stroke, and cycle control
2. **Measures particle size in real time** using two parallel sensing modalities:
   - **Computer vision** — a Raspberry Pi 5 + Global Shutter Camera running a YOLOv8 model trained on gelatin foam particles, reporting median particle size and IQR
   - **Ultrasound attenuation** — a 1 MHz acoustic signal chain measures how much sound energy the slurry absorbs; larger particles attenuate more
3. **Stops automatically** when the target particle size range (300–500 µm) is reached, using a PID control loop on the ESP32-S3 that adjusts stroke count based on live sensor feedback

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        EMBO Device                              │
│                                                                 │
│  ┌──────────────────┐          ┌──────────────────────────────┐ │
│  │  Raspberry Pi 5  │◄─UART──►│     ESP32-S3 MCU Board       │ │
│  │                  │          │                              │ │
│  │  - YOLOv8 CV     │          │  - PID control loop          │ │
│  │  - Particle size │          │  - 2× TMC2209 motor drivers  │ │
│  │    statistics    │          │  - UAS signal chain          │ │
│  │  - Global        │          │  - ILI9341/ST7789V TFT UI    │ │
│  │    Shutter Cam   │          │  - BLE debug (NimBLE)        │ │
│  └──────────────────┘          └──────────────────────────────┘ │
│           │                             │           │            │
│           ▼                             ▼           ▼            │
│    [Camera + LED]              [Stepper motors] [UAS Transducers]│
│    [Backlight panel]           [Limit switches] [SMA connectors] │
└─────────────────────────────────────────────────────────────────┘
```

### Particle Size Model

Mixing follows first-order breakage kinetics:

```
D(N) = D_min + (D₀ − D_min) × e^(−kN)
```

Where `N` is stroke count, `k` is a shear constant, `D₀` is initial particle size, and `D_min` is the minimum achievable size. The PID loop uses live measurements of `D(N)` to schedule remaining strokes.

---

## Repository Structure

```
EMBO/
├── README.md
├── .gitignore
│
├── docs/                              # Design documents and briefs
│   ├── EMBO_Project_Overview.txt      # Full project context for new members
│   ├── EMBO_PCB_Design_Brief_v2.1.txt # Complete electrical design spec
│   └── EMBO_PCB_Design_Brief_v2.1.docx
│
├── hardware/
│   ├── electrical/                    # KiCad schematic + PCB layout
│   │   └── README.md
│   └── mechanical/                    # CAD files (SolidWorks / Fusion 360)
│       └── README.md
│
├── firmware/
│   └── esp32/                         # ESP32-S3 Arduino/C++ firmware
│       └── README.md
│
├── software/
│   ├── cv-pipeline/                   # Raspberry Pi YOLOv8 vision pipeline
│   │   └── README.md
│   └── ui/                            # TFT touchscreen UI assets/logic
│       └── README.md
│
└── assets/                            # Images, diagrams, photos
```

---

## Hardware Overview

### Custom MCU Board (ESP32-S3)

| Subsystem | Components |
|---|---|
| Microcontroller | ESP32-S3-WROOM-1-N8 (8MB flash, BLE 5.0, USB-C native) |
| Motor control | 2× TMC2209 stepper drivers (SpreadCycle + StallGuard) |
| Ultrasound signal chain | AD9833 DDS generator → OPA2354 Tx amp (G=4.9) → transducer → OPA2354 Rx (100×) → BAT54 envelope detector → GPIO1 ADC |
| Display | SPI TFT (ILI9341/ST7789V) + XPT2046 resistive touch via SPI2 |
| Power | 24V PSU → LM2596 buck (5V) → AMS1117 LDO (3.3V); AO3401 reverse polarity protection |
| Comms | UART to Raspberry Pi (921600 baud); BLE UART debug stream (NimBLE) |

### GPIO Assignments (key signals)

| GPIO | Signal | Notes |
|---|---|---|
| 1 | UAS_ADC | ADC1_CH0 — envelope detector output |
| 4 | TMC_UART | Half-duplex UART to both TMC2209 via 1kΩ |
| 5/8 | STEP_M1/M2 | LEDC PWM for motor speed control |
| 6/9 | DIR_M1/M2 | Motor direction |
| 7/10 | EN_M1/M2 | Motor enable (active LOW, 10kΩ pull-up at IC) |
| 14/15 | LIMIT_M1/M2 | Limit switch inputs (internal pull-up) |
| 19/20 | USB D−/D+ | Fixed USB PHY — no UART bridge needed |
| 35/36/37 | SPI MOSI/CLK/MISO | Shared: AD9833 (Mode 2) + TFT + XPT2046 (Mode 0) |
| 47/48 | RPi TX/RX | UART to Raspberry Pi at 3.3V, 921600 baud |

### Sensing System

**Ultrasound Attenuation Sensing (UAS):**
- AD9833 generates 1 MHz sine wave → OPA2354 Tx amp (G=4.9, 2.96Vpp output) → J1 SMA → transducer
- Received signal → J2 SMA → BAV99 protection → OPA2354 two-stage Rx (10× + 10× = 100× total) → BAT54 envelope detector → GPIO1
- Firmware reads attenuation ratio vs saline baseline to track particle size change

**Computer Vision:**
- Raspberry Pi Global Shutter Camera frames transmitted to YOLOv8 model
- Outputs: median particle diameter (µm) and IQR
- Backlit LED panel (5V, J10) illuminates syringe from behind for particle contrast

---

## Software Stack

| Layer | Platform | Language | Key Libraries |
|---|---|---|---|
| Computer vision | Raspberry Pi 5 | Python 3 | OpenCV, Ultralytics YOLOv8 |
| Firmware / PID | ESP32-S3 | C++ (Arduino) | Arduino-ESP32, NimBLE, TFT_eSPI |
| Touchscreen UI | ESP32-S3 | C++ | TFT_eSPI, LVGL (TBD) |
| BLE debug | ESP32-S3 | C++ | NimBLE |

**Critical firmware notes:**
- SPI2 is shared between AD9833, ILI9341/ST7789V, and XPT2046. Firmware **must** switch SPI mode (Mode 0 ↔ Mode 2) and clock speed (40MHz ↔ 2MHz) before each device transaction.
- UART1 TX and RX are both mapped to GPIO4 via the GPIO matrix in half-duplex mode for TMC2209 addressing.
- ADC1 (GPIO1–10) is safe to use while BLE is active. ADC2 (GPIO11–20) is unavailable for analog during BLE.

---

## Team Structure

| Sub-team | Responsibilities |
|---|---|
| **Mechanical** | Frame, syringe holders, motor mounts, 3D printed enclosure (FDM + SLA resin) |
| **Electrical** | KiCad schematic + PCB layout, component sourcing, board bring-up and testing |
| **Software** | ESP32-S3 firmware (PID, motor control, UAS), RPi CV pipeline (YOLOv8), TFT UI |

---

## Project Timeline

| Milestone | Week | Date (approx.) |
|---|---|---|
| System Requirements Review | 5 | ✅ Current |
| Recess week (build sprint) | 7 | Jul 2026 |
| PCB schematic freeze | End of 7 | Jul 2026 |
| System Design Review | 9 | Aug 2026 |
| Final Exhibition | 13 | Sep 2026 |

The PCB schematic must be finalised by end of Week 7 to meet the JLCPCB/PCBWay fabrication lead time before the System Design Review.

---

## Getting Started

### Firmware (ESP32-S3)

Requirements: Arduino IDE 2.x with arduino-esp32 board package, or PlatformIO.

```bash
cd firmware/esp32
# Open the .ino or platformio.ini in your IDE
# Target board: ESP32-S3 Dev Module (or configure for WROOM-1-N8)
# Flash via USB-C — no USB-UART bridge needed (native USB Serial/JTAG)
```

Hold `BOOT` button and press `RESET` for manual bootloader entry if auto-reset fails.

### Computer Vision Pipeline (Raspberry Pi 5)

```bash
cd software/cv-pipeline
pip install -r requirements.txt
python main.py
```

### Electrical

PCB files live in `hardware/electrical/`. Open with KiCad 8+. See [docs/EMBO_PCB_Design_Brief_v2.1.txt](docs/EMBO_PCB_Design_Brief_v2.1.txt) for the full schematic design spec including all component values, layout rules, and the pre-submission checklist.

---

## Key References

- Yamagami et al., *The Size of Gelatin Sponge Particles*, CardioVascular and Interventional Radiology, 2006
- Yamamoto et al., CVIR 1997 — liver necrosis vs particle size
- PMC8670118 — hemorrhoidal embolization outcomes
- TMC2209 Datasheet — StallGuard, SpreadCycle, UART addressing
- AD9833 Datasheet — DDS signal generator, SPI Mode 2
- OPA2354 Datasheet — 250MHz GBW dual op-amp, single-supply operation

---

*SUTD 30.007 Engineering Design Innovation — Team EMBO — 2026*

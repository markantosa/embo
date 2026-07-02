# EMBO — Embolization Particle Sizing System 

> SUTD 30.007 Engineering Design Innovation | Team EMBO | Week 7 of 13

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
│  │  - Global        │          │  - ILI9341 TFT UI            │ │
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

Where `N` is stroke count, `k` is a shear constant (measured empirically), `D₀` is initial particle size, and `D_min` is the minimum achievable size. The PID loop uses live measurements of `D(N)` to schedule remaining strokes.

---

## Repository Structure

```
EMBO/
├── README.md
├── .gitignore
│
├── docs/                              # Design documents and briefs
│   ├── EMBO_Project_Overview.txt      # Full project context for new members
│   ├── EMBO_PCB_Design_Brief_v2.5.txt # Complete electrical design spec (current)
│   ├── EMBO_PCB_Design_Brief_v2.5.docx
│   └── EMBO_Pinout_Cheatsheet.txt     # Quick GPIO and connector reference
│
├── hardware/
│   ├── electrical/                    # KiCad schematic + PCB layout
│   │   └── README.md
│   └── mechanical/                    # CAD files (SolidWorks / Fusion 360)
│       └── README.md
│
├── firmware/
│   └── esp32/                         # ESP32-S3 Arduino/C++ firmware (PlatformIO)
│       ├── README.md                  # Flash guide and peripheral notes
│       └── ../FIRMWARE_TODO.md        # Build-up task list + hardware checklist
│
├── software/
│   ├── cv-pipeline/                   # Raspberry Pi YOLOv8 vision pipeline
│   └── ui/                            # TFT touchscreen UI assets/logic
│
└── assets/                            # Images, diagrams, photos
```

---

## Hardware Overview

### Custom MCU Board (ESP32-S3) — v2.5

| Subsystem | Components |
|---|---|
| Microcontroller | ESP32-S3-WROOM-1-N8 (8MB flash, BLE 5.0, USB-C native programming) |
| Motor drivers | 2× BigTreeTech TMC2209 V1.x plug-in modules in 2×8 sockets (field-replaceable) |
| Ultrasound signal chain | AD9833 DDS → OPA2354 Tx (G=4.9, 2.96Vpp) → transducer → OPA2354 Rx (G=100) → BAT54 envelope → GPIO1 ADC |
| Display | ILI9341 SPI TFT + XPT2046 resistive touch via 20-pin IDC ribbon to breakout board |
| Power | 24V PSU → AO4407A reverse polarity protection → LM2596 buck (5V) → AMS1117 LDO (3.3V) |
| Comms | UART2 to Raspberry Pi (921600 baud, GPIO47/48); BLE UART debug stream (NimBLE) |

| | |
|:---:|:---:|
| ![Main PCB layout](assets/main%20PCB%20layout.png) | ![Main PCB 3D view](assets/main%20PCB%203D%20view.png) |
| *PCB layout* | *3D render* |

### GPIO Assignments (key signals)

| GPIO | Signal | Notes |
|---|---|---|
| 1 | UAS_ADC | ADC1_CH0 — envelope detector output |
| 4 | TMC_UART | Half-duplex UART1 to both TMC2209 via 1kΩ |
| 5/8 | STEP_M1/M2 | LEDC PWM — step pulse generation |
| 6/9 | DIR_M1/M2 | Motor direction |
| 7/10 | EN_M1/M2 | Motor enable (active LOW, 10kΩ pull-up at IC) |
| 14/15 | LIMIT_M1/M2 | Limit switch inputs (internal pull-up) |
| 19/20 | USB D−/D+ | Fixed USB PHY — no UART bridge chip needed |
| 35/36/37 | SPI MOSI/CLK/MISO | Shared: AD9833 (Mode 2) + ILI9341 + XPT2046 (Mode 0) |
| 47/48 | RPi TX/RX | UART2 to Raspberry Pi, 921600 baud |

### Display Breakout Board

ILI9341 TFT + XPT2046 touch controller on a separate board, connected to the main MCU board via a 20-pin IDC ribbon.

| | |
|:---:|:---:|
| ![Display breakout layout](assets/display%20breakout%20layout.png) | ![Display breakout 3D view](assets/display%20breakout%203D%20view.png) |
| *PCB layout* | *3D render* |

### Sensing System

**Ultrasound Attenuation Sensing (UAS):**
- AD9833 generates 1 MHz sine wave → OPA2354 Tx amp (G=4.9, 2.96Vpp output) → J1 SMA → transducer
- Received signal → J2 SMA → BAV99 protection → OPA2354 two-stage Rx (10× + 10× = 100× total) → BAT54 envelope detector → GPIO1
- Firmware reads attenuation ratio vs saline baseline to track particle size change

**Computer Vision:**
- Raspberry Pi Global Shutter Camera frames processed by YOLOv8 model
- Outputs: median particle diameter (µm) and IQR, sent to ESP32 over UART
- Diffused LED panel (5V, J8) backlit behind syringe for particle contrast

---

## Firmware Status

| Module | File | Status |
|---|---|---|
| TMC2209 UART, SpreadCycle, StallGuard | `src/motors.cpp` | ✅ Done |
| LEDC step generation, limit switch ISRs | `src/motors.cpp` | ✅ Done |
| Homing routine + stroke counter | `src/motors.cpp` | ✅ Done |
| AD9833 1MHz DDS, UAS ADC calibration | `src/uas.cpp` | ✅ Done |
| NimBLE wireless debug UART | `src/ble_debug.cpp` | ✅ Done |
| RPi UART receive + packet parser | `src/rpi_uart.cpp` | ⚠️ Parser stub |
| PID → motor stroke mapping | `src/pid.cpp` | ⚠️ Mapping stub |
| TFT display, encoder, buttons, touch | `src/ui.cpp` | ⚠️ Stub |

See [`firmware/FIRMWARE_TODO.md`](firmware/FIRMWARE_TODO.md) for the full task list and hardware bring-up checklist.

---

## Software Stack

| Layer | Platform | Language | Key Libraries |
|---|---|---|---|
| Computer vision | Raspberry Pi 5 | Python 3 | OpenCV, Ultralytics YOLOv8 |
| Firmware / PID | ESP32-S3 | C++ (Arduino) | TMCStepper, NimBLE, TFT_eSPI, AD9833 |
| Touchscreen UI | ESP32-S3 | C++ | TFT_eSPI |
| BLE debug | ESP32-S3 | C++ | NimBLE |

**Critical firmware notes:**
- SPI2 is shared between AD9833 (Mode 2, ~10MHz), ILI9341 (Mode 0, 20MHz max via ribbon), and XPT2046 (Mode 0, 2MHz). Each library switches mode per transaction via `SPI.beginTransaction()`.
- TMC2209 uses UART1 (GPIO4, half-duplex). Raspberry Pi uses UART2 (GPIO47/48). These are separate peripherals — do not reassign.
- ADC1 (GPIO1) is safe while BLE is active. ADC2 cannot be used for analog during BLE — no ADC2 pins are used for analog in this design.
- SpreadCycle must be written via UART to both TMC2209 modules at every boot (no SPREAD pin on BTT modules). BLE log confirms success.

---

## Team Structure

| Sub-team | Responsibilities |
|---|---|
| **Mechanical** | Frame, syringe holders, motor mounts, 3D printed enclosure (FDM + SLA resin) |
| **Electrical** | KiCad schematic + PCB layout, component sourcing, board bring-up and testing |
| **Software** | ESP32-S3 firmware (PID, motor control, UAS), RPi CV pipeline (YOLOv8), TFT UI |

---

## Project Timeline

| Milestone | Week | Status |
|---|---|---|
| System Requirements Review | 5 | ✅ Complete |
| Recess week — build sprint | 7 | ✅ Current |
| PCB schematic freeze | End of 7 | 🔲 Upcoming |
| System Design Review | 9 | 🔲 |
| Final Exhibition | 13 | 🔲 |

The PCB schematic must be finalised by end of Week 7 to meet the JLCPCB fabrication lead time before the System Design Review.

---

## Getting Started

### Firmware (ESP32-S3)

Requires VS Code + PlatformIO extension. See [`firmware/esp32/README.md`](firmware/esp32/README.md) for the full flash guide.

```bash
# Quick start
cd firmware/esp32
pio run --target upload   # build and flash
pio device monitor        # serial monitor at 115200
```

Hold `BOOT` + press `RESET` for manual bootloader entry if auto-reset fails.

### Computer Vision Pipeline (Raspberry Pi 5)

```bash
cd software/cv-pipeline
pip install -r requirements.txt
python main.py
```

### Electrical

PCB files live in `hardware/electrical/`. Open with KiCad 8+. The full design spec including component values, GPIO assignments, layout rules, and the pre-submission checklist is in [`docs/EMBO_PCB_Design_Brief_v2.5.txt`](docs/EMBO_PCB_Design_Brief_v2.5.txt).

---

## Key References

- Yamagami et al., *The Size of Gelatin Sponge Particles*, CardioVascular and Interventional Radiology, 2006
- Yamamoto et al., CVIR 1997 — liver necrosis vs particle size
- PMC8670118 — hemorrhoidal embolization outcomes
- TMC2209 Datasheet — StallGuard4, SpreadCycle, UART addressing
- AD9833 Datasheet — DDS signal generator, SPI Mode 2
- OPA2354 Datasheet — 250MHz GBW dual op-amp, single-supply operation

---

*SUTD 30.007 Engineering Design Innovation — Team EMBO — 2026*

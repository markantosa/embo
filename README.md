# EMBO — Embolization Particle Sizing System

> SUTD 30.007 Engineering Design Innovation | Team EMBO | Week 13 of 13

EMBO is a closed-loop medical device that automates and quality-controls the preparation of gelatin foam embolic agents for interventional radiology procedures. It is the first device of its kind to measure and control particle size during embolic agent preparation in real time.

| | |
|:---:|:---:|
| ![EMBO fully assembled](assets/EMBO%20fully%20assembled.jpg) | ![EMBO electrical assembly](assets/EMBO%20electrical%20assembly.jpg) |
| *Fully assembled device* | *Electrical assembly* |

**CAD renders**

| Isometric | Front | Sheet-metal enclosure |
|:---:|:---:|:---:|
| <img src="assets/embo%20iso.png" width="220"> | <img src="assets/embo%20side.png" width="220"> | <img src="assets/EMBO%20Sheet%20Metal%20Case.png" width="220"> |

---

## The Problem

Embolization procedures use tiny gelatin foam particles (embolic agents) to deliberately block blood vessels — treating tumours, fibroids, and bleeding. Before every procedure, a clinician manually prepares a particle slurry by pumping two syringes back and forth until the mixture reaches a "pudding consistency." There is no measurement, no standard, and no quality check. Two clinicians doing this side by side produce slurries with completely different particle size distributions. Particle size directly affects clinical outcomes: wrong sizes can cause tissue death in unintended areas or fail to block the target vessel.

**No device currently exists to measure or control particle size during this preparation step.** Gelfoam has been used since the 1970s with the preparation method unchanged.

---

## What EMBO Does

EMBO replaces manual syringe pumping with a controlled, sensor-guided system that:

1. **Automates mixing** — two stepper motors drive syringe plungers back and forth with precise speed, stroke, and cycle control
2. **Measures particle size** using two independent sensing modalities:
   - **Ultrasound attenuation (UAS)** — a 1 MHz acoustic signal chain measures how much sound energy the slurry absorbs; larger particles attenuate more. This is the **closed-loop** measurement the mixing stop condition actually runs on.
   - **Computer vision (verification only)** — a Raspberry Pi Zero 2W + OV9281 global-shutter mono camera captures a frame on demand and runs it through a Roboflow-hosted instance-segmentation model, reporting median particle size and IQR plus a segmentation-blob-annotated photo. This is an **optional, operator-triggered check**, not a continuous control input — see "Sensing System" below for why.
3. **Stops automatically** when the target particle size is reached, using a closed-loop control scheduler on the ESP32-S3 driven by the UAS delta-voltage size equation, adjusting stroke-by-stroke based on live sensor feedback. The target is adjustable per procedure (50–1000 µm, set via the touchscreen/encoder), since the ideal size depends on which vessels are being treated.

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        EMBO Device                              │
│                                                                 │
│  ┌──────────────────┐          ┌──────────────────────────────┐ │
│  │ Raspberry Pi     │◄─UART──►│     ESP32-S3 MCU Board       │ │
│  │ Zero 2W          │          │                              │ │
│  │  - Roboflow      │          │  - Scheduler control loop    │ │
│  │    hosted CV     │          │    (UAS delta-V closed loop) │ │
│  │    (on-demand)   │          │  - 2× TMC2209 motor drivers  │ │
│  │  - Particle size │          │  - UAS signal chain          │ │
│  │    + annotated   │          │  - ILI9341 TFT UI (LovyanGFX)│ │
│  │    photo         │          │  - BLE debug (NimBLE)        │ │
│  │  - OV9281 CSI    │          │                              │ │
│  │    mono camera   │          │                              │ │
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

Where `N` is stroke count, `k` is a shear constant (measured empirically), `D₀` is initial particle size, and `D_min` is the minimum achievable size. The mixing scheduler uses live measurements of `D(N)` (via the UAS delta-V equation) to decide when to stop.

---

## Repository Structure

```
EMBO/
├── README.md
├── .gitignore
│
├── docs/                              # Design documents and briefs
│   ├── EMBO_Project_Overview.md       # Project context — public/general audience
│   ├── EMBO_PCB_Design_Brief_v3_4.txt # Complete electrical design spec (current)
│   ├── EMBO_PCB_Design_Brief_v3_4.docx / .pdf
│   ├── EMBO_PCB_Design_Brief_v2.51.txt# Superseded — kept for reference
│   └── EMBO_Pinout_Cheatsheet.txt     # Quick GPIO and connector reference (being updated for v3.4)
│
├── hardware/
│   ├── electrical/                    # KiCad schematics + PCB layout — 2 boards:
│   │   │                              #   main board (v3.4, current) + display breakout
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
│   ├── SOFTWARE_TODO.md               # Original CV pipeline build-up task list (superseded, kept for reference)
│   ├── cv_verify/                     # CURRENT — on-demand CV verification, Pi Zero 2W
│   │   ├── SESSION_HANDOFF.md         # Latest working state, Roboflow wiring, hardware notes
│   │   ├── main.py                    # Request/reply loop: waits for CAPTURE, replies SIZE + annotated IMG
│   │   ├── capture.py / preprocessing.py / link.py / config.py
│   │   └── SETUP.md                   # Pi Zero 2W flash/SSH/dev setup
│   └── cv-pipeline/                   # Legacy — original continuous-loop design, Pi 5 + YOLOv8.
│       │                              # detection.py/sizing.py now live here and are imported by
│       │                              # cv_verify/main.py (loop-agnostic), but the driving loop itself
│       │                              # moved to cv_verify/ — see cv_verify/TODO.md for why.
│       └── README.md
│
├── testing/
│   ├── PCB_Test_Firmware_v3_4/        # v3.4 bench-test firmware + Web Bluetooth sensor dashboard
│   └── CV_Verify_UART_Prototype/      # Throwaway prototype that proved out the IMG-over-UART
│                                      # protocol ahead of real detection/sizing existing — since
│                                      # merged into firmware/esp32/ directly, kept for reference
│
└── assets/                            # Images, diagrams, photos
```

Note: TFT touchscreen UI code lives in `firmware/esp32/src/ui.cpp` and is tracked in `firmware/FIRMWARE_TODO.md` — there's no separate `software/ui/` folder, since that code only runs on the ESP32, not the Pi.

---

## Hardware Overview

### Main MCU Board (ESP32-S3) — v3.4

| Subsystem | Components |
|---|---|
| Microcontroller | ESP32-S3-WROOM-1-N4 (4MB flash, no PSRAM, BLE 5.0, USB-C native programming) — corrected from N8 in v3.3, no pinout impact |
| Motor drivers | 2× MKS TMC2209 V2.0 plug-in modules in 2×8 sockets, field-replaceable; MS1/MS2 jumpers (new v3.4) make both modules interchangeable across UART addresses |
| Ultrasound signal chain | AD9833 DDS → OPA2354 Tx (G=4.9, 2.96Vpp) → transducer → OPA2354 Rx (G=100, interstage AC-coupled) → BAT54 envelope → GPIO1 ADC |
| Turbidity sensing | APDS9960 (I2C 0x39, ALS transmission) + MAX30102 (I2C 0x57, backscatter), shared I2C bus |
| Force sensing | 2× load cell + HX711 amplifier, socketed, shared-clock bit-bang driver — hardware fallback for StallGuard SG_RESULT |
| Display | ILI9341 SPI TFT + XPT2046 resistive touch via 20-pin IDC ribbon to breakout board |
| Power | 24V PSU → F1 (off-board T5A fuse) → F2 (on-board 8A backstop, new v3.4) → AO4407A reverse polarity protection → LM2596 buck (5V) → AMS1117 LDO (3.3V) |
| Comms | UART2 to Raspberry Pi (921600 baud, GPIO47/48); BLE UART debug stream (NimBLE) |

v3.4 is a hardware-hardening pass: 12× 220Ω GPIO protection resistors on every ESP32-to-TMC2209 control line (added after a hot-swap/spark incident on the fabricated v2.51 board), the PDN_UART pin correction (module pin 5, not pin 4), and a full reference-designator naming pass. See [`hardware/electrical/README.md`](hardware/electrical/README.md) for the complete changelog.

| | | |
|:---:|:---:|:---:|
| ![v3.4 PCB front render](assets/EMBO%20Controller%20v3.4%20PCB%20Front.jpg) | ![v3.4 PCB back render](assets/EMBO%20Controller%20v3.4%20PCB%20Back.jpg) | ![v3.4 PCB trace routing, layers 1 and 4](assets/EMBO%20Controller%20Routing%20L1%20and%20L4%20%28L2%20GND%20and%20L3%203V3%20hidden%20for%20clarity%29.png) |
| *3D render, front* | *3D render, back* | *KiCad trace routing — L1 + L4 (L2 GND, L3 3V3 hidden for clarity)* |

| | |
|:---:|:---:|
| ![v3.4 PCB bring-up](assets/PCB%20v3.4%20bring%20up.jpg) | ![v3.4 PCB wired up](assets/PCB%20v3.4%20wired%20up.jpg) |
| *Fabricated board — bring-up* | *Fabricated board — wired up* |

### GPIO Assignments (key signals)

| GPIO | Signal | Notes |
|---|---|---|
| 1 | UAS_ADC | ADC1_CH0 — envelope detector output |
| 3/43 | I2C_SDA/SCL | Shared bus, APDS9960 (0x39) + MAX30102 (0x57), 400kHz |
| 4/44 | TMC_UART TX/RX | Half-duplex, shared PDN bus, via 1kΩ + 220Ω GPIO protection per module |
| 5/8 | STEP_M1/M2 | Hardware timer — step pulse generation, 220Ω GPIO protection |
| 6/9 | DIR_M1/M2 | Motor direction, 220Ω GPIO protection |
| 7/10 | EN_M1/M2 | Motor enable (active LOW, 10kΩ pull-up at IC), 220Ω GPIO protection |
| 14/15 | LIMIT_M1/M2 | Limit switch inputs (internal pull-up) |
| 19/20 | USB D−/D+ | Fixed USB PHY — no UART bridge chip needed |
| 21/37/42 | HX711_2_DT / SCK (shared) / HX711_1_DT | 2× load cell amplifiers, shared clock |
| 35/36 | SPI MOSI/CLK | Shared: AD9833 (Mode 2) + ILI9341 + XPT2046 (Mode 0) |
| 47/48 | RPi TX/RX | UART2 to Raspberry Pi, 921600 baud |

### Display Breakout Board — 2-layer PCB

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

**Computer Vision (verification, not closed-loop control):**
- Raspberry Pi Zero 2W + OV9281-110 global-shutter mono CSI camera — no dye/contrast agent added to the slurry (real medical-device material), so contrast comes entirely from dark-field/oblique illumination and software-side local-contrast enhancement
- Operator-triggered only: the ESP32 sends `CAPTURE` over UART, the Pi captures + median-stacks frames (noise reduction), runs instance segmentation via a Roboflow-hosted model (RF-DETR, trained on ~100 collected samples), and replies with `SIZE <median_um> <iqr_um>` plus a downscaled photo with segmentation blob outlines baked in — both rendered on the touchscreen's Camera Verify screen side by side, with an IN SPEC/OUT OF SPEC check
- **Not a fusion input** — unlike UAS, a CV result never drives the mixing stop condition; worst case for a failed/slow capture is a timeout notice, not a stalled run. See `software/cv_verify/TODO.md` for the full reasoning
- Diffused LED panel (5V, J8) backlit behind syringe for particle contrast

**Turbidity Sensing (added v3.2, hardened v3.4):**
- APDS9960 (I2C 0x39) reads ALS clear-channel transmission through the syringe; external LED hardwired on
- MAX30102 (I2C 0x57) reads backscatter using its own onboard 660/880nm LEDs
- Shared I2C bus (GPIO3/GPIO43, 400kHz); independent of the UAS acoustic path — a second, optical estimate of slurry turbidity
- Live readings viewable on the BLE **Telemetry Dashboard** (`firmware/esp32/web/`) as Optical Sensor 1/2, alongside every other sensor — see "Getting Started" below
- Like CV, not yet part of the mixing stop condition — the fusion calibration table (`calibration.cpp`) is still unfilled placeholder data for every channel, so only UAS drives the closed loop today

**Force Sensing (added v3.2):**
- 2× load cell + HX711 24-bit amplifier at the syringe plunger contact point
- Shared-clock, per-channel data line — custom bit-bang driver (standard HX711 libraries assume one dedicated clock per chip)
- Hardware fallback for TMC2209 StallGuard SG_RESULT if the UART-based reading proves unreliable

---

## Firmware Status

| Module | File | Status |
|---|---|---|
| TMC2209 UART, SpreadCycle, StallGuard | `src/motors.cpp` | ✅ Done |
| LEDC step generation, limit switch ISRs | `src/motors.cpp` | ✅ Done |
| Homing routine + stroke counter | `src/motors.cpp` | ✅ Done |
| AD9833 1MHz DDS, UAS ADC calibration | `src/uas.cpp` | ✅ Done |
| NimBLE wireless debug UART + binary telemetry service | `src/ble_debug.cpp`, `src/ble_binary_telemetry.cpp` | ✅ Done |
| RPi UART: SIZE reply parsing + on-demand IMG (annotated photo) receive | `src/rpi_uart.cpp` | ✅ Done |
| Mixing scheduler: adjustable setpoint (50–1000µm), closed-loop UAS delta-V stop condition | `src/scheduler.cpp` | ✅ Done — replaces the old PID design |
| TFT UI: encoder-driven menus, live verify screen (photo + PSD/IQR), start/stop-estop | `src/ui.cpp`, `src/ui/screens/` | ⚠️ Touch input still stub (encoder-only works) |
| Turbidity sensors (APDS9960 + MAX30102) | `src/turbidity.cpp` | ✅ Done — feeds the BLE Telemetry Dashboard; not yet a fusion input (calibration table unfilled) |

See [`firmware/FIRMWARE_TODO.md`](firmware/FIRMWARE_TODO.md) for the full task list and hardware bring-up checklist, and [`firmware/esp32/README.md`](firmware/esp32/README.md#operational-workflow) for the doctor-facing operational workflow (boot → home → set target → run → stop/e-stop).

---

## Software Stack

| Layer | Platform | Language | Key Libraries |
|---|---|---|---|
| Computer vision (on-demand verification) | Raspberry Pi Zero 2W | Python 3 | picamera2, Pillow, NumPy, `requests` (Roboflow hosted inference — see `software/cv_verify/detection.py`) |
| Firmware / mixing scheduler | ESP32-S3 | C++ (Arduino) | TMCStepper, NimBLE, LovyanGFX, AD9833 |
| Touchscreen UI | ESP32-S3 | C++ | LovyanGFX |
| BLE debug + telemetry | ESP32-S3 | C++ | NimBLE |

**Critical firmware notes:**
- SPI2 is shared between AD9833 (Mode 2, ~10MHz), ILI9341 (Mode 0, 20MHz max via ribbon), and XPT2046 (Mode 0, 2MHz). Each library switches mode per transaction via `SPI.beginTransaction()`.
- TMC2209 uses a half-duplex UART bus (GPIO4 TX / GPIO44 RX as of v3.4). Raspberry Pi uses UART2 (GPIO47/48). These are separate peripherals — do not reassign.
- ADC1 (GPIO1) is safe while BLE is active. ADC2 cannot be used for analog during BLE — no ADC2 pins are used for analog in this design.
- SpreadCycle must be written via UART to both TMC2209 modules at every boot (no SPREAD pin on the MKS V2.0 modules). BLE log confirms success.
- HX711 reads are pinned to core 1 and short-critical-sectioned — PD_SCK must never sit HIGH past ~60µs or the chip powers itself down mid-read.

---

## Team Structure

| Sub-team | Responsibilities | Members |
|---|---|---|
| **Mechanical** | Frame, syringe holders, motor mounts, 3D printed enclosure (FDM + SLA resin), DOE (Design of Experiments) | Alvin, Victoria, Tian Wen |
| **Electrical & Software** | KiCad schematics + PCB layout (main board + display breakout), component sourcing, board bring-up/testing, ESP32-S3 firmware (mixing scheduler, motor control, UAS), RPi CV verification pipeline (Roboflow), TFT UI | Vincent, Ren Jie, Audrey, Aditi |

---

## Project Timeline

| Milestone | Week | Status |
|---|---|---|
| System Requirements Review | 5 | ✅ Complete |
| Recess week — build sprint | 7 | ✅ Complete |
| PCB schematic freeze | End of 7 | ✅ Complete |
| System Design Review | 9 | ✅ Complete |
| Final Exhibition | 13 | ✅ Current |

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

### Computer Vision Pipeline (Raspberry Pi Zero 2W)

```bash
cd software/cv_verify
python3 -m venv .venv --system-site-packages   # --system-site-packages: sees apt-installed picamera2
source .venv/bin/activate
pip install -r ../requirements.txt
export ROBOFLOW_API_KEY=<your key>             # not stored in any committed file
python3 main.py
```

See [`software/cv_verify/SETUP.md`](software/cv_verify/SETUP.md) for full Pi Zero 2W bring-up (camera/UART dtoverlays, etc.) and [`software/cv_verify/SESSION_HANDOFF.md`](software/cv_verify/SESSION_HANDOFF.md) for current working state and known issues.

### Electrical

PCB files live in `hardware/electrical/` — two boards: the main board (`embo main MCU PCB v3_4/`, current revision) and the display breakout. Open with KiCad 10. The full design spec including component values, GPIO assignments, layout rules, BOM, and the pre-submission checklist is in [`docs/EMBO_PCB_Design_Brief_v3_4.txt`](docs/EMBO_PCB_Design_Brief_v3_4.txt).

### Telemetry Dashboard (real firmware)

`firmware/esp32/web/index_logging to record UAS csv data.html` is a Web Bluetooth dashboard for the actual production firmware (not the bench-test project below) — live UAS envelope, both load cells, and both optical/turbidity sensors (labeled Optical Sensor 1/2), plus motor jog/home controls and the automated UAS frequency sweep. Requires BLE to be enabled on-device first (**Settings → Developer mode → UAS debug mode**), and must be served over http(s) — Web Bluetooth silently fails under a `file://` URL:

```bash
cd firmware/esp32/web
python3 -m http.server 8000
# open http://localhost:8000/index_logging%20to%20record%20UAS%20csv%20data.html
```

### PCB Bench-Test Firmware + Dashboard

`testing/PCB_Test_Firmware_v3_4/` is a separate, standalone PlatformIO project for bringing up an assembled v3.4 board pre-final-firmware: reads every sensor (UAS envelope, both load cells, both turbidity channels, both StallGuard results, limit switches), drives the two steppers, and streams it all over BLE to its own Web Bluetooth dashboard (`web/index.html`) with live strip-charts alongside the raw numbers.

---

## Key References

- Yamagami et al., *The Size of Gelatin Sponge Particles*, CardioVascular and Interventional Radiology, 2006
- Yamamoto et al., CVIR 1997 — liver necrosis vs particle size
- PMC8670118 — hemorrhoidal embolization outcomes
- TMC2209 Datasheet — StallGuard4, SpreadCycle, UART addressing
- AD9833 Datasheet — DDS signal generator, SPI Mode 2
- OPA2354 Datasheet — 250MHz GBW dual op-amp, single-supply operation
- HX711 Datasheet (Avia Semiconductor) — 24-bit ADC, load-cell amplifier
- APDS-9960 Datasheet (Broadcom) — ALS/gesture/proximity sensor
- MAX30102 Datasheet (ADI/Maxim) — pulse oximetry / heart-rate, used here in backscatter mode

---

*SUTD 30.007 Engineering Design Innovation — Team EMBO — 2026*

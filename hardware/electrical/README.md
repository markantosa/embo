# Electrical — Custom MCU Board

KiCad schematic and PCB layout for the EMBO ESP32-S3 control board.

## Design spec

Full design specification, component values, GPIO assignments, layout rules, and pre-submission checklist:
[`/docs/EMBO_PCB_Design_Brief_v2.5.txt`](../../docs/EMBO_PCB_Design_Brief_v2.5.txt)

Quick GPIO and connector reference:
[`/docs/EMBO_Pinout_Cheatsheet.txt`](../../docs/EMBO_Pinout_Cheatsheet.txt)

## Quick reference

- **MCU:** ESP32-S3-WROOM-1-N8 (8MB flash, no PSRAM, PCB antenna)
- **Motor drivers:** 2× BigTreeTech TMC2209 V1.x plug-in modules in 2×8 sockets — field-replaceable, no QFN-28 hand-solder (v2.5 change)
- **Fabrication target:** JLCPCB, 2-layer FR4 ENIG, hot-plate reflow (AD9833/OPA2354 only — TMC2209 modules are hand-socketed)
- **Schematic tool:** KiCad 8+
- **Power rails:** 24V PSU → AO4407A reverse polarity protection → LM2596 buck (5V) → AMS1117 LDO (3.3V)

## v2.5 changes from v2.4

- TMC2209-LA QFN-28 ICs replaced by BTT TMC2209 V1.x plug-in modules in 2×8 sockets (SK1, SK2)
- RS1–RS4 current sense resistors removed (fixed 110mΩ sense resistor is on-module)
- VCP decoupling caps removed (on-module)
- SPREAD pull-up resistors removed — SpreadCycle is firmware-enforced via UART (`GCONF.en_spreadcycle=1`)
- CLK pin corrected to GND tie (was incorrectly documented as floating in v2.1–v2.4)

## Critical layout rules

- ESP32-S3 antenna keepout: 15mm clear on **all layers** including ground plane
- AMS1117 SOT-223 tab must connect to ≥100mm² copper pour with ≥4 thermal vias
- LM2596 catch diode (SS34A) within 5mm of SW pin, same layer, minimal loop area
- UAS analog traces on top layer only, no vias, routed over uninterrupted ground pour
- C_adc (100nF) within 2mm of GPIO1; C_env (10nF) distinct component from C_adc
- SK1/SK2: only PDN pin 4 (left column, 4th from top) routed to UART junction — pin 5 (spare PDN) must be NC

## TMC2209 module UART addressing

| Driver | Module | MS1 | MS2 | UART address |
|---|---|---|---|---|
| Motor 1 | U5 (SK1) | GND | GND | 0 |
| Motor 2 | U6 (SK2) | 3.3V via 10kΩ | GND | 1 |

Shared half-duplex bus on GPIO4 via R_UART (1kΩ) + R_PDN_UP (10kΩ pull-up to 3.3V).

## Screenshots

### Main MCU board

| | |
|:---:|:---:|
| ![Main PCB layout](../../assets/main%20PCB%20layout.png) | ![Main PCB 3D view](../../assets/main%20PCB%203D%20view.png) |
| *PCB layout* | *3D render* |

### Display breakout board

| | |
|:---:|:---:|
| ![Display breakout layout](../../assets/display%20breakout%20layout.png) | ![Display breakout 3D view](../../assets/display%20breakout%203D%20view.png) |
| *PCB layout* | *3D render* |

## Firmware bring-up dependency

SpreadCycle cannot be set by hardware (no SPREAD pin on BTT modules). Firmware **must** write `GCONF.en_spreadcycle=1` to both drivers at every boot and read back to confirm before trusting StallGuard data. See [`firmware/FIRMWARE_TODO.md`](../../firmware/FIRMWARE_TODO.md) for the full hardware bring-up checklist.

## Schematic deadline

PCB schematic must be finalised by **end of Week 7** for fab submission.

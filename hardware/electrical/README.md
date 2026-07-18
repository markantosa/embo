# Electrical — Custom MCU Board

KiCad schematic and PCB layout for the EMBO ESP32-S3 control board.

Current fabricated/in-progress revision: **v3.4** (`hardware/electrical/embo main MCU PCB v3_4/`).
The prior `embo main MCU PCB v2_5/` folder is kept for reference only — see
below for what changed between them.

## Design spec

Full design specification, component values, GPIO assignments, layout rules, BOM, and pre-submission checklist:
[`/docs/EMBO_PCB_Design_Brief_v3_4.txt`](../../docs/EMBO_PCB_Design_Brief_v3_4.txt)

Quick GPIO and connector reference:
[`/docs/EMBO_Pinout_Cheatsheet.txt`](../../docs/EMBO_Pinout_Cheatsheet.txt)
(being updated for v3.4 — treat the design brief above as authoritative in the meantime)

## Quick reference

- **MCU:** ESP32-S3-WROOM-1-N4 (4MB flash, no PSRAM, PCB antenna) — corrected from N8 in v3.3; no pinout impact, flash capacity only affects OTA partitioning
- **Motor drivers:** 2× MKS TMC2209 V2.0 plug-in modules in 2×8 sockets — field-replaceable, MS1/MS2 jumpers make both modules interchangeable across UART addresses 0–3 (new v3.4)
- **Turbidity sensing:** APDS9960 (I2C 0x39, ALS transmission) + MAX30102 (I2C 0x57, backscatter), shared I2C bus
- **Force sensing:** 2× load cell + HX711 amplifier, socketed, shared-clock — hardware fallback for StallGuard SG_RESULT
- **Signal generator:** AD9833 plug-in breakout module (VCC/DGND/SDATA/SCLK/FSYNC/AGND/OUT)
- **Fuse:** 5×20mm slow-blow glass, 5A 250V (T5A), off-board PRIMARY, inline before J14 — **new v3.4:** on-board 8A SMD backstop fuse F2, deliberately rated above F1 so it never coordinates against it
- **Fabrication target:** JLCPCB, 2-layer FR4 ENIG, hot-plate reflow (AD9833/OPA2354 only — TMC2209/HX711 modules are hand-socketed)
- **Schematic tool:** KiCad 10
- **Power rails:** 24V PSU → F1 (off-board) → F2 (on-board backstop) → AO4407A reverse polarity protection → LM2596 buck (5V) → AMS1117 LDO (3.3V)

## v3.4 summary of changes (from v3.3)

This revision is a hardware-hardening and naming-discipline pass, prompted by (a) a live TMC2209 hot-swap incident on the fabricated v2.51 board that sparked and left the ESP32 module overheating on every subsequent power-up, and (b) every passive finally getting an individual reference designator (full lookup table in the design brief §12.3), closing a BOM gap that had gone uncounted.

- **Full reference-designator naming pass** — every resistor, capacitor, diode, the inductor, and both LEDs now has an explicit, unique ref designator
- **On-board backstop fuse F2** — 8A SMD slow-blow (Littelfuse Nano2 452-series), in series after Q1, sized *above* the primary inline T5A so it never coordinates against it; only catches an empty/bypassed/mis-fitted F1 holder
- **TMC2209 UART pin correction** — physical inspection of the MKS V2.0 module's own silkscreen found pin 5 (not pin 4) is the direct PDN_UART node — the opposite of every version through v3.3, which followed generic third-party SilentStepStick documentation instead of this specific module's markings. Jumper default flipped on both U5 and U6.
- **MS1/MS2 address jumpers** — both modules gain 1×3+shunt jumpers on MS1 and MS2 (4 headers total), making U5/U6 fully interchangeable spares instead of hardwired at fixed addresses
- **GPIO short-fault protection** — a 220Ω series resistor on every ESP32-to-TMC2209 control line (STEP, DIR, EN, PDN_UART, MS1, MS2 × 2 modules = 12 resistors), added directly in response to the hot-swap/spark incident on the v2.51 board
- **UAS Tx/Rx hardening** — Tx gain resistor (RfO1_1, 390Ω) changes SMD → THT for field gain adjustment (fixed value, deliberately not a trimpot); Rx chain gains an interstage AC-coupling network ahead of the ×10.1 second stage
- **Component package/procurement changes** — TMC2209 VM bulk caps SMD radial (8mm, beside not under each socket); J14 screw terminal 3.5mm → 5.0mm pitch; boot/reset buttons SMD 6×6 → THT 6mm tactile
- **24V power LED wired** — taps the *protected* rail (Q1 Source), not raw input, so it only lights when Q1 is actually passing current correctly

See the design brief's revision history (§0) for the full v2.0 → v3.4 changelog, and §13 for the pre-submission checklist and open items carried forward from the fabricated v2.51 board (LM2596 buck fault workaround, TMC2209 hot-swap short diagnosis).

## Critical layout rules

- ESP32-S3 antenna keepout: 15mm clear on **all layers** including ground plane
- AMS1117 SOT-223 tab must connect to ≥100mm² copper pour with ≥4 thermal vias
- LM2596 catch diode (SS34A) within 5mm of SW pin, same layer, minimal loop area
- UAS analog traces on top layer only, no vias, routed over uninterrupted ground pour
- C_adc (100nF) within 2mm of GPIO1; C_envRC1 (10nF) distinct component from C_adc
- SK1/SK2: PDN routes to the jumper-selected pin (pin 5 direct = default, pin 4 via on-module R8 = fallback) through its 220Ω GPIO protection resistor — never shunt both jumper positions at once
- All 12 GPIO protection resistors (STEP/DIR/EN/PDN/MS1/MS2 × 2 modules) placed in series at the socket boundary, not upstream at the MCU
- TMC2209 VM bulk caps (100µF SMD radial, 8mm height) placed *beside* each socket, not underneath — clearance for the socket + module stack
- Q1 (AO4407A): Drain → input (J14 side), Source → load (24V rail) — reversed from v3.1, bench-verify ~0mA reverse-polarity draw before trusting the board
- Power off and allow VM bulk caps to fully discharge before inserting/removing any TMC2209 module — procedural rule added after the v2.51 hot-swap/spark incident

## TMC2209 module addressing

MS1/MS2 are field-jumpered as of v3.4 (previously hardwired), making U5 and U6 fully interchangeable spares:

| Driver | Module | MS1 jumper | MS2 jumper | UART address |
|---|---|---|---|---|
| Motor 1 | U5 (SK1) | GND (no shunt) | GND (no shunt) | 0 |
| Motor 2 | U6 (SK2) | 3.3V via 10kΩ (JMP_MS1_T2) | GND (no shunt) | 1 |

Shared half-duplex PDN_UART bus: GPIO4 (TX) / GPIO44 (RX) via R_UART (1kΩ) + R_PDN_UP (10kΩ pull-up to 3.3V), through each module's 220Ω GPIO protection resistor to its jumper-selected pin (pin 5 direct by default, §7.3 of the design brief).

## Screenshots

### Main MCU board — v3.4 (current)

| | | |
|:---:|:---:|:---:|
| ![v3.4 PCB front](../../assets/EMBO%20Controller%20v3.4%20PCB%20Front.jpg) | ![v3.4 PCB back](../../assets/EMBO%20Controller%20v3.4%20PCB%20Back.jpg) | ![v3.4 PCB editor view](../../assets/EMBO%20Controller%20v3.4%20PCB%20Editor%20View.jpg) |
| *Assembled board, front* | *Assembled board, back* | *KiCad PCB editor view* |

### Main MCU board — earlier render (v2.5)

| | |
|:---:|:---:|
| ![Main PCB layout](../../assets/main%20PCB%20layout.png) | ![Main PCB 3D view](../../assets/main%20PCB%203D%20view.png) |
| *PCB layout* | *3D render* |

### Display breakout board

Pinout and IDC ribbon interface unchanged since v2.4 — carried forward as-is into v3.4.

| | |
|:---:|:---:|
| ![Display breakout layout](../../assets/display%20breakout%20layout.png) | ![Display breakout 3D view](../../assets/display%20breakout%203D%20view.png) |
| *PCB layout* | *3D render* |

## Firmware bring-up dependency

SpreadCycle cannot be set by hardware (no SPREAD pin on the MKS V2.0 modules). Firmware **must** write `GCONF.en_spreadcycle=1` to both drivers at every boot and read back to confirm before trusting StallGuard data. See [`firmware/FIRMWARE_TODO.md`](../../firmware/FIRMWARE_TODO.md) for the full hardware bring-up checklist, and [`testing/PCB_Test_Firmware_v3_4/`](../../testing/PCB_Test_Firmware_v3_4/) for the v3.4 bench-test firmware and Web Bluetooth sensor dashboard.

## Open items (carried forward from the fabricated v2.51 board)

- **LM2596 (U2) buck fault** — external buck module workaround in place; U2 itself never bench-verified standalone
- **TMC2209 hot-swap short** — the ESP32 module is suspected damaged from the v2.51 hot-swap/spark incident; diagnosis in progress (unpowered 3V3-to-GND resistance check, ESP32 isolation)
- **AD9833 real output amplitude** — never measured on a scope; every UAS gain figure in the design brief rests on the datasheet-typical ~0.6Vpp, not a confirmed measurement

## Schematic status

v3.4 is a full rebuild in 8 wiring blocks (per team request) — see the design brief for the pre-submission checklist (§13) before sending to fab.

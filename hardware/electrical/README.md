# Electrical — Custom MCU Board

KiCad schematic and PCB layout for the EMBO ESP32-S3 control board.

## Design spec

Full design specification, component values, GPIO assignments, layout rules, and pre-submission checklist:
[`/docs/EMBO_PCB_Design_Brief_v2.1.txt`](../../docs/EMBO_PCB_Design_Brief_v2.1.txt)

## Quick reference

- **MCU:** ESP32-S3-WROOM-1-N8 (8MB flash, no PSRAM, PCB antenna)
- **Fabrication target:** JLCPCB / PCBWay, 2-layer FR4 ENIG
- **Schematic tool:** KiCad 8+
- **Power rails:** 24V (motors) → LM2596 5V → AMS1117 3.3V

## Critical layout rules

- ESP32-S3 antenna keepout: 15mm clear on **all layers** including ground plane
- AMS1117 SOT-223 tab must connect to ≥100mm² copper pour with ≥4 thermal vias — 850mW dissipation
- LM2596 catch diode (1N5822) within 5mm of SW pin, same layer, minimal loop area
- UAS analog traces on top layer only, no vias, routed over uninterrupted ground pour
- TMC2209 VCP cap (100nF) within 2mm of each IC's VCP pin — without it motors will not run

## Schematic deadline

PCB schematic must be finalised by **end of Week 7** for fab submission.

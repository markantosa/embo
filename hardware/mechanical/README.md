# Mechanical — Frame, Enclosure, and Fixtures

CAD files and fabrication drawings for the EMBO physical structure.

## What lives here

- SolidWorks / Fusion 360 assembly and part files
- 3D print files (FDM `.stl`, SLA `.stl`)
- Fabrication drawings

## Device overview

- Metal frame sized to sit on an interventional radiology trolley
- 3D printed (FDM) syringe holder and guide rails
- SLA resin outer cover
- 2× NEMA 17 stepper motors with lead screws — drive syringe plungers
- Mechanical limit switches at each end-of-travel (JST-XH 2-pin to PCB J6/J7)

## Inter-team dependencies

| Decision | Affects |
|---|---|
| Motor selection (torque, frame size) | Electrical: TMC2209 current setting (`irun`); PCB motor connector footprint |
| Lead screw pitch | Firmware: `HOMING_BACKOFF_STEPS` calibration in `config.h`; steps-per-mm for stroke length |
| Syringe holder geometry | Software: camera field of view and working distance |
| Enclosure height clearance | Electrical: BTT TMC2209 plug-in modules + socket stack is taller than original QFN-28 — confirm clearance |
| Enclosure cutouts | Electrical: PCB mounting holes, connector positions (J3 RPi UART, J13 USB-C, J14 PSU, IDC ribbon J9) |

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
- Mechanical NO microswitches at each end-of-travel (JST-XH to PCB J8/J9)

## Inter-team dependencies

| Decision | Affects |
|---|---|
| Motor selection (torque, frame size) | Electrical: TMC2209 current setting; PCB motor connector footprint |
| Syringe holder geometry | Software: camera field of view and working distance |
| Lead screw pitch | Firmware: steps-per-mm calibration constant |
| Enclosure cutouts | Electrical: PCB mounting holes, connector positions |

# EMBO Firmware Build-Up TODO

Generated from: PCB Design Brief v2.5, Pinout Cheatsheet, Project Overview, and firmware stub audit.

---

## Layer 1 — Hardware bring-up

> Nothing else works until these are done.

### 1. TMC2209 UART init + SpreadCycle enforcement `src/motors.cpp` ✅
- [x] Initialize TMCStepper on GPIO4, half-duplex, address 0 (U5) and address 1 (U6)
- [x] Write `GCONF.en_spreadcycle = 1` to both drivers at every boot
- [x] Read back GCONF/DRVSTATUS and assert the write succeeded (hardware verification gate)
- [x] Set `GCONF.I_scale_analog = 0` — disable trimpot current control
- [x] Configure `IHOLD_IRUN` via UART for consistent run-to-run current
- [x] Set `TCOOLTHRS = 0xFFFFF` — enables StallGuard across the full speed range
- [x] Fix: `pid_init()` added to `main.cpp` `setup()` (was missing)
- [x] Fix: `pid_start()` now seeds `_last_ms = millis()` to prevent integral spike on first update
- [x] Fix: `platformio.ini` corrected from `ST7789_DRIVER` → `ILI9341_DRIVER`

### 2. LEDC step output channels `src/motors.cpp` ✅
- [x] Configure LEDC ch0 → `PIN_STEP_M1`, ch1 → `PIN_STEP_M2`
- [x] Implement `motor_set_speed()` — set LEDC frequency, 50% duty; pass 0 to stop
- [x] Kill LEDC channel (duty=0) immediately inside limit switch ISRs

### 3. AD9833 DDS init `src/uas.cpp` ✅
- [x] Initialize AD9833 over SPI: GPIO38 CS, Mode 2 — bill2462 library handles mode switching per-transaction
- [x] SPI.begin() called with explicit pins before AD9833.begin() and before TFT_eSPI init
- [x] Configure 1MHz sine output on REG0
- [x] Wait 10ms after output enable before sampling baseline (>> 5× RC envelope τ=100µs)
- [x] ADC raw → mV via esp_adc_cal_characterize() + esp_adc_cal_raw_to_voltage()
- [x] HARDWARE NOTE: AD9833 MCLK assumed 25MHz — verify on first bring-up

---

## Layer 2 — Data pipelines

> Unblocks sensing and the RPi link.

### 4. RPi UART protocol parser `src/rpi_uart.cpp` ✅
- [x] Parse incoming `"SIZE <median_um> <iqr_um>\n"` packets via `sscanf`
- [x] CR stripped for robustness against Windows-style line endings from RPi
- [x] Unrecognised lines silently discarded — RPi can send status strings freely
- [x] Each valid packet logged over BLE: `"RPi: median=X iqr=Y um"`

### 5. UAS attenuation baseline `src/uas.cpp` ✅
- [x] Baseline sampled automatically at end of uas_init() after AD9833 settle
- [x] uas_get_baseline_mv() exposed for BLE debug sanity check on first bring-up
- [ ] HARDWARE GATE: verify attenuation ratio shifts measurably between air and slurry-filled syringe before trusting in PID

---

## Layer 3 — PID + motor integration

> The core closed-loop control.

### 6. PID → motor stroke mapping `src/pid.cpp`
- [ ] Map PID output to motor speed and/or stroke count
- [ ] Start tuning with P-only (KI=0, KD=0); current values KP=1.0, KI=0.1 are placeholders
- [ ] Implement stroke-count stop condition using the breakage kinetics model:
  `D(N) = D_min + (D_0 - D_min) × e^(-kN)`
  where N = stroke count and k = shear constant (to be measured on real hardware)

---

## Layer 4 — UI

> Can be developed in parallel with layers 2–3 once task 2 (LEDC) is done.

### 7. TFT screen content `src/ui.cpp`
- [ ] Draw splash / home screen
- [ ] Real-time status display: particle size (median + IQR), run/stop state, stroke count, UAS attenuation value

### 8. Rotary encoder quadrature decode `src/ui.cpp`
- [ ] Attach `CHANGE` interrupts to GPIO16 (EC11_A) and GPIO17 (EC11_B)
- [ ] Implement Gray-code quadrature decode in ISR
- [ ] Wire encoder to adjust target particle size setpoint or mixing speed

### 9. Button handling + debounce `src/ui.cpp`
- [ ] Debounce BTN1 (GPIO11), BTN2 (GPIO12), EC11_SW (GPIO18)
- [ ] Assign actions: run/stop and confirm

### 10. XPT2046 touch input `src/ui.cpp`
- [ ] Touch CS GPIO42, 2MHz, Mode 0 — different clock from ILI9341; handle carefully on shared SPI bus
- [ ] Calibration routine — map raw ADC coords to screen pixels
- [ ] Hit-test touch coords against UI button regions

### 11. Homing routine `src/motors.cpp` ✅
- [x] Both motors home simultaneously — drive at `HOMING_STEP_HZ` (500 Hz) toward limit
- [x] ISR kills LEDC on trip; main loop polls both flags with 30s timeout
- [x] Back-off: timed reverse at same speed for `HOMING_BACKOFF_STEPS / HOMING_STEP_HZ` ms
- [x] `motors_home()` called in `setup()` after all inits; `pid_update()` guards on `motors_is_homed()`
- [x] HARDWARE NOTE: `HOMING_FORWARD = false` assumed — verify limit switch end on real hardware

### 12. Stroke counter `src/motors.cpp` ✅
- [x] `motor_increment_stroke()` — PID calls after each complete forward+return cycle
- [x] `motor_get_stroke_count()` / `motor_reset_stroke_count()` — reset on homing
- [x] Counter reset to 0 at end of successful `motors_home()`

### 13. `pid_init()` in `main.cpp` `setup()` ✅ *(fixed in task 1 audit)*

---

## Verification gates

Complete these checks in order before closing each layer.

| After task | What to verify |
|---|---|
| 1 | SG_RESULT responds under manual motor load — required before PID tuning |
| 2 | Both motors step cleanly at target RPM; limit switch trips kill motion immediately |
| 3 | UAS ADC reading shifts measurably between air and slurry-filled syringe |
| 4 | BLE log shows correct median/IQR values parsed from live RPi packets |
| 5 | Attenuation ratio stable at baseline, changes monotonically as slurry thickens |
| 6 | Device completes a full auto-stop cycle on a water/gelatin test batch |

---

## Hardware checklist — things to confirm or calibrate on real hardware

Work through these in order. Items marked **BLOCKING** must be resolved before the system can run reliably.

### Before first power-on
- [ ] **BLOCKING** — TMC2209 VDD voltage: design brief §6.6 says "NOT CONFIRMED — 3.3V assumed safe." Check purchased module schematic before applying power.
- [ ] Motor connector pin order A1/A2/B1/B2: physically verify wiring harness matches J4/J5 pin order `OA1 NC OA2 OB1 NC OB2` (design brief §9).

### At first power-on (BLE log checks)
- [ ] **BLOCKING** — BLE log shows `"TMC addr 0: SpreadCycle OK"` and `"TMC addr 1: SpreadCycle OK"` at boot. If either says FAILED, stop — SG_RESULT data will be meaningless.
- [ ] BLE log shows `"UAS: init OK, baseline=XXX mV"` — confirm baseline is non-zero (>50 mV) with the AD9833 1MHz signal present.
- [ ] **BLOCKING** — AD9833 MCLK: assumed 25MHz in firmware (`config.h`). Confirm the actual clock source on the PCB. If MCLK differs, update the constructor argument in `uas.cpp:15`.

### Motor tuning
- [ ] `HOMING_FORWARD = false` (`config.h`): confirm which physical end of travel has the limit switch. Flip to `true` if motors jog the wrong way during homing.
- [ ] `HOMING_BACKOFF_STEPS = 200` (`config.h`): after homing, verify the plunger has fully cleared the limit switch. Increase if the switch stays triggered after back-off.
- [ ] `irun = 20` (~62% RMS, `motors.cpp`): run a syringe-loaded stroke cycle and check motor temperature. Reduce if hot, increase if losing steps.
- [ ] `ihold = 10` (~31% RMS, `motors.cpp`): reduce further if motors are warm while idle between strokes.

### Sensing validation
- [ ] **BLOCKING** — SG_RESULT under load: manually resist a motor shaft while stepping. Confirm `motor_sg_result()` value drops (lower = more load). Required before PID viscosity tuning.
- [ ] **BLOCKING** — UAS attenuation shift: fill syringe with saline, recalibrate baseline, then add gelatin slurry. Confirm `uas_get_attenuation()` drops below 0.95. If no change, check transducer coupling and envelope detector output on oscilloscope.

### PID calibration (after all BLOCKING items are cleared)
- [ ] Set `KI = 0.0`, `KD = 0.0`, start with P-only. Tune `KP` until the device converges on the particle size target without oscillating.
- [ ] Measure shear constant `k` from a real mixing run with CV camera comparison: fit `D(N) = D_min + (D_0 - D_min) × e^(-kN)` to measured data.
- [ ] Re-enable `KI` only after `k` is known and the stroke-count stop condition is implemented.

### Touch (after UI tasks complete)
- [ ] Run XPT2046 3-point calibration on first boot and store coefficients in ESP32-S3 NVS.

---

## Notes

- **SpreadCycle is required for StallGuard4.** The BTT TMC2209 V1.x module has no SPREAD pin — enforcement is firmware-only. If this write is skipped or fails, SG_RESULT data is meaningless and the PID viscosity input is corrupt.
- **SPI bus sharing:** AD9833 uses Mode 2 (~10MHz); ILI9341 uses Mode 0 (20MHz max via ribbon); XPT2046 uses Mode 0 (2MHz max). Always deassert all CS lines before switching devices.
- **ADC1 only.** UAS ADC is on GPIO1 (ADC1_CH0). ADC2 cannot be used during BLE — do not move UAS to any ADC2 pin.
- **UART assignment:** TMC2209 uses UART1 (GPIO4, half-duplex). RPi uses UART2 (GPIO47/48). Do not reassign either — they are different peripherals.
- **Suggested solo order:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10
- **Suggested split if two people:** person A does 1–6; person B does 7–10 starting after task 2 is done.

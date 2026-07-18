# EMBO Firmware Build-Up TODO

Generated from: PCB Design Brief v3.4, Pinout Cheatsheet, Project Overview, and firmware stub audit.

See [`../firmware/CALIBRATION.md`](CALIBRATION.md) for the calibration procedure behind every raw-sensor-to-physical-value constant referenced below — this file tracks build-up tasks, that one tracks measurement/tuning values.

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
- [x] *(historical)* `pid.cpp`'s init/seed fixes from the original audit — moot now that `pid.cpp` has been replaced by `scheduler.cpp` (Layer 3, task 6), kept here only as a record of what the old bug was
- [x] Fix: `platformio.ini` corrected from `ST7789_DRIVER` → `ILI9341_DRIVER` *(also moot — display library is now LovyanGFX, not TFT_eSPI, see Layer 4 task 7)*

### 2. LEDC step output channels `src/motors.cpp` ✅
- [x] Configure LEDC ch0 → `PIN_STEP_M1`, ch1 → `PIN_STEP_M2`
- [x] Implement `motor_set_speed()` — set LEDC frequency, 50% duty; pass 0 to stop
- [x] Kill LEDC channel (duty=0) immediately inside limit switch ISRs

### 3. AD9833 DDS init `src/uas.cpp` ✅
- [x] Initialize AD9833 over SPI: GPIO38 CS, Mode 2 — bill2462 library handles mode switching per-transaction
- [x] SPI.begin() called with explicit pins before AD9833.begin() and before the TFT (LovyanGFX) initializes — see the integration note in `uas_init()` about the two now being separate SPI driver paths sharing the same physical bus
- [x] Configure 1MHz sine output on REG0
- [x] Wait 10ms after output enable before sampling baseline (>> 5× RC envelope τ=100µs)
- [x] ADC raw → mV via esp_adc_cal_characterize() + esp_adc_cal_raw_to_voltage()
- [x] HARDWARE NOTE: AD9833 MCLK assumed 25MHz — verify on first bring-up
- [x] **UPDATE (July 2026 technical advisory)** — single-tone 1MHz reading replaced with a 3-point sweep (900kHz/1MHz/1.1MHz, `config.h`) each `uas_update()` cycle. Firmware/timing-only change, no board respin. See `docs/EMBO_UAS_CV_Technical_Advisory.txt` §1.
- [ ] HARDWARE GATE: the ±10% frequency spacing is a placeholder — verify it sits within the real transducer's -3dB bandwidth before trusting the off-center points. Widen/narrow `UAS_FREQ_HZ_0/2` in `config.h` if not.

---

## Layer 2 — Data pipelines

> Unblocks sensing and the RPi link.

### 4. RPi UART protocol parser `src/rpi_uart.cpp` ✅
- [x] Parse incoming `"SIZE <median_um> <iqr_um>\n"` packets via `sscanf`
- [x] CR stripped for robustness against Windows-style line endings from RPi
- [x] Unrecognised lines silently discarded — RPi can send status strings freely
- [x] Each valid packet logged over BLE: `"RPi: median=X iqr=Y um"`

### 5. UAS attenuation baseline `src/uas.cpp` ✅
- [x] Baseline sampled automatically at end of uas_init() after AD9833 settle, now per-frequency
- [x] uas_get_baseline_mv(freq_idx) exposed for BLE debug sanity check on first bring-up
- [x] **UPDATE (July 2026 technical advisory)** — UAS is formally demoted to a secondary/trend signal. The RPi CV pipeline is the sole authoritative size input to the mixing scheduler (`scheduler.cpp`); this is documented in code, not just here. See `docs/EMBO_UAS_CV_Technical_Advisory.txt` §1.
- [ ] **BLOCKING before UAS is used for anything beyond raw logging** — empirical correlation check: at several stroke counts, connect via BLE, run `UAS ON`, and log the per-frequency attenuation + live CV median/IQR line together (already wired up in `ble_debug.cpp`). Plot correlation. Only treat attenuation as a usable coarse/fast proxy if the relationship is clean and monotonic across the real operating range — otherwise leave it as diagnostic-only.
- [ ] HARDWARE GATE: verify attenuation ratio shifts measurably between air and slurry-filled syringe before trusting in anything downstream

---

## Layer 3 — Mixing scheduler + motor integration

> The core closed-loop control. **No longer PID** — see below.

### 6. Mixing scheduler `src/scheduler.cpp`, `src/calibration.cpp` ✅ structure in place
- [x] Replaced the PID loop (`pid.cpp`, deleted) with a receding-horizon batch/measure/refit
  scheduler — rationale in `scheduler.h`'s header comment: the breakage process is
  monotonic/irreversible (rules out an integral term) and the CV measurement is slow/noisy
  relative to the control action (rules out a derivative term).
- [x] `calibration.cpp` maintains an online linear-regression fit of the linearized
  breakage model (`ln(D-D_min) = ln(D0-D_min) - k*N`) from real `(strokeCount, median_um)`
  points as they arrive from the RPi.
- [x] Each batch is sized from the fit's predicted remaining strokes, reduced by
  `SCHED_UNDERSHOOT_FRACTION` (calibration.h) so a fresh measurement always happens
  before the model could authorize an overshoot.
- [ ] **BLOCKING before this is trustworthy** — every constant it depends on
  (`BREAKAGE_K_DEFAULT`, `BREAKAGE_D_MIN_UM`, the undershoot fraction, batch size bounds)
  is still a placeholder. See `CALIBRATION.md` §5-6 for the measurement procedure; do not
  run this on a real syringe until at least `D_min` and a first real `k` estimate exist.
- [ ] Force sensing (HX711) and StallGuard are deliberately NOT scheduler inputs — only
  the automatic e-stop path (`force_sensor.h`) and diagnostics. Do not wire them into
  `calib_predict_next_batch_strokes()` without redoing the same CV-correlation discipline
  UAS/turbidity are held to (see task 5 below and `CALIBRATION.md` §3).

---

## Layer 4 — UI

> Can be developed in parallel with layers 2–3 once task 2 (LEDC) is done.

### 7. TFT screen content `src/ui.cpp` ✅ loading-screen UI, rebuilt on LovyanGFX
- [x] **Design decision — loading screen, not a live dashboard.** The TFT shows only a set-target screen and a "Mixing…" loading screen with a simple spinner — no live PSD/IQR/stroke-count readout on-device. Raw sensor data and diagnostics are a BLE-only concern (`ble_debug.cpp`'s `FORCE`/`TURB`/`UAS`/`FIT` commands) by project decision, not something duplicated on-screen. See `README.md`'s Operational Workflow section.
- [x] Ported from `testing/display_ui_testing` onto **LovyanGFX** (`LGFX_Config.h`) — validated on the bench as the most responsive display library trialed, replacing the old TFT_eSPI setup.
- [x] States: set-target → running (loading) → done (result) → back to set-target. Plus a persistent error screen if homing fails at boot (`ui_show_error()`).
- [ ] Live PSD trend graph idea from the old TFT_eSPI single-screen design is **dropped**, not carried forward — it doesn't fit a loading-screen UI. If a future revision wants a graph, it belongs on the BLE dashboard side, not this screen.

### 8. Rotary encoder quadrature decode `src/ui.cpp` ✅
- [x] `CHANGE` interrupts attached to GPIO16 (EC11_A) and GPIO17 (EC11_B), shared ISR reading both pins
- [x] Buxton-table quadrature decode in ISR (`_isrEncoder()`, ported from `testing/display_ui_testing`'s `encoder_driver.cpp`), accumulates into a volatile delta consumed in `ui_update()`
- [x] Encoder adjusts target particle size setpoint, 5µm/detent, clamped to 50–1000µm (`config.h`); only active on the set-target screen
- [x] Encoder's own push-switch (EC11_SW, GPIO18) is the confirm/start input — see task 9, this replaces BTN1's old start function

### 9. Button handling + debounce `src/ui.cpp` ✅ BTN1 (stop/e-stop) + EC11_SW (confirm/start)
- [x] **Design decision — BTN1 is stop-only, no start function.** Previous firmware overloaded BTN1 with start (and used a since-removed BTN2 for stop). This build gives BTN1 exactly one job — short press = graceful stop (`scheduler_stop()`), held ≥`BTN1_LONGPRESS_MS` (800ms, `config.h`) = emergency stop (`scheduler_emergency_stop()`), fired the instant the hold threshold crosses rather than waiting for release.
- [x] EC11_SW (GPIO18) debounced separately — confirms setpoint + starts a run on the set-target screen, returns to set-target from the done screen. Only meaningful action while idle; no-op during a run.
- [x] Automatic HX711 over-force (`force_sensor_estop_tripped()`, `main.cpp` loop) calls the exact same `scheduler_emergency_stop()` as the button — one kill path, not two. See `CALIBRATION.md` §4/§8 for the force threshold, which is still a placeholder.

### 10. XPT2046 touch input — REMOVED FROM SCOPE
- [x] **Design decision — no touch UI.** Encoder + one dedicated stop button covers the full operational workflow (see README); touch calibration risk isn't worth taking on. `PIN_TOUCH_CS`/`PIN_TOUCH_DO` remain defined in `config.h` (the pins are physically present per the v3.4 board) but nothing in firmware drives them.

### 11. Homing routine `src/motors.cpp` ✅
- [x] Both motors home simultaneously — drive at `HOMING_STEP_HZ` (500 Hz) toward limit
- [x] ISR kills LEDC on trip; main loop polls both flags with 30s timeout
- [x] Back-off: timed reverse at same speed for `HOMING_BACKOFF_STEPS / HOMING_STEP_HZ` ms
- [x] `motors_home()` called in `setup()` after all inits; `scheduler_update()` and `scheduler_start()` both guard on `motors_is_homed()`
- [x] On homing failure, `main.cpp` now calls `ui_show_error()` instead of silently continuing — the board halts on a persistent fault screen rather than letting a run start on an un-trusted stroke position
- [x] HARDWARE NOTE: `HOMING_FORWARD = false` assumed — verify limit switch end on real hardware

### 12. Stroke counter `src/motors.cpp` ✅
- [x] `motor_increment_stroke()` — the scheduler calls this after each complete forward+return cycle
- [x] `motor_get_stroke_count()` / `motor_reset_stroke_count()` — reset on homing
- [x] Counter reset to 0 at end of successful `motors_home()`
- [x] Also the `N` in the breakage model (`calibration.cpp`'s `calib_breakage_add_point()`) — every CV measurement is tagged with the stroke count at the time it arrived

### 13. Turbidity + force sensor bring-up `src/turbidity.cpp`, `src/force_sensor.cpp` ✅ read paths in place
- [x] Turbidity: I2C bus init (GPIO3/43, 400kHz), APDS9960 ALS clear-channel read, MAX30102 FIFO backscatter read — both diagnostic-only, streamed via `TURB ON` (BLE)
- [x] Force: shared-clock HX711 bit-bang driver for both channels, raw counts converted via `calibration.h`, streamed via `FORCE ON` (BLE)
- [ ] **BLOCKING before either is more than a raw-count stream** — see `CALIBRATION.md` §3 (turbidity baselines) and §4 (HX711 tare/scale/e-stop threshold). None of these have been measured against real hardware yet.

---

## Verification gates

Complete these checks in order before closing each layer.

| After task | What to verify |
|---|---|
| 1 | SG_RESULT responds under manual motor load — required before trusting it for anything, even diagnostically |
| 2 | Both motors step cleanly at target RPM; limit switch trips kill motion immediately |
| 3 | UAS ADC reading shifts measurably between air and slurry-filled syringe |
| 4 | BLE log shows correct median/IQR values parsed from live RPi packets |
| 5 | Attenuation ratio stable at baseline, changes monotonically as slurry thickens |
| 6 | Device completes a full auto-stop cycle on a water/gelatin test batch, using real (not placeholder) `k`/`D_min` from `CALIBRATION.md` §5 |
| 13 | `FORCE ON`/`TURB ON` show plausible, stable numbers with the syringe empty; HX711 e-stop trips reliably on a manual obstruction test before trusting it unattended |

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
- [ ] **BLOCKING** — SG_RESULT under load: manually resist a motor shaft while stepping. Confirm `motor_sg_result()` value drops (lower = more load). It's diagnostic-only (not a scheduler input), but a broken StallGuard reading is still worth catching early.
- [ ] **BLOCKING** — UAS attenuation shift: fill syringe with saline, recalibrate baseline, then add gelatin slurry. Confirm `uas_get_attenuation(freq_idx)` drops below 0.95 at each swept frequency. If no change, check transducer coupling and envelope detector output on oscilloscope.
- [ ] **BLOCKING** — UAS-vs-CV correlation (July 2026 advisory, see task 5 above): do not skip this even if the attenuation shift check above passes — a monotonic *shift* existing is not the same as attenuation being *invertible* to a size value. Only promote UAS beyond diagnostic-only status if the correlation is clean.
- [ ] **BLOCKING** — Turbidity I2C scan: `TURB ON` shows both `als=...(ok)` and `...(ok)` for MAX30102. If either says `NOT FOUND`, check J15 wiring/pull-ups before anything else in that path.
- [ ] **BLOCKING** — HX711 tare/scale/e-stop: see `CALIBRATION.md` §4 in full — do not run an unattended mixing cycle until the e-stop threshold is set from real peak-force data, not a guessed round number.
- [ ] **Watch for RPi packet format changes.** The CV pipeline advisory recommends switching from bounding-box detection to instance segmentation and from a simple diameter to Equivalent Circular Diameter (+ Feret/solidity). If the RPi side adds fields beyond `median_um`/`iqr_um`, note that `rpi_uart.cpp`'s `sscanf(_buf, "SIZE %d %d", ...)` parser will silently ignore any extra trailing fields rather than erroring — new metrics (Feret, solidity) would arrive over UART but never reach firmware until the parser and `rpi_uart.h` getters are explicitly extended for them.

### Scheduler / breakage-model calibration (after all BLOCKING items above are cleared)

See `CALIBRATION.md` §5-6 for the full procedure — summary:
- [ ] Fit `k` and `D_min` from a real mixing run with CV camera comparison: `D(N) = D_min + (D_0 - D_min) × e^(-kN)`.
- [ ] Update `BREAKAGE_K_DEFAULT`/`BREAKAGE_D_MIN_UM` (`calibration.h`) with the real fitted values as the fallback used before the online fit has enough points of its own.
- [ ] Tune `SCHED_UNDERSHOOT_FRACTION` only after `k`'s run-to-run variance is understood — tightening it too early risks the exact overshoot the whole scheduler design exists to avoid.
- [ ] Re-fit per material — Lyostypt ≠ Gelfoam-equivalent (see the technical advisory); don't assume one `k` covers every test material.

### Touch — not applicable
Touch input was removed from scope (task 10) — no calibration needed.

---

## Notes

- **SpreadCycle is required for StallGuard4.** The MKS TMC2209 V2.0 module has no SPREAD pin — enforcement is firmware-only. If this write is skipped or fails, SG_RESULT data is meaningless (it's diagnostic-only now, not a scheduler input, but still worth getting right).
- **No PID.** `pid.cpp`/`pid.h` are deleted — replaced by `scheduler.cpp`/`scheduler.h` (Layer 3, task 6). See that file's header comment for why a PID was a poor fit for this specific process (monotonic/irreversible breakage + slow/noisy CV measurement).
- **SPI bus sharing:** AD9833 uses Mode 2 (~10MHz); the TFT (LovyanGFX) uses Mode 0 (20MHz max via ribbon). No touch/XPT2046 on this bus (task 10, removed from scope) and no MISO as of v3.3. Always deassert all CS lines before switching devices. See the integration note in `uas.cpp`'s `uas_init()` about the TFT and AD9833 now using separate SPI driver paths on the same physical GPIOs.
- **ADC1 only.** UAS ADC is on GPIO1 (ADC1_CH0). ADC2 cannot be used during BLE — do not move UAS to any ADC2 pin.
- **UART assignment:** TMC2209 uses UART1, GPIO4 TX / GPIO44 RX (genuinely separate pins as of v3.4, same shared bus node off-chip). RPi uses UART2 (GPIO47/48). Do not reassign either — they are different peripherals.
- **I2C:** turbidity sensors (APDS9960 0x39, MAX30102 0x57) share GPIO3/43, 400kHz — independent of every other bus on the board.
- **Two separate BLE services exist in this project** — this firmware's `EMBO-Debug` (Nordic UART, diagnostics only) and `testing/PCB_Test_Firmware_v3_4`'s `EMBO-PCB-Test-v3.4` (bench bring-up dashboard). They are not interchangeable — see `firmware/esp32/README.md`'s BLE debug commands section.
- **Suggested solo order:** 1 → 2 → 3 → 4 → 5 → 6 → 13 → 7 → 8 → 9 (10 is out of scope)
- **Suggested split if two people:** person A does 1–6 and 13; person B does 7–9 starting after task 2 is done.

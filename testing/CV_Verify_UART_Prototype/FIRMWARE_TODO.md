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
- [x] **CV moved to on-demand, single-shot verification.** `rpi_request_capture()` sends `"CAPTURE\n"`; the RPi is expected to capture one frame, run CV once, and reply with the same `SIZE` line — no longer a continuous stream. `rpi_capture_status()`/`rpi_pop_capture_result()` track pending/ready/timed-out state (`RPI_CAPTURE_TIMEOUT_MS`, `config.h`). See `ui.cpp`'s verification screen and `ble_debug.cpp`'s `CAPTURE` command.
- [ ] **RPi-side work still needed** (per user direction, not yet built): the RPi must actually implement the `CAPTURE` request/reply side of this protocol (capture-on-demand instead of whatever loop it runs today). Coordinate with `software/cv-pipeline/`.
- [ ] **Future work, deliberately not stubbed out yet:** an annotated JPEG (particle circles overlaid) alongside the `SIZE` reply, for display on a future UI revision. No wire format decided — see the closing comment in `rpi_uart.h`.

### 5. UAS attenuation baseline `src/uas.cpp` ✅
- [x] Baseline sampled automatically at end of uas_init() after AD9833 settle, now per-frequency
- [x] uas_get_baseline_mv(freq_idx) exposed for BLE debug sanity check on first bring-up
- [x] **UPDATE (this revision)** — UAS is now one of FOUR closed-loop sensor-fusion inputs
  (`scheduler.cpp`, task 6), not a diagnostic-only trend signal. It earns that status the
  same way the July 2026 advisory always said it would have to: an empirical correlation
  check, now made concrete as the 9-syringe bench calibration (`SENSOR_CAL_TABLE`,
  `calibration.cpp`) plus an automatic monotonicity gate at runtime
  (`calib_estimate_particle_size_um()`) that excludes this channel if its calibration curve
  isn't actually monotonic, rather than trusting it unconditionally.
- [ ] **BLOCKING before UAS is trusted as a fusion input** — the 9-syringe bench session
  (`CALIBRATION.md` §5) must actually run and populate `SENSOR_CAL_TABLE`'s `uasAttenuation`
  column. Until then this column is all zeros, which the monotonicity check correctly
  refuses to trust (see `FusedSizeEstimate.uasTrusted`) — so UAS is safely excluded from
  fusion by default, not silently wrong.
- [ ] Independently of the fusion gate, still worth running the UAS-vs-CV correlation check
  described previously: connect via BLE, run `UAS ON`, trigger a `CAPTURE` at several
  points in a real run, and log the per-frequency attenuation + that CV result together.
  This is now a cross-check on the fusion result, not the thing that decides whether UAS
  is used at all.
- [ ] HARDWARE GATE: verify attenuation ratio shifts measurably between air and slurry-filled syringe before trusting in anything downstream

---

## Layer 3 — Mixing scheduler + motor integration

> The core control logic. **Closed loop, on four live sensors — NOT on CV.** See below.

### 6. Mixing scheduler `src/scheduler.cpp`, `src/calibration.cpp` ✅ structure in place
- [x] **CV moved to optional, on-demand, single-shot verification** (per project
  direction) — the RPi only captures+runs CV when the ESP32 explicitly asks
  (`rpi_request_capture()`, task 4 above). It is NOT one of the scheduler's fusion inputs.
- [x] **Closed the loop on the OTHER four sensors instead** — UAS attenuation, APDS9960
  turbidity, MAX30102 turbidity, and load-cell force. Each is calibrated against a 9-point
  bench dataset (`SENSOR_CAL_TABLE`, `calibration.cpp` — known particle sizes from 9
  reference syringes) and inverted through its own curve into a size estimate; the
  estimates from channels that pass a monotonicity sanity check are fused (median) via
  `calib_estimate_particle_size_um()`. This directly replaced an earlier, wrong assumption
  (from a prior pass of this task) that removing CV meant going open-loop entirely — it
  didn't; it meant closing the loop on the sensors that were always meant to earn this role
  once real calibration data existed. See `scheduler.h`'s header comment for the full
  history and rationale.
- [x] Replaced the PID loop (`pid.cpp`, deleted) with a stroke-then-check hysteresis
  design, NOT a proportional/integral/derivative controller — the breakage process is
  still monotonic/irreversible (rules out a PID integral term regardless of how good the
  live measurement is). The scheduler strokes continuously and, after every stroke, checks
  the fused estimate; it stops once that estimate reads within `TARGET_TOLERANCE_UM`
  (config.h) of the target for `FUSION_CONSECUTIVE_CHECKS_REQUIRED` consecutive checks in a
  row (calibration.h) — debounced so one noisy reading can't stop an irreversible process
  early.
- [x] `MIXING_MAX_STROKES_SAFETY_CAP` (calibration.h) is a hard backstop: if the fused
  estimate never converges, the run stops anyway at this stroke count and
  `scheduler_hit_safety_cap()` reports true — a sign to check calibration, not a normal
  completion.
- [x] The breakage-kinetics model (`ln(D-D_min) = ln(D0-D_min) - k*N`, `calibration.cpp`)
  is KEPT but demoted to diagnostic-only — it no longer drives anything the scheduler
  does. It's still fed by operator-triggered camera verifications (`calib_breakage_add_point()`)
  and readable via the BLE `FIT` command, purely as a cross-check against the live fused
  estimate. `calib_breakage_reset()` is manual only (BLE `FIT RESET`), for when the
  material genuinely changes.
- [ ] **BLOCKING before this is trustworthy** — `SENSOR_CAL_TABLE` (calibration.cpp) is
  still all placeholder zeros. Every column reads as "not monotonic" against an all-zero
  table, which correctly means `calib_estimate_particle_size_um()` excludes every channel
  and returns `numChannelsUsed = 0` — i.e. the fusion safely refuses to estimate anything
  rather than producing a wrong number, but this also means the scheduler currently has NO
  way to stop except the safety cap. Run the full 9-syringe bench session
  (`CALIBRATION.md` §5, `ble_debug.cpp`'s `FUSION` command) before trusting this on a real
  mixing run.
- [ ] StallGuard remains NOT a fusion input (never validated as a size proxy, per the
  technical advisory) — it's diagnostic-only, same as before.
- [ ] Force sensing (HX711) is BOTH a fusion input AND an independent e-stop safety input
  (`calib_force_estop_tripped()`) — confirm the e-stop threshold work (`CALIBRATION.md`
  §4) is done separately from the fusion calibration; they're independent thresholds that
  happen to share the same sensor.

---

## Layer 4 — UI

> Can be developed in parallel with layers 2–3 once task 2 (LEDC) is done.

### 7. TFT screen content `src/ui.cpp` ✅ loading-screen UI + optional camera-verification screen
- [x] **Design decision — loading screen, not a live dashboard.** The TFT shows a set-target screen, a "Mixing…" loading screen with a simple spinner, a done screen, and (new) an on-demand camera-verification screen — no live PSD/IQR/stroke-count readout during a run. Raw sensor data and diagnostics are a BLE-only concern (`ble_debug.cpp`'s `FORCE`/`TURB`/`UAS`/`FIT`/`CAPTURE` commands) by project decision, not something duplicated on-screen. See `README.md`'s Operational Workflow section.
- [x] Ported from `testing/display_ui_testing` onto **LovyanGFX** (`LGFX_Config.h`) — validated on the bench as the most responsive display library trialed, replacing the old TFT_eSPI setup.
- [x] States: set-target → running (loading) → done (result) → back to set-target, with a `VERIFYING` state reachable from set-target or done (encoder long-press) that requests one RPi capture and shows median/IQR (or a timeout notice) — see task 9. Plus a persistent error screen if homing fails at boot (`ui_show_error()`).
- [ ] Live PSD trend graph idea from the old TFT_eSPI single-screen design is **dropped**, not carried forward — it doesn't fit a loading-screen UI. If a future revision wants a graph (or the annotated-JPEG display mentioned in task 4), it belongs alongside the verification screen, not as a continuous readout.

### 8. Rotary encoder quadrature decode `src/ui.cpp` ✅
- [x] `CHANGE` interrupts attached to GPIO16 (EC11_A) and GPIO17 (EC11_B), shared ISR reading both pins
- [x] Buxton-table quadrature decode in ISR (`_isrEncoder()`, ported from `testing/display_ui_testing`'s `encoder_driver.cpp`), accumulates into a volatile delta consumed in `ui_update()`
- [x] Encoder adjusts target particle size setpoint, 5µm/detent, clamped to 50–1000µm (`config.h`); only active on the set-target screen
- [x] Encoder's own push-switch (EC11_SW, GPIO18) is the confirm/start input — see task 9, this replaces BTN1's old start function

### 9. Button handling + debounce `src/ui.cpp` ✅ BTN1 (stop/e-stop) + EC11_SW (confirm/start/verify)
- [x] **Design decision — BTN1 is stop-only, no start function.** Previous firmware overloaded BTN1 with start (and used a since-removed BTN2 for stop). This build gives BTN1 exactly one job — short press = graceful stop (`scheduler_stop()`), held ≥`BTN1_LONGPRESS_MS` (800ms, `config.h`) = emergency stop (`scheduler_emergency_stop()`), fired the instant the hold threshold crosses rather than waiting for release.
- [x] EC11_SW (GPIO18) debounced separately, now with its own short/long distinction (`EC11_SW_LONGPRESS_MS`, 800ms) mirroring BTN1's pattern: short press confirms setpoint + starts a run on the set-target screen (or returns to set-target from the done/verify screens); held long triggers an optional camera verification (`rpi_request_capture()`) from the set-target or done screens. No-op during a run either way.
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
- [x] Turbidity: I2C bus init (GPIO3/43, 400kHz), APDS9960 ALS clear-channel read, MAX30102 FIFO backscatter read — both closed-loop fusion inputs (`scheduler.cpp`, task 6) as well as streamed via `TURB ON` (BLE) for diagnostics
- [x] Force: shared-clock HX711 bit-bang driver for both channels, raw counts converted via `calibration.h`, streamed via `FORCE ON` (BLE) — both a fusion input and an independent e-stop safety input
- [ ] **BLOCKING before either is more than a raw-count stream** — see `CALIBRATION.md` §3 (turbidity baselines) and §4 (HX711 tare/scale/e-stop threshold). None of these have been measured against real hardware yet, and the fusion table (task 6) needs both baselines locked first (baselines change the ratio calculation the fusion table's columns are built from).

---

## Verification gates

Complete these checks in order before closing each layer.

| After task | What to verify |
|---|---|
| 1 | SG_RESULT responds under manual motor load — required before trusting it for anything, even diagnostically |
| 2 | Both motors step cleanly at target RPM; limit switch trips kill motion immediately |
| 3 | UAS ADC reading shifts measurably between air and slurry-filled syringe |
| 4 | BLE log shows correct median/IQR values parsed after a `CAPTURE` request (or the UI's encoder long-press) — not automatically, since CV is on-demand now |
| 5 | Attenuation ratio stable at baseline, changes monotonically as slurry thickens |
| 6 | The BLE `FUSION` command shows all four channels as `trusted` (not `excluded`) once `SENSOR_CAL_TABLE` is filled in, and the fused estimate tracks a mixing run's actual progress sensibly (decreasing toward target, not jumping around). A full run stops on `scheduler_target_reached()` with `scheduler_hit_safety_cap() == false`, and a camera verification afterward confirms the achieved size is reasonably close to target. |
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
- [ ] **BLOCKING** — SG_RESULT under load: manually resist a motor shaft while stepping. Confirm `motor_sg_result()` value drops (lower = more load). It's diagnostic-only (not a fusion input), but a broken StallGuard reading is still worth catching early.
- [ ] **BLOCKING** — UAS attenuation shift: fill syringe with saline, recalibrate baseline, then add gelatin slurry. Confirm `uas_get_attenuation(freq_idx)` drops below 0.95 at each swept frequency. If no change, check transducer coupling and envelope detector output on oscilloscope.
- [ ] **BLOCKING** — Turbidity I2C scan: `TURB ON` shows both `als=...(ok)` and `...(ok)` for MAX30102. If either says `NOT FOUND`, check J15 wiring/pull-ups before anything else in that path.
- [ ] **BLOCKING** — HX711 tare/scale/e-stop: see `CALIBRATION.md` §4 in full — do not run an unattended mixing cycle until the e-stop threshold is set from real peak-force data, not a guessed round number.
- [ ] **Watch for RPi packet format changes.** The CV pipeline advisory recommends switching from bounding-box detection to instance segmentation and from a simple diameter to Equivalent Circular Diameter (+ Feret/solidity). If the RPi side adds fields beyond `median_um`/`iqr_um`, note that `rpi_uart.cpp`'s `sscanf(_buf, "SIZE %d %d", ...)` parser will silently ignore any extra trailing fields rather than erroring — new metrics (Feret, solidity) would arrive over UART but never reach firmware until the parser and `rpi_uart.h` getters are explicitly extended for them.

### Sensor-fusion calibration — the 9-syringe bench session (after all BLOCKING items above are cleared)

See `CALIBRATION.md` §5 for the full procedure — summary:
- [ ] For each of the 9 reference syringes (known particle size spanning the target range): load it, let readings settle, run the BLE `FUSION` command, and copy the printed `uas`/`apds`/`max`/`force` values into that syringe's row in `SENSOR_CAL_TABLE` (`calibration.cpp`) alongside its known size.
- [ ] After all 9 rows are filled in, run `FUSION` again on a couple of the same syringes and confirm each channel reports `trusted`, not `excluded` — if a channel is excluded, its 9-point curve isn't monotonic (check the raw values for that column; that sensor may not be usable for this material/size range).
- [ ] **BLOCKING before this is trustworthy** — this table hasn't been populated yet (all zeros). Do not run an unattended mixing cycle relying on fusion until it has, and until you've confirmed at least `FUSION_MIN_CHANNELS_REQUIRED` (2) channels come back trusted.
- [ ] Re-run per material — Lyostypt ≠ Gelfoam-equivalent (see the technical advisory); don't assume one calibration table covers every test material. There's no equivalent of `FIT RESET` for this table yet (it's a compile-time array) — swapping materials means re-flashing with a new table, or extending this to a runtime-editable table if that becomes a frequent need.
- [ ] Cross-check the fused estimate against the (diagnostic-only) breakage-model fit (BLE `FIT`) and against camera verifications (BLE `CAPTURE`) during early runs — if the fusion estimate and CV disagree substantially and consistently, something in the bench table is off.
- [ ] Sanity-check `MIXING_MAX_STROKES_SAFETY_CAP` (calibration.h) against a real full-range run once the table is populated — it should sit comfortably above what a legitimate run takes, but low enough to still catch a degenerate/miscalibrated setup.

### Touch — not applicable
Touch input was removed from scope (task 10) — no calibration needed.

---

## Notes

- **SpreadCycle is required for StallGuard4.** The MKS TMC2209 V2.0 module has no SPREAD pin — enforcement is firmware-only. If this write is skipped or fails, SG_RESULT data is meaningless (it's diagnostic-only, not a fusion input, but still worth getting right).
- **No PID, but very much closed-loop.** `pid.cpp`/`pid.h` are deleted — replaced by `scheduler.cpp`/`scheduler.h` (Layer 3, task 6), which closes the loop on a live, 4-sensor fused size estimate (UAS + 2x turbidity + force) and stops with a debounced hysteresis check, not a proportional/integral/derivative controller. CV sizing is the one thing that's optional and on-demand (task 4), used only as an independent double-check — see `scheduler.h`'s header comment for the full rationale and history (an earlier pass of this task mistakenly went open-loop/feedforward entirely when CV was removed from the loop; that was corrected once the actual intent — close the loop on the other four sensors instead — was clarified).
- **SPI bus sharing:** AD9833 uses Mode 2 (~10MHz); the TFT (LovyanGFX) uses Mode 0 (20MHz max via ribbon). No touch/XPT2046 on this bus (task 10, removed from scope) and no MISO as of v3.3. Always deassert all CS lines before switching devices. See the integration note in `uas.cpp`'s `uas_init()` about the TFT and AD9833 now using separate SPI driver paths on the same physical GPIOs.
- **ADC1 only.** UAS ADC is on GPIO1 (ADC1_CH0). ADC2 cannot be used during BLE — do not move UAS to any ADC2 pin.
- **UART assignment:** TMC2209 uses UART1, GPIO4 TX / GPIO44 RX (genuinely separate pins as of v3.4, same shared bus node off-chip). RPi uses UART2 (GPIO47/48). Do not reassign either — they are different peripherals.
- **I2C:** turbidity sensors (APDS9960 0x39, MAX30102 0x57) share GPIO3/43, 400kHz — independent of every other bus on the board.
- **Two separate BLE services exist in this project** — this firmware's `EMBO-Debug` (Nordic UART, diagnostics only) and `testing/PCB_Test_Firmware_v3_4`'s `EMBO-PCB-Test-v3.4` (bench bring-up dashboard). They are not interchangeable — see `firmware/esp32/README.md`'s BLE debug commands section.
- **Suggested solo order:** 1 → 2 → 3 → 4 → 5 → 6 → 13 → 7 → 8 → 9 (10 is out of scope)
- **Suggested split if two people:** person A does 1–6 and 13; person B does 7–9 starting after task 2 is done.

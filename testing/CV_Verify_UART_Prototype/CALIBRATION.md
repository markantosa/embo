# EMBO Firmware Calibration Reference

Single place to find **every value in the firmware that needs to be measured
or tuned on real hardware**, why it matters, how to measure it, and what
depends on it. Sensor-to-particle-size calibration is still an open,
actively-changing effort (per `docs/EMBO_UAS_CV_Technical_Advisory.txt`) —
this doc is meant to stay the working reference for that as new data comes
in, rather than have calibration notes scattered across `config.h` comments,
`FIRMWARE_TODO.md`, and people's heads.

**When you calibrate something, update the value here AND in the source
file it lives in — this doc is not a substitute for `config.h`, it's the
map to it.** If a value here and in code ever disagree, code wins and this
doc is stale — fix the doc.

Every entry follows the same shape: **What / Where / How to calibrate /
Depends on / Re-calibrate if**.

---

## 0. Calibration order

Do these in order — later stages assume earlier ones are already correct,
and re-doing an earlier one invalidates everything after it.

**This project is closed-loop on four live sensors (UAS, 2x turbidity,
force) — NOT on CV.** CV is an optional, operator-triggered, single-shot
verification (see §5's note and `scheduler.h`'s header comment for the full
history of why, including a wrong turn taken partway through this project
where CV's removal from the loop was briefly misread as "go open-loop
entirely" rather than "close the loop on the other four sensors instead").

1. Motor & homing (§1)
2. Force sensing — HX711 tare/scale, e-stop threshold (§4)
3. Turbidity baseline (§3)
4. UAS baseline + frequency spacing (§2)
5. StallGuard threshold, once current/homing are locked (§1.4)
6. **The 9-syringe sensor-fusion bench calibration (§5)** — this is the
   main event. Needs §1-4 locked first (their baselines/tare/scale feed
   directly into the ratios and grams values this table is built from).
7. Mixing stop-condition constants (§6) — needs §5's table populated
8. Breakage kinetics `k`/`D_min` (§7) — diagnostic-only cross-check now,
   not blocking, but still useful; can be done alongside or after §5
9. Target tolerance and safety thresholds (§8) — last, since it depends on
   the real noise characteristics of everything above

---

## 1. Motor & homing — `firmware/esp32/include/config.h`, `src/motors.cpp`

| Constant | Current value | What it controls |
|---|---|---|
| `HOMING_FORWARD` | `false` | Direction driven toward the limit switch |
| `HOMING_STEP_HZ` | 500 | Homing approach speed |
| `HOMING_BACKOFF_STEPS` | 200 | Steps backed off after limit trip |
| `HOMING_TIMEOUT_MS` | 30000 | Abort homing if no trip within this time |
| `irun` (motors.cpp) | 20 (~62% RMS) | Run current |
| `ihold` (motors.cpp) | 10 (~31% RMS) | Hold current between moves |

**How to calibrate:**
- **Homing direction/backoff:** jog manually via BLE test firmware
  (`testing/PCB_Test_Firmware_v3_4`) first — confirm which physical end
  each limit switch is on before trusting `HOMING_FORWARD`. Increase
  `HOMING_BACKOFF_STEPS` if the switch is still triggered after back-off.
- **Current (`irun`/`ihold`):** run a syringe-loaded stroke cycle, feel the
  motor case temperature after a few minutes. Reduce if hot to the touch,
  increase if steps are audibly/visibly lost under load. Confirm against
  the v3.4 board's 110mΩ sense-resistor ceiling (**~1.77A RMS max** —
  design brief §7.1) before pushing higher.

**Depends on:** nothing (do this first).
**Re-calibrate if:** lead screw pitch, motor model, or syringe/plunger
friction changes (mechanical team dependency — see
`hardware/mechanical/README.md`'s inter-team dependency table).

### 1.4 StallGuard threshold

Not yet a named constant — `motor_sg_result()` exists but no threshold is
wired to a decision yet. Needs its own calibration pass: manually resist a
motor shaft while stepping and record `SG_RESULT` at a few known loads,
with current (`irun`) already locked from above (SG_RESULT scales with
current, so this must be redone if `irun` changes). Cross-check against
HX711 force (§4) at the same load points — they're meant to be
independent, redundant load signals, so they should broadly agree.

---

## 2. Ultrasound attenuation (UAS) — `firmware/esp32/include/config.h`, `src/uas.cpp`

| Constant | Current value | What it controls |
|---|---|---|
| `UAS_FREQ_HZ_0/1/2` | 900k / 1.0M / 1.1M | 3-point sweep frequencies |
| `UAS_SETTLE_US` | 500 | Settle time after each frequency step before ADC read |
| baseline (runtime) | sampled at boot | Per-frequency 0%-attenuation reference |

**How to calibrate:**
- **Frequency spacing:** the ±10% spacing is a placeholder (see
  `config.h` comment). Scope the actual transducer's response and confirm
  900kHz/1.1MHz sit within its real -3dB bandwidth; widen/narrow if not.
- **Baseline:** re-sample with the syringe filled with plain saline (no
  particles) each time the transducer coupling/gel/mounting changes.
- **UAS becomes a trusted fusion input through §5's 9-syringe table, not
  through a CV correlation check.** The old plan was "prove UAS correlates
  with CV before trusting it as anything beyond diagnostic" — that's been
  superseded: UAS is now calibrated directly against known particle sizes
  (§5), and `calib_estimate_particle_size_um()` automatically excludes it
  from fusion if its 9-point curve isn't monotonic. A CV correlation check
  is still worth running (connect via BLE, `UAS ON`, trigger `CAPTURE`s
  across a run) as an independent cross-check, but it is no longer the
  gate that decides whether UAS gets used at all.

**Depends on:** nothing directly for the baseline/spacing calibration above;
the §5 bench session is what actually qualifies UAS as a fusion input.
**Re-calibrate if:** transducer, gel/coupling, or syringe geometry changes
(and re-run §5 afterward — a UAS baseline change shifts every attenuation
ratio the fusion table's `uasAttenuation` column was built from).

---

## 3. Turbidity sensing (APDS9960 + MAX30102) — `firmware/esp32/src/turbidity.cpp`

Two of the four sensor-fusion inputs (§5) — APDS9960 ALS transmission and
MAX30102 backscatter, read via `calib_turbidity_ratio_als()` /
`calib_turbidity_ratio_backscatter_ir()`.

| Value | What it controls | How to calibrate |
|---|---|---|
| APDS9960 `ATIME` / gain | ALS integration time / sensitivity | Currently 0xC0 (~103ms) / 4x gain (`turbidity.cpp`), chosen for a visible dynamic range with the external LED hardwired on — re-check once real slurry is in the light path, not just air |
| `APDS9960_BASELINE_CLEAR` (calibration.h) | 0%-turbidity reference | Sample with saline only, before any particles, same discipline as the UAS baseline above |
| MAX30102 LED current (RED/IR `LEDx_PA`) | Backscatter signal strength | `turbidity.cpp` uses ~7mA (`0x24`) as a starting point — raise if backscatter counts are too low against ambient noise, watch for saturation at the high end |
| `MAX30102_BASELINE_IR`/`_RED` (calibration.h) | 0%-turbidity reference | Same saline-first discipline |

**How to calibrate:** lock both baselines with the syringe filled with
plain saline (no particles) BEFORE running the §5 bench session — these
baselines are the denominator of the ratio that becomes `turbApdsRatio`/
`turbMaxRatio` in every row of `SENSOR_CAL_TABLE`, so changing a baseline
after the table is filled in invalidates the whole table.

**Depends on:** I2C bus bring-up (`turbidity_init()` — confirms both 0x39
and 0x57 respond).
**Re-calibrate if:** external LED (APDS9960) intensity/positioning changes,
or syringe/enclosure light-sealing changes (hardwired LED removes software
ambient subtraction — see design brief §9) — and re-run §5 afterward.

---

## 4. Force sensing (HX711 × 2) — `firmware/esp32/src/force_sensor.cpp`

One of the four sensor-fusion inputs (§5) AND an independent e-stop safety
input (`calib_force_estop_tripped()`) — two separate roles sharing one
sensor, calibrated independently of each other.

| Value | What it controls | How to calibrate |
|---|---|---|
| Tare offset (per channel) | Zero-force raw-count baseline | With the syringe plunger unloaded (no mixing load), record the raw count average over a few seconds; subtract this offset from all future readings |
| Scale factor (counts → grams, per channel) | Converts raw counts to a physical force/mass unit | Hang a known reference mass (or press with a calibrated force gauge) at the load cell's actual mounting point, record raw counts, divide by the known force — repeat at 2-3 points to check linearity |
| **E-stop force threshold** | Automatic emergency stop trigger, independent of the mixing schedule | Run several normal mixing cycles first and record the peak force seen in *normal* operation; set the threshold with headroom above that peak (e.g. the load cell's rated capacity or a clear outlier level) — must never trip during a routine run, but must catch a genuine jam/obstruction fast |

**How to calibrate:**
1. Tare both channels with the plunger mechanically at rest.
2. Scale-factor both channels against a known reference load — do this
   per-channel, load cells are rarely identical.
3. Collect a set of "normal run" peak-force samples across several full
   mixing cycles before picking the e-stop threshold — do not guess a
   round number.
4. Bench-verify the e-stop path itself: manually obstruct the plunger (at
   low current/speed first) and confirm the firmware actually cuts motor
   power before the mechanical stall becomes a problem.

**Depends on:** motor/homing (§1) being locked, so "normal" peak force is
measured under a stable, repeatable motion profile.
**Re-calibrate if:** load cell model, mounting bracket, or syringe/plunger
contact geometry changes (see `hardware/mechanical/README.md`'s inter-team
dependency table — mechanical changes here are electrical/firmware's
problem too).

---

## 5. Sensor-fusion calibration table — the main event

**This is the calibration that actually makes the machine closed-loop.**
Four independent live sensors — UAS attenuation, APDS9960 turbidity,
MAX30102 turbidity, load-cell force — are each calibrated against a bench
dataset of **9 syringes of slurry at known, set particle sizes** spanning
the target range. Each sensor's own 9-point (reading → known size) curve is
used to invert a live reading into a size estimate; the estimates from
channels whose curve passes a monotonicity check are fused (median) into
one number the scheduler compares against the operator's target after every
stroke. See `scheduler.h`'s header comment and `calibration.h`'s
`SensorCalibrationPoint`/`calib_estimate_particle_size_um()`.

**CV is deliberately NOT part of this table.** It's a separate, optional,
operator-triggered verification (§7's note, `rpi_uart.h`) — good as an
independent double-check specifically because it counts real particles
rather than inferring from a bulk sensor proxy, but slower and historically
less trustworthy as a *continuous* signal (see
`docs/EMBO_UAS_CV_Technical_Advisory.txt`).

| Table (in `firmware/esp32/src/calibration.cpp`) | Current state |
|---|---|
| `SENSOR_CAL_TABLE[9]` | **All zeros — placeholder.** Every row needs to be filled in from the bench session below. |

**How to calibrate:**
1. Lock §1 (motor/homing), §3 (turbidity baselines), and §4 (force tare/
   scale) FIRST — this table's columns are built directly from those
   sensors' calibrated outputs (ratios and grams), not raw counts, so
   recalibrating any of them after this table is filled in invalidates it.
2. Prepare 9 reference syringes of slurry, each at a distinct known
   particle size spanning the full target range (e.g. roughly evenly
   spaced across 50-1000µm, matching `TARGET_SIZE_UM_MIN/MAX`).
3. For each syringe: load it into the machine, let readings settle, then
   run the BLE `FUSION` command (`ble_debug.cpp`). It prints the live
   `uas`/`apds`/`max`/`force` values — copy these, plus that syringe's known
   size, into one row of `SENSOR_CAL_TABLE`.
4. After all 9 rows are filled in, reflash, and re-run `FUSION` on a couple
   of the same syringes as a sanity check — confirm the estimate is
   reasonably close to that syringe's known size and that the channels you
   expect to be usable report `trusted` (see the `FusedSizeEstimate`
   fields), not `excluded`.
5. If a channel comes back `excluded` (its 9-point curve isn't monotonic),
   look at the raw values for that column across the 9 rows — either that
   sensor genuinely doesn't track size well for this material (exclude it
   going forward, it's fine to run fusion on fewer than 4 channels as long
   as `FUSION_MIN_CHANNELS_REQUIRED` is met) or a measurement was fumbled
   and worth redoing.

**Depends on:** §1, §3, §4 all locked first.
**Re-calibrate if:** material changes (Lyostypt ≠ Gelfoam-equivalent, per
the advisory — don't assume one table covers every material), any upstream
baseline/tare/scale changes (§1/§3/§4), or transducer/sensor
mounting/positioning changes. There's no runtime equivalent of `FIT RESET`
for this table yet (it's a compile-time array) — re-calibrating means
re-running the bench session and reflashing.

---

## 6. Mixing stop condition — debounce + safety cap

Replaces the plain PID in `pid.cpp`. The scheduler strokes continuously and
checks the fused estimate (§5) after every stroke — see `scheduler.h`'s
header comment for why this is a hysteresis-style stop, not a PID, even
with a live measurement now available (the process is still monotonic/
irreversible, which is what actually rules out PID, independent of
measurement quality).

| Constant | Current value | What it controls |
|---|---|---|
| `FUSION_CONSECUTIVE_CHECKS_REQUIRED` (calibration.h) | 3 | How many consecutive within-tolerance stroke-checks are required before the scheduler actually stops — debounces a single noisy reading |
| `FUSION_MIN_CHANNELS_REQUIRED` (calibration.h) | 2 | Refuses to trust a fused estimate built from fewer channels than this |
| `MIXING_MAX_STROKES_SAFETY_CAP` (calibration.h) | 2000 | Hard backstop — stops the run at this stroke count regardless of the fused estimate, flagging `scheduler_hit_safety_cap()`, if fusion never converges |

**How to calibrate:**
- **`FUSION_CONSECUTIVE_CHECKS_REQUIRED`:** start conservative (3+). Lower
  only once §5's fused estimate is observed to be stable/low-noise on a
  static sample; raise if you see the scheduler stopping early on what
  turns out to be a noise blip.
- **`FUSION_MIN_CHANNELS_REQUIRED`:** 2 is a reasonable floor (never stop on
  a single sensor's say-so for an irreversible process). Raise toward 3-4
  once you know how many channels reliably come back `trusted` in
  practice.
- **`MIXING_MAX_STROKES_SAFETY_CAP`:** sanity-check against a real
  full-range run (target near `TARGET_SIZE_UM_MIN` from a fresh syringe) —
  must sit comfortably above what a legitimate run takes, but low enough to
  still catch a genuinely miscalibrated setup running forever.

**Depends on:** §5's table populated with enough real data that "converges"
is a meaningful concept at all.

---

## 7. Breakage kinetics — `D(N) = D_min + (D0 - D_min)·e^(-k·N)` (diagnostic only)

**This model does NOT drive the stop condition** — §5/§6 do that now. It's
kept as an independent cross-check ("the model expects roughly N more
strokes based on stroke count alone; do the live sensors agree?") and
because operator-triggered camera verifications still feed it.

| Constant | Current value | What it controls |
|---|---|---|
| `k` | **not yet measured** | Shear/breakage rate constant |
| `D_min` | **not yet measured** | Minimum achievable particle size for this material/process — the model's floor |

**How to calibrate:** trigger a camera verification (`ui.cpp`'s encoder
long-press, or BLE `CAPTURE`) at several points through a real mixing
run — each result automatically feeds `calib_breakage_add_point()`, and the
firmware's own online fit (`calib_breakage_get_fit()`) refits `k`/`D0`
after every point; read it back anytime via the BLE `FIT` command. Points
accumulate across runs by default — use BLE `FIT RESET` when the material
changes.

**Depends on:** an on-demand CV capture path working, locked motor behavior (§1).
**Re-calibrate if:** material changes, or the CV pipeline's sizing metric
changes (e.g. simple diameter → ECD, per the advisory) — use `FIT RESET`.

---

## 8. Target tolerance & safety thresholds

| Constant | Current value | What it controls |
|---|---|---|
| `TARGET_TOLERANCE_UM` | 25 | Used TWICE: (a) the width band `calib_estimate_particle_size_um()`'s result must sit within, for `FUSION_CONSECUTIVE_CHECKS_REQUIRED` checks (§6), to actually stop a run; (b) color-codes an operator-triggered verification result IN SPEC / OUT OF SPEC (`ui.cpp`) |
| `TARGET_SIZE_UM_MIN/MAX/STEP` | 50 / 1000 / 5 | Encoder-adjustable setpoint range |
| HX711 e-stop threshold | see §4 | Automatic emergency stop |
| StallGuard e-stop threshold | see §1.4 | (if adopted as a second automatic e-stop input) |

**How to calibrate `TARGET_TOLERANCE_UM`:** this is currently a
placeholder — it must be set from the FUSED ESTIMATE's actual measurement
noise (repeatability of `calib_estimate_particle_size_um()` on a static,
unchanging sample — check via repeated `FUSION` commands), not picked
arbitrarily and not based on CV noise (CV is a separate, secondary check
now, see §7). If the tolerance is tighter than the fusion noise floor, the
scheduler will either run forever (never gets `FUSION_CONSECUTIVE_CHECKS_
REQUIRED` in a row) or the verification screen will flag good results as
"OUT OF SPEC" on noise alone.

**Depends on:** everything above — this is deliberately last.

---

## 9. Button / e-stop behavior (firmware-side, not a numeric calibration)

Not a value to measure, but a design decision already locked in
`ui.cpp`/`scheduler.cpp` — noted here since it interacts directly with §4's
e-stop threshold:

- **BTN1 is dedicated to stop/e-stop only, no start function.** Short press
  = graceful stop (finishes the current stroke, then holds); held ≥
  `BTN1_LONGPRESS_MS` = true e-stop (kills motor power immediately,
  mid-step). Start/confirm lives on the encoder's own push-switch instead
  — see `firmware/esp32/README.md`'s Operational Workflow.
- The HX711 force e-stop (§4) uses the **same code path**
  (`scheduler_emergency_stop()`) as the button e-stop, not a separate one —
  one authoritative "kill everything now" function, called from both the
  button ISR and the force-threshold check in `main.cpp`'s loop.

---

## Change log

Update this section whenever a value in this doc is actually calibrated
from real data (not just a placeholder default) — a running record of
what's real vs. still assumed.

| Date | Value | Old | New | Basis |
|---|---|---|---|---|
| — | *(none yet — every value above is still a placeholder or unmeasured)* | | | |

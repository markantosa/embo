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

1. Motor & homing (§1)
2. Force sensing — HX711 tare/scale, e-stop threshold (§4)
3. Turbidity baseline (§3)
4. UAS baseline + frequency spacing (§2)
5. StallGuard threshold, once current/homing are locked (§1.4)
6. Breakage kinetics constant `k` and `D_min` (§5) — needs a working CV
   pipeline and a locked motor/force setup, since it's fit from real
   mixing runs
7. Batch scheduler constants (§6) — needs `k` from §5
8. Target tolerance and safety thresholds (§7) — last, since it depends on
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
- **CV correlation gate — do this before trusting UAS for anything beyond
  raw logging.** At several stroke counts, log CV median/IQR alongside
  raw per-frequency attenuation (already wired in `ble_debug.cpp` via
  `UAS ON`). Plot attenuation vs. CV median. Per the July 2026 advisory,
  UAS is a **secondary/trend signal only** — do not promote it to a
  scheduler input unless this relationship is clean and monotonic across
  the real operating range.

**Depends on:** nothing directly, but the CV correlation check needs the
CV pipeline working (`software/cv-pipeline/`).
**Re-calibrate if:** transducer, gel/coupling, or syringe geometry changes.

---

## 3. Turbidity sensing (APDS9960 + MAX30102) — added v3.2, not yet ported into `firmware/esp32`

These sensors exist on the v3.4 board and are exercised in
`testing/PCB_Test_Firmware_v3_4/src/hal/turbidity.cpp`, but the main
firmware (`firmware/esp32/`) doesn't read them yet — this section is a
placeholder for values to add to `config.h` once that port happens.

| Value (planned) | What it controls | How to calibrate |
|---|---|---|
| APDS9960 `ATIME` / gain | ALS integration time / sensitivity | Currently 0xC0 (~103ms) / 4x gain in the test firmware, chosen for a visible dynamic range with the external LED hardwired on — re-check once real slurry is in the light path, not just air |
| APDS9960 clear-channel baseline | 0%-turbidity reference | Sample with saline only, before any particles, same discipline as the UAS baseline above |
| MAX30102 LED current (RED/IR `LEDx_PA`) | Backscatter signal strength | Test firmware uses ~7mA (`0x24`) as a starting point — raise if backscatter counts are too low against ambient noise, watch for saturation at the high end |
| MAX30102 baseline (RED/IR) | 0%-turbidity reference | Same saline-first discipline |

**How to calibrate (once ported):** same empirical-correlation discipline
as UAS (§2) — this is a second, independent optical estimate of slurry
turbidity, not yet validated against real particle-size data. Log clear/IR/RED
alongside CV median/IQR across a real mixing run before trusting it for
anything beyond a diagnostic trend line on the BLE dashboard.

**Depends on:** I2C bus bring-up (`turbidityInit()` — confirms both 0x39
and 0x57 respond).
**Re-calibrate if:** external LED (APDS9960) intensity/positioning changes,
or syringe/enclosure light-sealing changes (hardwired LED removes software
ambient subtraction — see design brief §9).

---

## 4. Force sensing (HX711 × 2) — added v3.2, not yet ported into `firmware/esp32`

Currently only exercised in `testing/PCB_Test_Firmware_v3_4/src/hal/force_sensor.cpp`,
which reads **raw 24-bit signed counts** — no tare or grams conversion yet.
Needed before force can drive anything beyond a raw diagnostic number.

| Value (planned) | What it controls | How to calibrate |
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

## 5. Breakage kinetics — `D(N) = D_min + (D0 - D_min)·e^(-k·N)`

| Constant | Current value | What it controls |
|---|---|---|
| `k` | **not yet measured** | Shear/breakage rate constant — the core unknown the whole control scheme is built on |
| `D_min` | **not yet measured** | Minimum achievable particle size for this material/process — the model's floor |

**How to calibrate:**
1. Run a real mixing session with the CV pipeline live, logging
   `(stroke_count, median_um)` pairs at regular intervals (every N strokes,
   not just at the end).
2. Fit the exponential model to that data (least-squares fit of `k` and
   `D_min` — a simple offline fit is fine to start; this does not need to
   run on the ESP32 itself for the initial calibration pass).
3. Repeat across a few runs/materials (see the advisory's note that
   Lyostypt ≠ Gelfoam-equivalent, and DIY gelatin foam should be
   cross-validated against real purchased sponge) — `k` is material- and
   possibly batch-dependent, not a universal constant.
4. Once `k`'s run-to-run variance is understood, decide whether the
   scheduler (§6) needs to re-fit `k` online every run, or whether a
   fixed calibrated value is stable enough to hardcode with a safety
   margin.

**Depends on:** a working CV pipeline (median/IQR must be trustworthy —
see the advisory's Problem A/B/C on why raw CV numbers aren't fully
trustworthy yet either) and locked motor behavior (§1).
**Re-calibrate if:** material changes, lead screw / stroke length changes,
or the CV pipeline's sizing metric changes (e.g. simple diameter → ECD,
per the advisory) — a metric change invalidates any previously-fit `k`.

---

## 6. Mixing scheduler (receding-horizon batch/measure/refit loop)

Replaces the plain PID in `pid.cpp` — see the control-loop discussion in
project notes. These constants don't exist in code yet; listed here so
they land in one place once implemented.

| Value (planned) | What it controls | How to calibrate |
|---|---|---|
| Batch size vs. error | How many strokes to run before the next CV measurement | Start conservative (small batches) until `k` (§5) is well-characterized; scale batch size with distance from target once confident |
| Undershoot fraction | Deliberately commands fewer strokes than the model predicts are needed, so a measurement always happens before the model could overshoot | Start around 70-80% of the model's predicted remaining-strokes count; tighten toward 100% only once `k`'s real-run variance (§5) is small |
| Min/max batch size | Bounds so the scheduler never commands zero (stalls) or an unreasonably large batch (defeats the whole point of frequent re-measurement) | Set from practical CV frame rate and stroke rate — batches should be short enough that a measurement is only a few seconds away, not minutes |
| Stale-measurement timeout | How long to trust a CV reading before treating it as "no data" | Set from observed CV pipeline latency plus margin |

**Depends on:** `k`/`D_min` (§5) must exist before this loop can predict
anything. **This is the highest-priority firmware gap right now** — it's
what actually replaces the unfinished `// TODO: map PID output → stroke
count` in `pid.cpp`.

---

## 7. Target tolerance & safety thresholds

| Constant | Current value | What it controls |
|---|---|---|
| `TARGET_TOLERANCE_UM` | 25 | "In spec" half-width around the setpoint — auto-stop condition |
| `TARGET_SIZE_UM_MIN/MAX/STEP` | 50 / 1000 / 5 | Encoder-adjustable setpoint range |
| HX711 e-stop threshold | see §4 | Automatic emergency stop |
| StallGuard e-stop threshold | see §1.4 | (if adopted as a second automatic e-stop input) |

**How to calibrate `TARGET_TOLERANCE_UM`:** this is currently a
placeholder — it must be set from the CV pipeline's actual measurement
noise (repeatability of `median_um` on a static, unchanging sample), not
picked arbitrarily. If the tolerance is tighter than the measurement
noise, the scheduler will never confidently detect "in spec" and will
either run forever or false-stop on noise.

**Depends on:** everything above — this is deliberately last.

---

## 8. Button / e-stop behavior (firmware-side, not a numeric calibration)

Not a value to measure, but a design decision to lock before wiring
button logic — noted here since it interacts directly with §4's e-stop
threshold:

- **BTN2 currently does double duty** as both graceful stop and
  emergency stop (see `firmware/esp32/README.md`'s Operational Workflow —
  flagged as a known gap). Decision: split these — a graceful stop
  finishes the current stroke/batch and holds; a true e-stop (button OR
  automatic HX711/StallGuard trigger) kills motor power immediately,
  mid-step.
- The HX711 force e-stop (§4) should use the **same code path** as the
  button e-stop, not a separate one — one authoritative "kill everything
  now" function, called from multiple trigger sources (button ISR, force
  threshold check in the main loop, StallGuard threshold if adopted).

---

## Change log

Update this section whenever a value in this doc is actually calibrated
from real data (not just a placeholder default) — a running record of
what's real vs. still assumed.

| Date | Value | Old | New | Basis |
|---|---|---|---|---|
| — | *(none yet — every value above is still a placeholder or unmeasured)* | | | |

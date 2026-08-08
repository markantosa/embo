# Sensor Calibration Data Summary (as of 2026-08-08)

Snapshot of every stroke/size/UAS-voltage/load-cell data source found under
`testing/` and `software/Microscope_Imaging_Data/`, organized by agent
(material) and syringe type. This is a **read-only inventory**, not a
calibration table — none of this has been reshaped into
`firmware/esp32/src/calibration.cpp`'s `SENSOR_CAL_TABLE`, which is still
all-placeholder-zeros pending the formal 9-syringe bench session described in
`firmware/CALIBRATION.md` §5.

**Top-line finding: nothing here is joined yet.** Particle size (microscope),
UAS voltage, and load-cell force were each collected as separate campaigns,
on different dates, without a shared sample ID linking a given batch's size
to its UAS reading to its force reading. Building the real fusion table
means either re-running matched simultaneous captures, or very carefully
inferring correspondence by stroke count alone (risky — see the caveat under
§4).

**Pending update:** APDS9960 + MAX30102 turbidity data is being uploaded
separately (expected later today, 2026-08-08) to gauge the delta between
baseline and mixed readings for both sensors. Not reflected in §1–§5 below
yet — this doc will need a §2.5-equivalent "Turbidity vs. stroke count"
section added once it lands, and §6's proposal already carries a forward
note (§6c) on how it should slot in. Re-check this doc after that upload
rather than treating turbidity as a permanent gap.

---

## 1. Particle size vs. stroke count (microscope, ground truth)

Source: `software/Microscope_Imaging_Data/` (`README (4).md`,
`summary_all_strokes (1).csv`, `variance_study_stroke5_summary.csv`).
Metric: Equivalent Circular Diameter (ECD), HIROX KH-8700 digital microscope.
**Material: Gelfoam-surrogate gelatin** (not Lyostypt — no microscope sizing
data exists for Lyostypt anywhere in the repo). Syringe type used for this
batch prep is not recorded in the microscope dataset.

| Stroke | n particles | Median ECD (µm) | IQR (µm) | Min (µm) | Max (µm) |
|---|---|---|---|---|---|
| 0  | 5   | 5194.9 | 667.0 | 4869.8 | 5920.4 |
| 1  | 152 | 1072.8 | 951.2 | 357.5  | 3719.3 |
| 5  | 151 | 794.2  | 363.8 | 416.3  | 2865.2 |
| 10 | 414 | 680.5  | 335.1 | 268.4  | 2267.1 |
| 15 | 354 | 557.2  | 298.9 | 206.1  | 2183.3 |
| 20 | 256 | 543.2  | 270.6 | 179.7  | 2502.6 |
| 25 | 365 | 562.5  | 380.3 | 165.0  | 2401.0 |

**Trend:** sharp drop 0→1 stroke, continued decrease with diminishing
returns through stroke 20, then a plateau/fluctuation in the ~540–680µm
range from stroke 10–25 rather than a clean monotonic decline.

**Measurement noise, quantified:** a follow-up variance study
(`variance_study_stroke5_summary.csv`) took 5 independent sub-samples at a
fixed stroke count (5) and found batch-to-batch median ECD varying
828.8–1101.8µm (mean of medians 965.2µm, **CV ≈ 10.4%**). This is offered in
the source README as the likely explanation for the stroke 10–25 plateau
being noise rather than a real trend reversal — worth remembering before
reading too much into any single-sample stroke-count comparison elsewhere in
this doc, including the UAS and load-cell data below, which have no
equivalent repeated-sample variance check done on them yet.

**Stroke 0 caveat:** n=5 only (essentially unmixed material, hard to sample
consistently) — treat as an approximate anchor, not a robust data point.

**Not yet done (per the source README's own "Next steps"):** extending the
sweep past stroke 25, and — the specific gap this whole document exists to
highlight — pairing each stroke count's size result with a matching UAS
voltage reading from the *same batch, same moment*. A script name
(`uas_cv_calibration.py`) is referenced for this pairing but **does not
exist in the repo yet** — it's aspirational, not implemented.

---

## 2. UAS voltage vs. stroke count

Source: `testing/UAS Related stuff/UAS datas/`, deeply nested by syringe
type → gain setting → stroke number, each leaf containing
`uas_averaged_summary_*.csv` (+ raw data). All data below is
**Gelfoam-surrogate gelatin unless marked Lyostypt.**

### 2a. BD syringe, 26× gain — most complete sweep (0–40 strokes, 2026-08-04)

Path: `testing/UAS Related stuff/UAS datas/BD syringe/26 x gain/`

| Stroke | Peak V (~1.01MHz) | V @ 1.0MHz |
|---|---|---|
| 0  | 3.1389 | 3.1357 |
| 5  | 3.1189 | 3.1133 |
| 10 | 3.1290 | 3.1258 |
| 15 | 3.1290 | 3.1250 |
| 20 | 3.1349 | 3.1316 |
| 25 | 3.1362 | 3.1344 |
| 30 | 3.1368 | 3.1342 |
| 35 | 3.1396 | 3.1385 |
| 40 | 3.1389 | 3.1389 |

**This sweep is not usable as-is** — under 1% variation across all 40
strokes indicates the 26× gain setting is saturating the ADC/envelope
detector rather than tracking attenuation. Needs a re-gain rerun (lower
gain, per the same troubleshooting logic `firmware/FIRMWARE_TODO.md`
task 3 already flags for the production 3-point sweep) before this BD
dataset means anything.

### 2b. Nipro syringe, 10× gain — partial, real trend (2026-08-01)

Path: `testing/UAS Related stuff/UAS datas/nipro syringe data and analysis/10 x gain/`

| Stroke | V @ 1.0MHz |
|---|---|
| 5  | 2.1235 |
| 6  | 2.1246 |
| 10 | 2.0260 |
| 12 | 1.7340 |

Only 4 points, no stroke-0 baseline, but shows a real downward trend
(unlike the saturated BD 26× set) — the most credible UAS-vs-stroke signal
found, despite being the sparsest. Worth prioritizing a full re-sweep at
this gain setting.

Other Nipro campaigns exist but are too sparse to trend: 100× gain
(strokes 0, 1, 5 only) and several ad-hoc 26-gain 5-stroke tests under
`nipro syringe data and analysis/26 gain/` that aren't a stroke sweep (they
repeat strokes 0–5 across multiple labeled "samples" — more suited to a
variance study than a stroke-response curve, but not yet analyzed as one).

### 2c. Terumo syringe — inconsistent, needs reconciliation (2026-08-01)

Path: `testing/UAS Related stuff/UAS datas/Temuro syringe data and analysis/test_results/`

Two summary files exist and **disagree in scale**:

- `uas_vpp_summary.csv` (strokes 0–10): values ~1.9–2.65 except stroke 8
  (0.303, likely dropout) and stroke 10 (0.282); stroke 9 exactly repeats
  stroke 1's value (2.625) — looks like a copy/logging error, not a real
  reading.
- `uas_trend_at_ref.csv` (strokes 0–9, plus an isolated stroke 20 at 0.507):
  values on a completely different scale (~0.27–0.30).

**Do not use either file until reconciled** — likely a units/reference-point
mismatch between the two scripts that generated them, not a hardware issue.
This syringe/oscilloscope folder also has a per-frequency oscilloscope sweep
(`oscilloscope/0.9 MHz` … `1 MHz`, 10th/20th stroke only) that's a frequency
characterization, not a stroke-response dataset — out of scope for this doc.

### 2d. Lyostypt — the only Lyostypt UAS data in the repo

Path: `testing/UAS Related stuff/UAS datas/previous UAS data lyostyph/jely_pump_1MHz/`
(strokes 0–10, older/undated relative to the other campaigns, 1MHz single-tone
rig predating the current 3-frequency sweep design).

This is flagged as **not yet extracted into a table** — the raw per-stroke
CSVs exist but weren't tabulated for this summary. Given it's the only
Lyostypt UAS series that exists anywhere, extracting it should be a near-term
priority if Lyostypt is still an active target material (per
`firmware/esp32/include/calibration.h`'s comment, Lyostypt needs its own
`SENSOR_CAL_TABLE`, not a shared one with Gelfoam).

---

## 3. Load-cell force / viscosity data

Source: `testing/loadcell_viscosity/cleaned data/*.csv` and
`testing/loadcell_viscosity/readme.md`.

**Important naming caveat, from the source readme directly:** these files'
stroke counts (`1s`, `3s`, `5s`, `7s`, `9s`) use an **old definition of
"stroke" = 2 pumps**, not the current firmware's stroke definition (one
forward+return cycle, `motor_increment_stroke()`). **Do not directly compare
these stroke labels to the UAS or microscope stroke counts above without
converting** — a loadcell "5s" file is not necessarily the same physical
point in a mixing run as a "5th stroke" UAS or microscope sample.

No agent, syringe type, or date is recorded in these files or their
filenames — this data cannot currently be matched to any specific UAS or
microscope campaign above.

| File | Samples | Max\|force1\| (g) | Mean\|force1\| (g) |
|---|---|---|---|
| 1s.csv   | 30  | 1730.6 | 57.9 |
| 3s1.csv  | 99  | 4959.1 | 121.5 |
| 3s23.csv | 62  | 2548.4 | 444.1 |
| 5s1.csv  | 43  | 5031.4 | 256.3 |
| 5s2.csv  | 101 | 2510.1 | 449.2 |
| 5s3.csv  | 30  | 2078.9 | 338.8 |
| 7s.csv   | 210 | 4947.3 | 320.7 |
| 9s1–9s7.csv | 37–126 each | 3464.7–4892.4 | 169.5–322.6 |

(`3s1`/`3s23`, and `9s1`–`9s7`, are chronological fragments of single
continuous runs that got split across files due to a logging crash — see
the readme — not independent repeat samples.)

**Separately, `testing/syringe_force_baseline/test_results/`** has simple
single-trace force baselines, labeled by material rather than stroke count:
a Lyostypt baseline (2026-07-09), two "china foam" baselines (big/small,
2026-07-14), and one unlabeled trace (2026-07-07). These are baseline
characterizations, not a stroke sweep, and "china foam" is a material label
that doesn't appear anywhere in the UAS or microscope datasets above.

---

## 4. Cross-referencing across sections — read this before using anything above together

**No shared sample ID exists between §1, §2, and §3.** Every attempt to
line these up by stroke count alone is an inference, not a measurement:

- Different campaigns ran on different dates, almost certainly different
  material batches (gelatin is hand-prepared per `software/SOFTWARE_TODO.md`
  task 11's DIY-vs-purchased-sponge caveat — batch-to-batch cross-link
  variation is a known open risk).
  - Different syringe types across sections — §1's syringe type isn't even
  recorded, §2 has BD/Nipro/Terumo/none(Lyostypt), §3 has no syringe label
  at all.
- §3's "stroke" unit is literally different from §1/§2's (2 pumps vs. 1
  forward+return cycle — see the caveat in §3).
- §1 itself already measured ~10% batch-to-batch CV at a single fixed
  stroke count — so even within one section, two samples at "the same"
  stroke count aren't guaranteed comparable, let alone across sections.

**What this means for the firmware calibration table:** populating
`SENSOR_CAL_TABLE`'s 9 rows (`knownSizeUm`, `uasAttenuation`, `turbApdsRatio`,
`turbMaxRatio`, `forceGrams`) per `firmware/CALIBRATION.md` §5 requires a
**new, simultaneous** bench session — one syringe, one batch, one moment,
all four sensors (or at least UAS + force, since turbidity is currently
disabled per `firmware/esp32/README.md`) read together, cross-checked
against a CV/microscope size measurement of that same batch. None of the
data inventoried in this document substitutes for that session; it's useful
for sanity-checking rough trend direction (e.g. "does UAS voltage move the
right way as strokes increase") and for prioritizing which gain
setting/syringe combination to start the real session with (§2b's Nipro
10× gain data is the most promising starting point; §2a's BD 26× gain needs
a lower gain before it's usable at all).

---

## 5. Gaps — explicit

- **No agent/syringe/date metadata in the load-cell CSVs** (§3) — cannot be
  linked to any UAS or microscope campaign with confidence.
- **BD 26× gain UAS sweep is saturated/flat** — needs a re-gain rerun.
- **Nipro has no full stroke sweep at any gain** — best coverage is 4
  scattered points (§2b).
- **Terumo's two UAS summary files disagree in scale** and contain
  suspect repeated/dropout values — needs reconciliation before use.
- **Lyostypt: only one UAS series exists (0–10 strokes), not yet
  extracted into a table; no load-cell stroke sweep for Lyostypt at all;
  no microscope sizing data for Lyostypt at all.** Lyostypt is the
  thinnest-covered material across every sensor.
- **"China foam" material** (force baseline only) has no UAS or microscope
  counterpart anywhere.
- **No particle-size ground truth is joined to any sensor reading** — this
  is the single blocking gap before `SENSOR_CAL_TABLE` can be populated with
  real (not placeholder) values.
- **`uas_cv_calibration.py`**, referenced in the microscope dataset's
  README as the intended pairing script, does not exist in the repo yet.

---

## 6. Proposal — wiring agent/syringe selection into the closed-loop calibration

**Scope note: Terumo is excluded from this proposal entirely**, per direction
— its UAS data (§2c) was already flagged as unreliable/unreconciled, and
dropping it removes the need to resolve that inconsistency at all. Everything
below is Gelfoam-surrogate (BD, Nipro) and Lyostypt only.

### 6a. The core problem: one global table can't represent this data

`calibration.cpp` today has exactly one `SENSOR_CAL_TABLE[9]`, and
`calib_estimate_particle_size_um()` inverts live readings through it
unconditionally — there's no concept of "which material/syringe is this
reading from." But §1–§3 already show that assumption doesn't hold:

- **Agents differ in trend, not just offset.** Gelfoam is the only material
  with any size-vs-stroke ground truth at all (§1); Lyostypt's mechanical
  behavior is documented elsewhere (`docs/EMBO_UAS_CV_Technical_Advisory.txt`
  §3) as genuinely different — non-gelling, stringy, bovine collagen vs.
  denatured gelatin. A single calibration curve fit to Gelfoam data would
  silently mis-size Lyostypt, not just be slightly off.
- **Syringe choice changes the raw sensor's operating point, not just a
  scale factor.** BD's 26× gain saturates the UAS ADC outright (§2a) while
  Nipro's 10× gain doesn't (§2b) — different syringes/mounts apparently
  need different AD9833 gain to stay in range. A table built from Nipro
  readings can't be reused unmodified for BD without also carrying "what
  gain was this table calibrated at."

**Proposal: key the calibration data by (agent, syringe) pair, not one
global table.** Concretely, extend `calibration.h`/`calibration.cpp` from a
single `SENSOR_CAL_TABLE` to a small lookup, e.g.:

```
struct MaterialCalibration {
    Agent agent;            // GELFOAM, LYOSTYPT
    SyringeType syringe;    // BD, NIPRO  (Terumo intentionally omitted)
    float uasGainSetting;   // the AD9833/ADC gain this table was calibrated at
    SensorCalibrationPoint table[SENSOR_CAL_NUM_POINTS];
};
```

with one populated row per (agent, syringe) combination that actually gets a
real bench session, and the rest left as the existing all-zero placeholder
(which already fails safe — see the comment in `calibration.cpp` — so an
un-run combination just excludes itself from fusion rather than guessing).

**Where this plugs into the UI:** `src/mixing_options.cpp`'s Agent
(Gelfoam/Lyostypt) and Syringe Type (Terumo/Nipro — BD isn't currently a UI
option and would need adding, or an existing option renamed/remapped) are
**confirmed cosmetic today** (per the earlier README audit — nothing reads
them). This proposal is the reason to make them functional: the operator's
selection during the Start flow should select which `MaterialCalibration` row
`scheduler.cpp` uses for the whole run, and which UAS gain `uas_init()`/
`uas_update()` configures before the run starts. That's a firmware change,
not something this document does — flagging it as the concrete next step
that gives the existing (currently decorative) menu screens a real purpose.

**Phasing, given current data quality:** don't try to populate every
(agent, syringe) cell at once.
1. **Nipro + Gelfoam first** — §2b is the only UAS dataset with a real
   (non-saturated) trend, and §1's microscope data is Gelfoam. This pair has
   the best head start toward a real 9-point session.
2. **BD + Gelfoam second, after a gain fix** — re-run §2a's sweep at a lower
   gain before attempting a bench session; the syringe/mount itself may be
   fine, only the gain setting was wrong.
3. **Lyostypt (any syringe) last, and flagged low-confidence even once
   done** — §2d's data isn't extracted yet, there's no Lyostypt microscope
   ground truth at all (§1), and no Lyostypt load-cell stroke sweep (§3).
   Until a real Lyostypt session runs, the safest default is excluding
   Lyostypt from live fusion and relying on the diagnostic-only breakage-fit
   cross-check (`calib_breakage_add_point()`) with operator CV verifications
   carrying more relative weight for that material specifically.

### 6b. Baselining/taring per session, including UAS

Force already does this correctly and should be the template for the other
channels: `force_sensor_tare()` runs fresh every boot
(`calibration.h`/`main.cpp`, `calib_hx711_set_tare()`), so `HX711_TARE_1/2`
are only a startup fallback, not a value trusted indefinitely. Turbidity and
UAS do **not** currently follow this pattern — `APDS9960_BASELINE_CLEAR`,
`MAX30102_BASELINE_IR/RED`, and the UAS baseline are fixed constants (or, for
UAS, sampled once at `uas_init()` — see `firmware/esp32/README.md`'s "UAS
attenuation baseline" note), not re-sampled per run.

**This matters more once agent/syringe selection is wired in (§6a).** Two
compounding reasons a fixed baseline breaks down here:

- §2's data shows the same nominal setup (1MHz, similar gain intent)
  produces meaningfully different absolute starting voltages across
  syringes (BD ~3.14V, Nipro ~2.1V @ comparable stroke range) — a single
  hardcoded baseline can't be right for more than one syringe choice.
- A syringe swap, re-mount, or even the same syringe reloaded with a fresh
  batch changes acoustic/optical coupling before a single stroke has
  happened — exactly the kind of drift `force_sensor_tare()` already exists
  to cancel out for force, but UAS/turbidity currently don't.

**Proposal: sample a fresh baseline for every channel at the start of each
run, not just force.** Concretely, once the operator confirms the Warning
screen (mount check, "3. Warning / mount check" in `firmware/esp32/README.md`
— syringe is loaded and mounted, but mixing hasn't started):
1. Take a short settle-and-average reading on UAS (per swept frequency),
   APDS9960 ALS, and MAX30102 IR/RED — mirroring the existing
   `force_sensor_tare()` pattern (and `uas_init()`'s existing settle-then-
   sample logic, just re-run per-run instead of once per boot).
2. Store these as the *session* baseline, not the compile-time
   `_BASELINE_*` constants — same relationship `_tare1`/`_tare2` in
   `calibration.cpp` already have to `HX711_TARE_1/2`.
3. Compute `uasAttenuation`/`turbApdsRatio`/`turbMaxRatio` against this
   session baseline for the rest of the run, the same division-by-baseline
   `calib_turbidity_ratio_*()` already does — just against a live variable
   instead of a `#define`.
4. **The bench calibration table itself (§6a) should be recorded in the same
   relative terms** — i.e. `SENSOR_CAL_TABLE`'s `uasAttenuation` column
   should be a ratio against *that bench session's own pre-mix baseline*,
   not an absolute voltage — so a table built once stays valid across future
   runs with naturally different starting voltages (different day, slightly
   different mount, etc.), rather than needing to be re-baselined every time
   drift is observed. This is consistent with how the column is already
   named (`uasAttenuation`, a ratio) even though the *live* value it's
   currently compared against isn't yet computed relative to a per-run
   baseline.

**Net effect:** the combination of §6a (per-agent/syringe table selection)
and §6b (per-run baseline) means "select Gelfoam + Nipro, mount syringe,
confirm Warning screen" should end with the scheduler holding: the correct
calibration table, the correct UAS gain, and a fresh zero-point for every
fusion channel — before the first stroke of that specific run.

### 6c. Forward note — incoming turbidity data (APDS9960 + MAX30102)

Turbidity data is being uploaded separately (expected 2026-08-08) to gauge
the baseline-vs-mixed delta for both sensors — not yet reflected in §1–§5.
Once it lands, it should extend this proposal rather than sit outside it:

- **It's a second, independent confirmation that §6b's per-run-baseline
  argument generalizes.** If the incoming data shows APDS9960/MAX30102
  deltas also vary by agent/syringe the way UAS's absolute voltage does
  (§2a vs §2b), that's a second sensor confirming fixed constants
  (`APDS9960_BASELINE_CLEAR`, `MAX30102_BASELINE_IR/RED`, currently `1.0f`
  placeholders in `calibration.h`) can't be right for more than one
  material/syringe combination — strengthening, not just adding to, the
  case already made in §6b.
- **It's currently the difference between 2 live fusion channels and 4.**
  Recall from `firmware/esp32/README.md`: `turbidity_init()`/
  `turbidity_update()` are commented out of `main.cpp` right now (sensors
  not physically connected on the bench), so even a perfect bench table
  can't be used until that's reversed. The incoming data is a good forcing
  function to prioritize re-enabling those two calls alongside wiring in
  the wider proposal above — there's little point finishing §6a/§6b's
  UAS/force plumbing while turbidity stays hardware-disabled.
- **Update the phasing in §6a once the delta data is in hand.** If
  APDS9960/MAX30102 turn out to have a cleaner (less gain-saturated, more
  monotonic) response than UAS's BD-syringe result, turbidity may deserve
  to lead a bench session rather than trail UAS — don't assume UAS stays
  the priority channel by default once real turbidity numbers exist to
  compare against.
- Same agent/syringe/Terumo-exclusion scoping as the rest of this document
  applies to the incoming data too — if it includes Terumo runs, treat those
  the same way §2c's UAS data was treated: out of scope, not silently folded
  into the Gelfoam/Nipro or Gelfoam/BD picture.

---

## Source index

| Data | Path |
|---|---|
| Particle size (microscope) | `software/Microscope_Imaging_Data/README (4).md`, `summary_all_strokes (1).csv`, `variance_study_stroke5_summary.csv` |
| UAS — BD syringe | `testing/UAS Related stuff/UAS datas/BD syringe/26 x gain/`, `.../50 x gain/` |
| UAS — Nipro syringe | `testing/UAS Related stuff/UAS datas/nipro syringe data and analysis/` |
| UAS — Terumo syringe | `testing/UAS Related stuff/UAS datas/Temuro syringe data and analysis/test_results/` |
| UAS — Lyostypt | `testing/UAS Related stuff/UAS datas/previous UAS data lyostyph/jely_pump_1MHz/` |
| Load cell / viscosity | `testing/loadcell_viscosity/cleaned data/`, `readme.md` |
| Force baseline (incl. Lyostypt, china foam) | `testing/syringe_force_baseline/test_results/`, `readme.md` |
| Firmware calibration target format | `firmware/CALIBRATION.md` §5, `firmware/esp32/include/calibration.h` (`SensorCalibrationPoint`, `SENSOR_CAL_TABLE`) |

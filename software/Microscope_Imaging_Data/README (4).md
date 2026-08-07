# Gelfoam Particle Size vs. Stroke Count — Microscope Measurement Dataset

## Purpose
Characterizes how Gelfoam-surrogate particle size (equivalent circular
diameter, ECD) changes with syringe mixing stroke count. This dataset forms
the imaging/ground-truth half of the UAS voltage &harr; particle size
calibration effort (see `uas_cv_calibration.py` in the CV pipeline).

## Method
- **Instrument:** HIROX KH-8700 3D digital microscope, built-in 2D
  measurement tool (Area/auto-width), calibrated per-zoom.
- **Sizing metric:** Equivalent Circular Diameter (ECD), computed as
  `ECD = 2 x sqrt(Area / pi)` from each particle's measured area — the
  standard convention for irregular-particle sizing, consistent with
  clinical embolic agent size classifications.
- **Sample prep:** batch mixed on syringe to target stroke count, aliquot
  spread thin in petri dish (dark background, oblique lighting) to separate
  particles for individual measurement.
- **Exclusions applied consistently across all samples:** particles cut off
  at frame edges, and particles with boundaries too indistinct to
  confidently outline, were excluded — see per-sample n counts below.

## Results summary

| Stroke Count | n particles | Median ECD (um) | IQR (um) | Min (um) | Max (um) |
|---|---|---|---|---|---|
| 0  | 5   | 5194.9 | 667.0 | 4869.8 | 5920.4 |
| 1  | 152 | 1072.8 | 951.2 | 357.5  | 3719.3 |
| 5  | 151 | 794.2  | 363.8 | 416.3  | 2865.2 |
| 10 | 414 | 680.5  | 335.1 | 268.4  | 2267.1 |
| 15 | 354 | 557.2  | 298.9 | 206.1  | 2183.3 |
| 20 | 256 | 543.2  | 270.6 | 179.7  | 2502.6 |
| 25 | 365 | 562.5  | 380.3 | 165.0  | 2401.0 |

Full statistics (mean, stdev, Q1, Q3) in `summary_all_strokes.csv`.

![Median particle size vs stroke count](median_vs_stroke_count.png)

**Trend:** median particle size drops sharply between 0 and 1 stroke, then
continues decreasing with diminishing returns through stroke 20 — consistent
with expected mechanical fragmentation behavior (large initial size
reduction, flattening as material is progressively broken down further).
The curve is visibly plateauing from stroke 10 onward, fluctuating within a
~540-680 um range across strokes 10-25 (680.5, 557.2, 543.2, 562.5) rather
than continuing a clean monotonic decline — a follow-up variance study
(see `variance_study_stroke5_summary.csv`) found that batch-to-batch
measurement noise at a fixed stroke count is on the order of 10% (CV), which
likely explains much of this plateau-region fluctuation rather than it
reflecting a real reversal in the trend. IQR also narrows overall with
increasing stroke count, indicating mixing improves size *consistency* in
addition to reducing median size.

**Note on stroke 0:** n=5 is a much smaller sample than the other conditions
(expected, since this is essentially unmixed material) — treat as an
approximate anchor point rather than as statistically robust as the other
stroke counts.

## Repository structure
```
stroke0/    raw microscope images + measurement CSVs, 0 strokes
stroke1/    raw microscope images + measurement CSVs, 1 stroke
stroke5/    raw microscope images + measurement CSVs, 5 strokes
stroke10/   raw microscope images + measurement CSVs, 10 strokes
stroke15/   raw microscope images + measurement CSVs, 15 strokes
stroke20/   raw microscope images + measurement CSVs, 20 strokes
stroke25/   raw microscope images + measurement CSVs, 25 strokes

particle_measurements_stroke0.csv    per-particle ECD (um), sample_id=stroke0
particle_measurements_stroke1.csv    per-particle ECD (um), sample_id=stroke1
particle_measurements_stroke5.csv    per-particle ECD (um), sample_id=stroke5
particle_measurements_stroke10.csv   per-particle ECD (um), sample_id=stroke10
particle_measurements_stroke15.csv   per-particle ECD (um), sample_id=stroke15
particle_measurements_stroke20.csv   per-particle ECD (um), sample_id=stroke20
particle_measurements_stroke25.csv   per-particle ECD (um), sample_id=stroke25

summary_all_strokes.csv              one row per stroke count: n, median,
                                      Q1, Q3, IQR, mean, stdev, min, max
median_vs_stroke_count.png           trend graph (median +/- IQR vs stroke count)

variance_study_stroke5_summary.csv   5 independent sub-samples from one
                                      syringe at a fixed stroke count (5),
                                      quantifying batch-to-batch measurement
                                      variability (CV ~10%)
variance_study_boxplot.png           box plot of the 5 variance-study
                                      sub-samples, with mean +/- SD of medians
```

## Next steps
- Extend stroke sweep beyond 25 (e.g. up to 40) to confirm whether the
  observed plateau continues or particle size stabilizes entirely.
- Pair each stroke count's median/IQR with a matching UAS voltage reading
  (same batch, same-moment split) to build the full voltage-to-size
  calibration dataset — see `uas_cv_calibration.py`.

"""
Builds a per-single-stroke table of (UAS voltage, predicted CV particle size),
using the CORRECTED stroke convention: 1 stroke = one push (left OR right),
not a left+right pair.

Why this requires interpolation: all existing UAS data was collected under
the OLD (mislabeled) convention, where "stroke N" in the folder names actually
meant N left+right pairs = 2N real strokes. So the raw data only has direct
voltage measurements at every EVEN true stroke (labels 0-40 -> true strokes
0, 2, 4, ..., 80). There is no direct measurement at odd true strokes
(1, 3, 5, ..., 79) -- those are the "gaps" to fill.

Method:
  1. Relabel: true_stroke = 2 x old_label for every measured UAS point.
  2. Interpolate UAS voltage linearly across the odd (unmeasured) true
     strokes, between their two even neighbors.
  3. Generate CV particle size at EVERY true stroke (both measured and
     interpolated voltage points) using the power-law fit from Part D of the
     report (log-log regression fit in spearman_and_loglog_fit.py):
        size = 14810 x voltage^-3.35
     This is deliberate -- rather than mixing actual CV measurements (only
     available at 7 sparse strokes) with predicted ones, every row uses the
     SAME formula, so the size column is a smooth, consistent function of
     voltage across the whole range.

Caveat carried over from Part D: the exponent (-3.35) is sensitive to the
stroke-0 high-leverage point (95% CI: -4.24 to -2.46; drops to -2.05 if that
point is excluded) -- so treat generated sizes as illustrative/interpolated,
not as new independent measurements.
"""
import glob
import os

import numpy as np
import pandas as pd

REFERENCE_FREQ_HZ = 960_000
UAS_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# Power-law fit from Part D (spearman_and_loglog_fit.py, log-log regression
# on the full n=7 dataset): size = A * voltage^B
FIT_A = 14810.4
FIT_B = -3.35


def _leading_number(folder_name: str):
    digits = ""
    for ch in folder_name.strip():
        if ch.isdigit():
            digits += ch
        else:
            break
    return int(digits) if digits else None


def load_measured_voltages():
    """old_label -> UAS voltage @0.96MHz, for every labeled stroke folder found."""
    result = {}
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if any(ex in entry for ex in ["cross check with oscilloscope", "ambient"]):
            continue
        label = _leading_number(entry)
        if label is None:
            continue
        csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
        if not csvs:
            continue
        df = pd.read_csv(csvs[0])
        row = df.iloc[(df["freq_hz"] - REFERENCE_FREQ_HZ).abs().argsort()[:1]]
        if label in result:
            continue
        result[label] = float(row["mean_uas_volts"].values[0])
    return result


def main():
    measured = load_measured_voltages()
    labels_sorted = sorted(measured)
    print(f"Found {len(labels_sorted)} measured stroke folders (old labels {labels_sorted[0]}-{labels_sorted[-1]})")

    # Relabel to TRUE stroke count (1 stroke = one push): true = 2 * old_label
    true_stroke_measured = {2 * lbl: v for lbl, v in measured.items()}
    max_true_stroke = max(true_stroke_measured)

    # Interpolate voltage at every integer true stroke from 0 to max, filling
    # the odd (unmeasured) gaps linearly between their even neighbors.
    known_x = np.array(sorted(true_stroke_measured))
    known_y = np.array([true_stroke_measured[x] for x in known_x])
    all_strokes = np.arange(0, max_true_stroke + 1)
    interpolated_voltage = np.interp(all_strokes, known_x, known_y)

    predicted_size = FIT_A * np.power(interpolated_voltage, FIT_B)

    out = pd.DataFrame({
        "stroke": all_strokes,
        "uas_voltage_v": interpolated_voltage,
        "cv_particle_size_um": predicted_size,
        "voltage_source": ["measured" if s in true_stroke_measured else "interpolated" for s in all_strokes],
    })
    out_path = os.path.join(OUT_DIR, "per_stroke_voltage_and_size.csv")
    out.to_csv(out_path, index=False)

    n_measured = (out["voltage_source"] == "measured").sum()
    n_interp = (out["voltage_source"] == "interpolated").sum()
    print(f"\nBuilt {len(out)} rows (true strokes 0-{max_true_stroke}, 1 stroke = one push):")
    print(f"  {n_measured} rows have directly measured UAS voltage (even true strokes)")
    print(f"  {n_interp} rows have linearly interpolated voltage (odd true strokes -- the 'gaps')")
    print(f"  ALL {len(out)} rows have particle size generated from size = {FIT_A} x voltage^{FIT_B} (Part D fit)")
    print(f"\nSaved: {out_path}")
    print(out.head(10).to_string(index=False))


if __name__ == "__main__":
    main()

"""
Follow-up check: determine_relationship_shape.py found voltage-vs-size is best
described by a power law -- but only at ONE frequency (0.96MHz), chosen for
practical reasons (unclipped, syringe-safe), not because it was verified to be
representative. This script repeats that same shape-fit (5 candidate 2-parameter
curves, compared by R^2) at EVERY frequency in the transducer's resonance band
(0.94-1.06MHz, 13 frequencies) to check whether the power-law finding holds
generally, or was a coincidence specific to 0.96MHz.

Note: CV median size per stroke doesn't depend on frequency (it's microscope
data), so only the voltage side changes per frequency -- the voltage-vs-size
fit is redone at each frequency using that frequency's stroke-voltage values.
"""
import glob
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.optimize import curve_fit

STROKES = [0, 1, 5, 10, 15, 20, 25]
RESONANCE_BAND_HZ = range(940_000, 1_060_001, 10_000)  # 0.94-1.06MHz, 13 freqs

CV_DIR = r"C:\Users\LOQ\Documents\Github repo\embo\software\Microscope_Imaging_Data\Stroke_Tabulated_Data"
UAS_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def cv_median_size(stroke: int) -> float:
    matches = glob.glob(os.path.join(CV_DIR, f"particle_measurements_stroke{stroke} *.csv"))
    matches += glob.glob(os.path.join(CV_DIR, f"particle_measurements_stroke{stroke}.csv"))
    df = pd.read_csv(matches[0])
    return df["particle_size_um"].median()


def _leading_number(folder_name: str):
    digits = ""
    for ch in folder_name.strip():
        if ch.isdigit():
            digits += ch
        else:
            break
    return int(digits) if digits else None


def load_stroke_folder(stroke: int) -> pd.DataFrame:
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if _leading_number(entry) == stroke:
            csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
            return pd.read_csv(csvs[0])
    raise FileNotFoundError(stroke)


def r_squared(y_actual, y_pred):
    y_actual = np.asarray(y_actual, dtype=float)
    y_pred = np.asarray(y_pred, dtype=float)
    ss_res = np.sum((y_actual - y_pred) ** 2)
    ss_tot = np.sum((y_actual - y_actual.mean()) ** 2)
    return 1 - ss_res / ss_tot


MODELS = {
    "linear":      (lambda x, a, b: a * x + b,        [1, 0]),
    "exponential": (lambda x, a, b: a * np.exp(b * x), [1, 0.1]),
    "power":       (lambda x, a, b: a * np.power(x, b), [1, 1]),
    "logarithmic": (lambda x, a, b: a * np.log(x) + b, [1, 0]),
    "inverse":     (lambda x, a, b: a / x + b,         [1, 0]),
}


def best_fit(x, y):
    best_name, best_r2 = None, -np.inf
    power_r2 = None
    for name, (func, p0) in MODELS.items():
        try:
            popt, _ = curve_fit(func, x, y, p0=p0, maxfev=10000)
            r2 = r_squared(y, func(x, *popt))
        except Exception:
            continue
        if name == "power":
            power_r2 = r2
        if r2 > best_r2:
            best_name, best_r2 = name, r2
    return best_name, best_r2, power_r2


def main():
    size_by_stroke = {s: cv_median_size(s) for s in STROKES}
    stroke_folders = {s: load_stroke_folder(s) for s in STROKES}

    rows = []
    for freq_hz in RESONANCE_BAND_HZ:
        voltages = []
        for s in STROKES:
            df = stroke_folders[s]
            row = df.iloc[(df["freq_hz"] - freq_hz).abs().argsort()[:1]]
            voltages.append(float(row["mean_uas_volts"].values[0]))
        sizes = [size_by_stroke[s] for s in STROKES]

        best_name, best_r2, power_r2 = best_fit(np.array(voltages), np.array(sizes))
        rows.append({
            "freq_mhz": freq_hz / 1e6,
            "best_shape": best_name,
            "best_r2": best_r2,
            "power_r2": power_r2,
        })

    result = pd.DataFrame(rows)
    result.to_csv(os.path.join(OUT_DIR, "shape_fit_across_resonance_band.csv"), index=False)
    print("Voltage-vs-size shape fit, repeated at every resonance-band frequency:")
    print(result.to_string(index=False))

    n_power_wins = (result["best_shape"] == "power").sum()
    print(f"\nPower law is the best-fitting shape at {n_power_wins}/{len(result)} frequencies.")
    print(f"Power-law R^2 range across the band: {result['power_r2'].min():.3f} - {result['power_r2'].max():.3f}")
    print(f"(0.96MHz specifically: power R^2 = {result.loc[result.freq_mhz==0.96, 'power_r2'].values[0]:.3f})")

    fig, ax = plt.subplots(figsize=(10, 6))
    colors = ["tab:green" if s == "power" else "tab:orange" for s in result["best_shape"]]
    ax.bar(result["freq_mhz"], result["power_r2"], width=0.008, color=colors)
    ax.axvline(0.96, color="black", linestyle="--", linewidth=1, label="0.96MHz (frequency used in Figures 4-8)")
    ax.set_ylim(0, 1)
    ax.set_xlabel("Frequency (MHz)")
    ax.set_ylabel("Power-law R^2 (voltage vs size)")
    ax.set_title("Does the Power-Law Fit Hold Across the Whole Resonance Band?\n(green = power law is the best shape at that frequency, orange = a different shape wins)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "shape_fit_across_resonance_band.png"), dpi=150)
    print("\nSaved: shape_fit_across_resonance_band.csv, shape_fit_across_resonance_band.png")


if __name__ == "__main__":
    main()

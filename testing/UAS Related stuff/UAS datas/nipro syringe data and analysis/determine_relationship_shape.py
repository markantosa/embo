"""
Goal: instead of assuming the UAS voltage <-> CV particle size relationship is
LINEAR (which is all Pearson r actually tests -- see the correlation-vs-
relationship discussion), fit several candidate curve SHAPES to the same data
and let R^2 say which shape actually fits best. Pearson r/p is only the right
tool to report if linear turns out to be (at or near) the best-fitting shape.

Fairness rule: every candidate here has exactly 2 free parameters, same as a
line (y = a*x + b). Comparing R^2 across models with different numbers of
parameters is invalid -- a model with more free parameters can always fit
better even when it's capturing noise, not a real shape (with n=7 points, a
6-parameter model could hit R^2=1 trivially). Keeping parameter count fixed
makes the R^2 comparison a fair test of SHAPE, not of flexibility.

Candidate shapes (x = UAS voltage, y = CV median particle size -- same
orientation as the calibration curve in Figure 5, and x is never zero here,
unlike stroke count, so log/power/inverse forms are all valid):
  linear:      y = a*x + b
  exponential: y = a*exp(b*x)
  power:       y = a*x^b
  logarithmic: y = a*ln(x) + b
  inverse:     y = a/x + b

Caveat printed at the end: n=7 is a small sample for shape discrimination --
several shapes can fit similarly well by eye/R^2 with this few points, so the
winning shape should be treated as "best-supported by this data", not proven.
"""
import glob
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.optimize import curve_fit

STROKES = [0, 1, 5, 10, 15, 20, 25]
REFERENCE_FREQ_HZ = 960_000

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


def uas_voltage_at_reference_freq(stroke: int) -> float:
    match = None
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if _leading_number(entry) == stroke:
            match = full_path
            break
    csvs = glob.glob(os.path.join(match, "uas_averaged_summary_*.csv"))
    df = pd.read_csv(csvs[0])
    nearest_row = df.iloc[(df["freq_hz"] - REFERENCE_FREQ_HZ).abs().argsort()[:1]]
    return float(nearest_row["mean_uas_volts"].values[0])


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


def fit_and_plot(x, y, point_labels, xlabel, ylabel, title_prefix, out_stem):
    """Fit all candidate shapes to (x, y), print/save/plot the comparison.

    Returns (best_shape_name, best_r2, linear_r2).
    """
    results = []
    fitted_curves = {}
    for name, (func, p0) in MODELS.items():
        try:
            popt, _ = curve_fit(func, x, y, p0=p0, maxfev=10000)
            y_pred = func(x, *popt)
            r2 = r_squared(y, y_pred)
            results.append({"shape": name, "r_squared": r2, "params": popt})
            fitted_curves[name] = (func, popt)
        except Exception as e:
            print(f"  {name}: fit failed ({e})")

    results_df = pd.DataFrame(results).sort_values("r_squared", ascending=False)
    print(f"\n=== {title_prefix} ===")
    print(f"Candidate shape fits (x = {xlabel}, y = {ylabel}), ranked by R^2:")
    print(results_df.to_string(index=False))

    best = results_df.iloc[0]
    linear_r2 = float(results_df.loc[results_df["shape"] == "linear", "r_squared"].iloc[0])
    print(f"Best-fitting shape: {best['shape']} (R^2 = {best['r_squared']:.4f}); linear R^2 = {linear_r2:.4f}")
    if best["shape"] == "linear" or (linear_r2 >= best["r_squared"] - 0.02):
        print("=> Linear is the best fit (or effectively tied). Pearson r/p is a fair summary here.")
    else:
        print(f"=> '{best['shape']}' fits noticeably better than linear -- this relationship is NOT linear.")

    results_df.drop(columns="params").to_csv(os.path.join(OUT_DIR, f"{out_stem}.csv"), index=False)

    fig, ax = plt.subplots(figsize=(10, 7))
    ax.scatter(x, y, color="black", zorder=5, s=60, label="Measured data (n=7)")
    for xi, yi, lbl in zip(x, y, point_labels):
        ax.annotate(lbl, (xi, yi), textcoords="offset points", xytext=(6, 6), fontsize=8)

    x_smooth = np.linspace(x.min(), x.max(), 200)
    colors = plt.cm.tab10.colors
    for i, (name, (func, popt)) in enumerate(fitted_curves.items()):
        r2 = results_df.loc[results_df["shape"] == name, "r_squared"].values[0]
        ax.plot(x_smooth, func(x_smooth, *popt), label=f"{name} (R2={r2:.3f})",
                color=colors[i % len(colors)], linewidth=2 if name == best["shape"] else 1,
                alpha=1.0 if name == best["shape"] else 0.6)

    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(f"{title_prefix} (winner: {best['shape']}, R2={best['r_squared']:.3f})")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, f"{out_stem}.png"), dpi=150)
    print(f"Saved: {out_stem}.csv, {out_stem}.png")

    return best["shape"], best["r_squared"], linear_r2


def main():
    rows = []
    for s in STROKES:
        rows.append({
            "stroke": s,
            "cv_median_size_um": cv_median_size(s),
            "uas_voltage_v": uas_voltage_at_reference_freq(s),
        })
    data = pd.DataFrame(rows)
    point_labels = [f"stroke {s}" for s in data["stroke"]]

    # 1) The combined relationship: voltage vs size directly (x never 0, fine as-is)
    fit_and_plot(
        data["uas_voltage_v"].to_numpy(), data["cv_median_size_um"].to_numpy(), point_labels,
        f"UAS voltage @ {REFERENCE_FREQ_HZ/1e6:.2f}MHz (V)", "CV median particle size (um)",
        "Voltage vs Size: Which Curve Shape Fits Best?", "shape_fit_comparison"
    )

    # 2) and 3) Each measurement independently, as a function of stroke count.
    # Stroke count includes 0, which breaks power/log/inverse (need x>0), so
    # stroke+1 is used as x for these two -- applied identically to every
    # candidate shape, so the comparison between shapes stays fair.
    stroke_x = (data["stroke"] + 1).to_numpy()

    size_shape, size_r2, size_linear_r2 = fit_and_plot(
        stroke_x, data["cv_median_size_um"].to_numpy(), point_labels,
        "Stroke count + 1", "CV median particle size (um)",
        "Stroke vs Size (independently): Which Curve Shape Fits Best?", "shape_fit_stroke_vs_size"
    )

    volt_shape, volt_r2, volt_linear_r2 = fit_and_plot(
        stroke_x, data["uas_voltage_v"].to_numpy(), point_labels,
        "Stroke count + 1", f"UAS voltage @ {REFERENCE_FREQ_HZ/1e6:.2f}MHz (V)",
        "Stroke vs Voltage (independently): Which Curve Shape Fits Best?", "shape_fit_stroke_vs_voltage"
    )

    print(
        f"\n=== Summary ===\n"
        f"Size vs stroke:    best shape = {size_shape} (R^2={size_r2:.4f})\n"
        f"Voltage vs stroke: best shape = {volt_shape} (R^2={volt_r2:.4f})\n"
        f"Voltage vs size (combined): see shape_fit_comparison.csv above\n"
        "\nCAVEAT: n=7 is a small sample for discriminating between curve shapes -- several shapes "
        "can fit similarly well by chance this few points. Treat winning shapes as 'best-supported "
        "by this data', not definitively proven. More matched CV/UAS points would make this more robust."
    )


if __name__ == "__main__":
    main()

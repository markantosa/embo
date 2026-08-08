"""
Step-up from determine_relationship_shape.py, addressing two remaining gaps:

1. That script's R^2 comparison picks the best SHAPE, but never checked
   whether a relationship exists at all before comparing shapes. Spearman
   rank correlation is the right tool for that first question: it tests only
   whether voltage and size move in a consistent order (higher voltage <=>
   smaller size), with NO assumption about linearity or any other shape --
   a cleaner "is there a real relationship" check than jumping straight to
   curve-fitting.

2. The power-law fit in determine_relationship_shape.py was done with
   scipy.curve_fit directly on raw (voltage, size) values, minimizing
   ABSOLUTE squared error. Since stroke 0's size (~5195um) is 5-10x larger
   than the other 6 points, that fit is dominated by getting stroke 0 close,
   and barely constrained by the other 6 -- the classic small-n, high-leverage
   problem. Fitting in LOG-LOG space instead (linear regression of
   log(size) vs log(voltage)) minimizes RELATIVE (proportional) error
   uniformly across all points, which is the standard way to fit a power law
   without one large point dominating. This also gives a proper confidence
   interval on the exponent from ordinary linear regression theory.

A leave-one-out sensitivity check (refit excluding stroke 0) is included
specifically because n=7 with one high-leverage point means the exponent
estimate could be fragile -- better to show that fragility than hide it
behind a single R^2 number.
"""
import glob
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

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
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if _leading_number(entry) == stroke:
            csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
            df = pd.read_csv(csvs[0])
            row = df.iloc[(df["freq_hz"] - REFERENCE_FREQ_HZ).abs().argsort()[:1]]
            return float(row["mean_uas_volts"].values[0])
    raise FileNotFoundError(stroke)


def loglog_power_fit(voltage, size):
    """Fit size = a * voltage^b via linear regression in log-log space.

    Returns dict with exponent b, its 95% CI, a, R^2 (in log-log space),
    p-value of the regression slope, and per-point residual % in raw space.
    """
    log_v = np.log(voltage)
    log_s = np.log(size)
    slope, intercept, r_value, p_value, std_err = stats.linregress(log_v, log_s)
    n = len(voltage)
    df = n - 2
    t_crit = stats.t.ppf(0.975, df)
    ci = t_crit * std_err
    a = math.exp(intercept)
    b = slope
    predicted_size = a * np.power(voltage, b)
    residual_pct = (size - predicted_size) / size * 100
    return {
        "a": a, "b": b, "b_ci95": ci, "b_low": b - ci, "b_high": b + ci,
        "r2_loglog": r_value ** 2, "p_value": p_value, "n": n,
        "predicted_size": predicted_size, "residual_pct": residual_pct,
    }


def main():
    rows = []
    for s in STROKES:
        rows.append({
            "stroke": s,
            "cv_median_size_um": cv_median_size(s),
            "uas_voltage_v": uas_voltage_at_reference_freq(s),
        })
    data = pd.DataFrame(rows)
    voltage = data["uas_voltage_v"].to_numpy()
    size = data["cv_median_size_um"].to_numpy()

    # ---- Step 1: is there a relationship at all? Spearman rank correlation ----
    rho, spearman_p = stats.spearmanr(voltage, size)
    print(f"Step 1 -- Spearman rank correlation: rho = {rho:.4f}, p = {spearman_p:.5f}, n = {len(voltage)}")
    print("(Tests only whether voltage and size move in a consistent order -- no shape assumed.)")

    # ---- Step 2: fit power law in log-log space (full dataset) ----
    fit_full = loglog_power_fit(voltage, size)
    print(f"\nStep 2 -- Power-law fit (log-log regression), all n={fit_full['n']} points:")
    print(f"  size = {fit_full['a']:.1f} x voltage^{fit_full['b']:.2f}")
    print(f"  exponent b = {fit_full['b']:.2f} +/- {fit_full['b_ci95']:.2f} "
          f"(95% CI: {fit_full['b_low']:.2f} to {fit_full['b_high']:.2f})")
    print(f"  R^2 (log-log) = {fit_full['r2_loglog']:.4f}, p = {fit_full['p_value']:.5f}")
    print(f"  residual %% by stroke: " +
          ", ".join(f"stroke{s}={r:+.1f}%" for s, r in zip(data['stroke'], fit_full['residual_pct'])))

    # ---- Sensitivity check: refit excluding stroke 0 (the high-leverage point) ----
    mask = data["stroke"] != 0
    fit_excl0 = loglog_power_fit(voltage[mask], size[mask])
    print(f"\nSensitivity check -- excluding stroke 0 (size={size[0]:.0f}um, "
          f"{size[0]/np.median(size[mask]):.1f}x the median of the rest):")
    print(f"  exponent b = {fit_excl0['b']:.2f} +/- {fit_excl0['b_ci95']:.2f}, "
          f"R^2 (log-log) = {fit_excl0['r2_loglog']:.4f}")
    print(
        f"  => exponent shifts from {fit_full['b']:.2f} to {fit_excl0['b']:.2f} when stroke 0 is "
        "dropped -- the exponent estimate is sensitive to this one point, so report it as a range, "
        "not a precise value, until more data is collected."
    )

    # ---- Save data table ----
    out = data.copy()
    out["predicted_size_um"] = fit_full["predicted_size"]
    out["residual_pct"] = fit_full["residual_pct"]
    out.to_csv(os.path.join(OUT_DIR, "spearman_and_loglog_fit_data.csv"), index=False)

    # ---- Figure A: log-log power fit with 95% CI band ----
    fig, ax = plt.subplots(figsize=(9, 7))
    ax.scatter(voltage, size, color="black", zorder=5, s=70, label="Measured data (n=7)")
    for _, row in data.iterrows():
        ax.annotate(f"stroke {row.stroke:.0f}", (row.uas_voltage_v, row.cv_median_size_um),
                    textcoords="offset points", xytext=(6, 6), fontsize=8)

    v_smooth = np.linspace(voltage.min() * 0.95, voltage.max() * 1.05, 200)
    fit_line = fit_full["a"] * np.power(v_smooth, fit_full["b"])
    fit_line_low = fit_full["a"] * np.power(v_smooth, fit_full["b_low"])
    fit_line_high = fit_full["a"] * np.power(v_smooth, fit_full["b_high"])
    ax.plot(v_smooth, fit_line, color="tab:green", linewidth=2,
            label=f"Power-law fit: size = {fit_full['a']:.0f} x V^{fit_full['b']:.2f}")
    ax.fill_between(v_smooth, fit_line_low, fit_line_high, color="tab:green", alpha=0.15,
                     label="95% CI on exponent")

    stats_text = (
        f"Exponent = {fit_full['b']:.2f} (95% CI {fit_full['b_low']:.2f} to {fit_full['b_high']:.2f})\n"
        f"R2 (log-log) = {fit_full['r2_loglog']:.3f}, p = {fit_full['p_value']:.4f}\n"
        f"Spearman rho = {rho:.3f}, p = {spearman_p:.4f}"
    )
    ax.text(0.97, 0.97, stats_text, transform=ax.transAxes, ha="right", va="top", fontsize=9,
            bbox=dict(boxstyle="round", facecolor="white", alpha=0.85))

    ax.set_xlabel(f"UAS voltage @ {REFERENCE_FREQ_HZ/1e6:.2f}MHz (V)")
    ax.set_ylabel("CV median particle size (um)")
    ax.set_title("Power-Law Fit in Log-Log Space, with 95% Confidence Band")
    ax.legend(fontsize=8, loc="upper right", bbox_to_anchor=(1, 0.72))
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "loglog_fit_with_ci.png"), dpi=150)

    # ---- Figure B: residuals by stroke + exponent sensitivity ----
    fig2, (axL, axR) = plt.subplots(1, 2, figsize=(12, 5))

    colors = ["tab:red" if s == 0 else "tab:blue" for s in data["stroke"]]
    axL.bar([str(s) for s in data["stroke"]], fit_full["residual_pct"], color=colors)
    axL.axhline(0, color="black", linewidth=0.8)
    axL.set_xlabel("Stroke")
    axL.set_ylabel("Residual (%) -- (actual - predicted) / actual")
    axL.set_title("Fit Residuals by Stroke\n(red = stroke 0, the high-leverage point)")
    axL.grid(True, alpha=0.3)

    axR.bar(["All 7 points", "Excluding stroke 0"], [fit_full["b"], fit_excl0["b"]],
            yerr=[fit_full["b_ci95"], fit_excl0["b_ci95"]], capsize=6,
            color=["tab:green", "tab:orange"])
    axR.axhline(0, color="black", linewidth=0.8)
    axR.set_ylabel("Fitted exponent (b)")
    axR.set_title("Exponent Sensitivity to Stroke 0")
    axR.grid(True, alpha=0.3)

    fig2.tight_layout()
    fig2.savefig(os.path.join(OUT_DIR, "loglog_fit_sensitivity.png"), dpi=150)

    print("\nSaved:")
    print("  spearman_and_loglog_fit_data.csv")
    print("  loglog_fit_with_ci.png")
    print("  loglog_fit_sensitivity.png")


if __name__ == "__main__":
    main()

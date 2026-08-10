"""
Correction to the original Part E: it assumed matched (frequency, voltage,
size) data only exists at 0.96MHz and would need new data collection to fit
a joint frequency+voltage model. That was wrong -- every one of the 7
CV-matched strokes already has UAS voltage measured across the FULL
0.9-2.0MHz sweep (111 frequencies each), not just 0.96MHz. CV median size is
a single physical measurement per stroke (particle size doesn't depend on
which frequency you probe it at), so it can be paired with EVERY frequency's
voltage reading for that stroke. No new measurements needed.

Model: size = a * voltage^b * freq^c, fit as one multiple linear regression
in log-log space: ln(size) = ln(a) + b*ln(voltage) + c*ln(freq_mhz)

Data used: the 13 frequencies in the transducer's resonance band
(0.94-1.06MHz) x the 7 CV-matched strokes = 91 rows. Restricted to the
resonance band (not the full 111-point sweep) because that's the range
already established as physically meaningful in this report -- frequencies
outside it are noise-dominated/off-resonance and would just add noise to the
fit, not real information.
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
RESONANCE_BAND_HZ = list(range(940_000, 1_060_001, 10_000))  # 13 frequencies

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


def load_stroke_uas(stroke: int) -> pd.DataFrame:
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if _leading_number(entry) == stroke:
            csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
            return pd.read_csv(csvs[0])
    raise FileNotFoundError(stroke)


def main():
    rows = []
    for s in STROKES:
        size = cv_median_size(s)
        df = load_stroke_uas(s)
        for freq_hz in RESONANCE_BAND_HZ:
            match = df.iloc[(df["freq_hz"] - freq_hz).abs().argsort()[:1]]
            voltage = float(match["mean_uas_volts"].values[0])
            rows.append({
                "stroke": s, "freq_mhz": freq_hz / 1e6, "voltage_v": voltage,
                "cv_median_size_um": size,
            })
    data = pd.DataFrame(rows)
    data.to_csv(os.path.join(OUT_DIR, "joint_model_combined_data.csv"), index=False)
    print(f"Combined dataset: {len(data)} rows ({len(STROKES)} strokes x {len(RESONANCE_BAND_HZ)} frequencies)")

    # ---- Fit: ln(size) = ln(a) + b*ln(voltage) + c*ln(freq_mhz) ----
    X = np.column_stack([
        np.ones(len(data)),
        np.log(data["voltage_v"]),
        np.log(data["freq_mhz"]),
    ])
    y = np.log(data["cv_median_size_um"])

    coeffs, residuals_ss, rank, sv = np.linalg.lstsq(X, y, rcond=None)
    ln_a, b, c = coeffs
    a = math.exp(ln_a)

    y_pred = X @ coeffs
    ss_res = np.sum((y - y_pred) ** 2)
    ss_tot = np.sum((y - y.mean()) ** 2)
    r2 = 1 - ss_res / ss_tot
    n, k = X.shape[0], X.shape[1]
    dof = n - k

    # standard errors of coefficients
    sigma2 = ss_res / dof
    cov = sigma2 * np.linalg.inv(X.T @ X)
    se = np.sqrt(np.diag(cov))
    t_crit = stats.t.ppf(0.975, dof)
    ci = t_crit * se

    print(f"\nJoint fit: size = {a:.1f} x voltage^{b:.3f} x freq_mhz^{c:.3f}")
    print(f"  b (voltage exponent) = {b:.3f} +/- {ci[1]:.3f} (95% CI: {b-ci[1]:.3f} to {b+ci[1]:.3f})")
    print(f"  c (frequency exponent) = {c:.3f} +/- {ci[2]:.3f} (95% CI: {c-ci[2]:.3f} to {c+ci[2]:.3f})")
    print(f"  R^2 (log-log, joint) = {r2:.4f}, n = {n}, dof = {dof}")

    # t-test for each coefficient (is it significantly different from 0?)
    t_stats = coeffs / se
    p_values = 2 * (1 - stats.t.cdf(np.abs(t_stats), dof))
    print(f"  p-value (voltage term): {p_values[1]:.6f}")
    print(f"  p-value (frequency term): {p_values[2]:.6f}")

    # ---- Compare to Part D's single-frequency (0.96MHz) fit ----
    single_freq_data = data[data["freq_mhz"] == 0.96]
    X1 = np.column_stack([np.ones(len(single_freq_data)), np.log(single_freq_data["voltage_v"])])
    y1 = np.log(single_freq_data["cv_median_size_um"])
    coeffs1, _, _, _ = np.linalg.lstsq(X1, y1, rcond=None)
    y1_pred = X1 @ coeffs1
    r2_single = 1 - np.sum((y1 - y1_pred) ** 2) / np.sum((y1 - y1.mean()) ** 2)
    print(f"\nFor comparison -- Part D single-frequency (0.96MHz only) fit: "
          f"b={coeffs1[1]:.3f}, R^2={r2_single:.4f}, n={len(single_freq_data)}")

    # ---- Sensitivity: leave stroke 0 out ----
    mask = data["stroke"] != 0
    Xe = np.column_stack([np.ones(mask.sum()), np.log(data.loc[mask, "voltage_v"]), np.log(data.loc[mask, "freq_mhz"])])
    ye = np.log(data.loc[mask, "cv_median_size_um"])
    coeffs_e, _, _, _ = np.linalg.lstsq(Xe, ye, rcond=None)
    ye_pred = Xe @ coeffs_e
    r2_e = 1 - np.sum((ye - ye_pred) ** 2) / np.sum((ye - ye.mean()) ** 2)
    print(f"\nSensitivity -- excluding stroke 0: b={coeffs_e[1]:.3f}, c={coeffs_e[2]:.3f}, R^2={r2_e:.4f}")

    # ---- Sensitivity: leave each frequency out one at a time (range of b, c) ----
    b_range, c_range = [], []
    for freq in data["freq_mhz"].unique():
        m = data["freq_mhz"] != freq
        Xf = np.column_stack([np.ones(m.sum()), np.log(data.loc[m, "voltage_v"]), np.log(data.loc[m, "freq_mhz"])])
        yf = np.log(data.loc[m, "cv_median_size_um"])
        cf, _, _, _ = np.linalg.lstsq(Xf, yf, rcond=None)
        b_range.append(cf[1])
        c_range.append(cf[2])
    print(f"\nLeave-one-frequency-out: b ranges {min(b_range):.3f} to {max(b_range):.3f}, "
          f"c ranges {min(c_range):.3f} to {max(c_range):.3f} (13 refits, one per dropped frequency)")

    # ---- Plot: predicted vs actual (log-log), colored by frequency ----
    fig, ax = plt.subplots(figsize=(8, 7))
    sc = ax.scatter(y, y_pred, c=data["freq_mhz"], cmap="viridis", s=25)
    lims = [min(y.min(), y_pred.min()), max(y.max(), y_pred.max())]
    ax.plot(lims, lims, "k--", linewidth=1, label="Perfect fit")
    cbar = fig.colorbar(sc, ax=ax)
    cbar.set_label("Frequency (MHz)")
    ax.set_xlabel("Actual ln(size)")
    ax.set_ylabel("Predicted ln(size)")
    ax.set_title(f"Joint Model Fit: size = {a:.0f} x V^{b:.2f} x freq^{c:.2f}\nR2={r2:.3f}, n={n} (7 strokes x 13 frequencies)")
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "joint_model_fit.png"), dpi=150)

    # ---- Plot: exponent stability (leave-one-frequency-out) ----
    fig2, (axL, axR) = plt.subplots(1, 2, figsize=(11, 5))
    axL.bar(range(len(b_range)), b_range, color="tab:blue")
    axL.axhline(b, color="black", linestyle="--", label=f"Full-data b={b:.2f}")
    axL.set_xlabel("Which frequency was dropped (index)")
    axL.set_ylabel("Voltage exponent (b)")
    axL.set_title("Voltage Exponent Stability\n(leave-one-frequency-out)")
    axL.legend()
    axL.grid(True, alpha=0.3)

    axR.bar(range(len(c_range)), c_range, color="tab:orange")
    axR.axhline(c, color="black", linestyle="--", label=f"Full-data c={c:.2f}")
    axR.set_xlabel("Which frequency was dropped (index)")
    axR.set_ylabel("Frequency exponent (c)")
    axR.set_title("Frequency Exponent Stability\n(leave-one-frequency-out)")
    axR.legend()
    axR.grid(True, alpha=0.3)
    fig2.tight_layout()
    fig2.savefig(os.path.join(OUT_DIR, "joint_model_sensitivity.png"), dpi=150)

    print("\nSaved:")
    print("  joint_model_combined_data.csv")
    print("  joint_model_fit.png")
    print("  joint_model_sensitivity.png")


if __name__ == "__main__":
    main()

"""
Concern being tested: assigning particle size directly from ABSOLUTE UAS
voltage may not generalize, because different sessions/runs start from a
different baseline voltage (rig setup, fluid fill level, small electronic
offset differences, etc. all shift the whole curve up/down). If the sensor
tracks CHANGE in voltage from that session's own starting point instead of
the raw value, it should be more robust to those session-to-session offset
differences.

Part 1: refit the 7-point Part D dataset using delta-voltage instead of
absolute voltage (delta = voltage - that session's own stroke-0 baseline),
and compare fit quality (R^2) to the original absolute-voltage fit.

Part 2: an actual cross-session consistency test, using data not used
before -- the 5 independent "1 sample" repeat sessions in 26 gain/ each have
their own stroke 0-5 sweep with their OWN baseline. Their stroke-5 readings
correspond to the 5 independent CV samples in
software/Microscope_Imaging_Data/variance_study_stroke5_summary.csv (5
separate syringe batches, all mixed to 5 strokes, imaged separately). This
lets us test: does delta-voltage (measured independently in each of the 5
sessions, each with a different starting baseline) predict particle size
consistently with the main power-law curve? If yes, that's real evidence
delta-voltage generalizes across sessions better than absolute voltage would.
"""
import glob
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

REFERENCE_FREQ_HZ = 960_000
STROKES_MAIN = [0, 1, 5, 10, 15, 20, 25]

CV_DIR = r"C:\Users\LOQ\Documents\Github repo\embo\software\Microscope_Imaging_Data\Stroke_Tabulated_Data"
VARIANCE_CSV = r"C:\Users\LOQ\Documents\Github repo\embo\software\Microscope_Imaging_Data\variance_study_stroke5_summary.csv"
MAIN_UAS_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
GAIN26_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain"
)
SAMPLE_SESSIONS = [
    "test 5 stroke  1 sample (1st sample)",
    "test 5 stroke  1 sample (2nd sample )",
    "test 5 stroke  1 sample (3rd sample)",
    "test 5 stroke  1 sample (4th sample)",
    "test 5 stroke  1 sample (5th sample)",
]
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


def voltage_at_freq(csv_path, freq_hz=REFERENCE_FREQ_HZ):
    df = pd.read_csv(csv_path)
    row = df.iloc[(df["freq_hz"] - freq_hz).abs().argsort()[:1]]
    return float(row["mean_uas_volts"].values[0])


def voltage_for_stroke_in_dir(base_dir, stroke):
    for entry in os.listdir(base_dir):
        full_path = os.path.join(base_dir, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if any(ex in entry for ex in ["cross check with oscilloscope", "ambient", "jelly saline"]):
            continue
        if _leading_number(entry) == stroke:
            csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
            if csvs:
                return voltage_at_freq(csvs[0])
    raise FileNotFoundError(f"stroke {stroke} not found in {base_dir}")


def loglog_power_fit(x, y):
    log_x, log_y = np.log(x), np.log(y)
    slope, intercept, r_value, p_value, std_err = stats.linregress(log_x, log_y)
    return {"a": np.exp(intercept), "b": slope, "r2": r_value ** 2, "p": p_value}


def main():
    # ---- Part 1: main dataset, absolute voltage vs delta voltage ----
    rows = []
    baseline_v = voltage_for_stroke_in_dir(MAIN_UAS_DIR, 0)
    for s in STROKES_MAIN:
        v = voltage_for_stroke_in_dir(MAIN_UAS_DIR, s)
        size = cv_median_size(s)
        rows.append({
            "stroke": s, "uas_voltage_v": v, "delta_voltage_v": v - baseline_v,
            "cv_median_size_um": size,
        })
    main_data = pd.DataFrame(rows)
    main_data.to_csv(os.path.join(OUT_DIR, "delta_voltage_vs_size_main.csv"), index=False)
    print("Main dataset (0.96MHz), baseline (stroke 0) =", round(baseline_v, 4), "V")
    print(main_data.to_string(index=False))

    fit_abs = loglog_power_fit(main_data["uas_voltage_v"], main_data["cv_median_size_um"])
    # log(0) is undefined, and delta voltage at stroke 0 is 0 BY DEFINITION
    # (it's the baseline itself) -- exclude it from the log-log power fit,
    # same as any log-log regression must exclude a zero x-value. This is a
    # real limitation of pairing "delta from baseline" with a power-law form
    # specifically: the model can't be evaluated at the baseline point itself.
    nonzero = main_data["delta_voltage_v"] > 0
    fit_delta = loglog_power_fit(main_data.loc[nonzero, "delta_voltage_v"], main_data.loc[nonzero, "cv_median_size_um"])
    print(f"(delta-voltage fit excludes stroke 0, where delta=0 by definition -- log-log power fit needs x>0; n={nonzero.sum()})")
    print(f"\nAbsolute voltage fit:  size = {fit_abs['a']:.1f} x V^{fit_abs['b']:.2f}, "
          f"R2={fit_abs['r2']:.4f}, p={fit_abs['p']:.5f}")
    print(f"Delta voltage fit:     size = {fit_delta['a']:.1f} x dV^{fit_delta['b']:.2f}, "
          f"R2={fit_delta['r2']:.4f}, p={fit_delta['p']:.5f}")

    # ---- Part 2: cross-session consistency check using the 5 independent samples ----
    variance_df = pd.read_csv(VARIANCE_CSV, nrows=5)  # first 5 rows = sample1..sample5
    session_rows = []
    for i, session in enumerate(SAMPLE_SESSIONS, start=1):
        session_dir = os.path.join(GAIN26_DIR, session)
        v0 = voltage_for_stroke_in_dir(session_dir, 0)
        v5 = voltage_for_stroke_in_dir(session_dir, 5)
        cv_size = float(variance_df.loc[variance_df["sample_id"] == f"sample{i}", "median_ecd_um"].values[0])
        session_rows.append({
            "sample": f"sample{i}", "baseline_v": v0, "stroke5_v": v5,
            "delta_voltage_v": v5 - v0, "cv_median_size_um": cv_size,
        })
    session_data = pd.DataFrame(session_rows)
    session_data.to_csv(os.path.join(OUT_DIR, "delta_voltage_cross_session_check.csv"), index=False)
    print("\nCross-session check (5 independent stroke-5 samples, own baseline each):")
    print(session_data.to_string(index=False))
    print(f"\nBaseline voltage range across sessions: {session_data.baseline_v.min():.3f} - "
          f"{session_data.baseline_v.max():.3f} V (spread of {session_data.baseline_v.max()-session_data.baseline_v.min():.3f} V "
          "-- this is exactly the session-to-session offset the delta approach is meant to cancel out)")

    # predict size from the MAIN delta-voltage fit, compare to actual CV size for each session
    session_data["predicted_size_from_delta_fit"] = fit_delta["a"] * np.power(session_data["delta_voltage_v"], fit_delta["b"])
    session_data["residual_pct"] = (
        (session_data["cv_median_size_um"] - session_data["predicted_size_from_delta_fit"])
        / session_data["cv_median_size_um"] * 100
    )
    print("\nHow well does the main delta-voltage power-law curve predict these 5 independent sessions?")
    print(session_data[["sample", "delta_voltage_v", "cv_median_size_um", "predicted_size_from_delta_fit", "residual_pct"]].to_string(index=False))
    print(f"Mean |residual|: {session_data['residual_pct'].abs().mean():.1f}%")

    # ---- Plots ----
    fig, axes = plt.subplots(1, 2, figsize=(13, 6))
    axes[0].scatter(main_data["uas_voltage_v"], main_data["cv_median_size_um"], color="tab:blue", s=60, label="Main data (n=7)")
    v_smooth = np.linspace(main_data["uas_voltage_v"].min(), main_data["uas_voltage_v"].max(), 100)
    axes[0].plot(v_smooth, fit_abs["a"] * np.power(v_smooth, fit_abs["b"]), color="tab:blue",
                 label=f"Fit: R2={fit_abs['r2']:.3f}")
    axes[0].set_xlabel("Absolute UAS voltage (V)")
    axes[0].set_ylabel("CV median particle size (um)")
    axes[0].set_title("Absolute Voltage vs Size")
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    axes[1].scatter(main_data["delta_voltage_v"], main_data["cv_median_size_um"], color="tab:green", s=60, label="Main data (n=7)")
    dv_smooth = np.linspace(main_data.loc[nonzero, "delta_voltage_v"].min(), main_data["delta_voltage_v"].max(), 100)
    axes[1].plot(dv_smooth, fit_delta["a"] * np.power(dv_smooth, fit_delta["b"]), color="tab:green",
                 label=f"Main fit: R2={fit_delta['r2']:.3f}")
    axes[1].scatter(session_data["delta_voltage_v"], session_data["cv_median_size_um"], color="tab:red",
                     marker="^", s=80, zorder=5, label="5 independent sessions (own baseline each)")
    for _, row in session_data.iterrows():
        axes[1].annotate(row["sample"], (row.delta_voltage_v, row.cv_median_size_um),
                          textcoords="offset points", xytext=(6, 6), fontsize=8)
    axes[1].set_xlabel("Delta voltage from session's own baseline (V)")
    axes[1].set_ylabel("CV median particle size (um)")
    axes[1].set_title("Delta Voltage vs Size\n(+ 5 independent cross-session checks)")
    axes[1].legend(fontsize=8)
    axes[1].grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "delta_voltage_vs_size.png"), dpi=150)

    print("\nSaved:")
    print("  delta_voltage_vs_size_main.csv")
    print("  delta_voltage_cross_session_check.csv")
    print("  delta_voltage_vs_size.png")


if __name__ == "__main__":
    main()

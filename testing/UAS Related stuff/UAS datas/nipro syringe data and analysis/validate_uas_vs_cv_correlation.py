"""
Goal (Step 2 / "Goal C"): validate whether UAS voltage and CV-measured particle
size move together in a consistent, correlated way across stroke count.

Why this matters: UAS voltage on its own only proves the SENSOR is behaving
consistently -- it says nothing about whether it's actually tracking real
particle size. CV (microscope) gives the independent ground truth. If both
move together in a statistically real way, that's evidence the UAS reading is
a genuine size proxy, not just a self-consistent but meaningless number.

Data sources (both use the SAME stroke-counting convention, confirmed
consistent across UAS and CV per team verification -- Step 0):
  - CV:   software/Microscope_Imaging_Data/Stroke_Tabulated_Data/
          one CSV per stroke, one row per individual particle measured.
  - UAS:  testing/UAS Related stuff/UAS datas/nipro syringe data and analysis/
          26 gain/test without taking out the saline and jelly/
          one folder per stroke, uas_averaged_summary_*.csv has one row per
          swept frequency (mean_uas_volts averaged over ~9-18 samples).

Method:
  1. Compute CV median particle size per stroke from the raw per-particle CSVs
     (median is used, not mean, because particle-size distributions are
     typically right-skewed -- a few large chunks would otherwise pull the
     mean up and misrepresent the "typical" particle).
  2. Pull the UAS voltage at a fixed reference frequency (0.96MHz) for the
     same strokes. 0.96MHz was chosen deliberately, not arbitrarily:
       - it falls inside the transducer's confirmed resonance/operating band
         (0.94-1.06MHz, see transducer_working_range_benchmark.py), so it's
         a frequency the final system would actually use
       - it stays clean/unclipped across this whole stroke range (unlike the
         ~1.0MHz peak, which saturates against the ADC ceiling from stroke 2
         onward -- see the peak-clipping analysis earlier this project)
       - within that resonance band, it was shown to be statistically safe
         from the syringe-brand effect (see syringe_significance_full_spectrum.py
         -- 0.96MHz is one of the "green" frequencies there)
  3. Plot both trends on a dual-axis chart, and a separate size-vs-voltage
     scatter, as RAW DATA ONLY -- no correlation statistic is computed here.

IMPORTANT: this script deliberately does NOT compute Pearson correlation.
Pearson r only tests for a LINEAR relationship, and determine_relationship_shape.py
shows the true relationship (in both the individual stroke-vs-size and
stroke-vs-voltage curves, and their combination) is curved, not linear -- so
Pearson r is the wrong tool here, not just an incomplete one. See
determine_relationship_shape.py for the actual shape-fit method (R^2 per
candidate curve shape) used to quantify this relationship.
"""
import glob
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

STROKES = [0, 1, 5, 10, 15, 20, 25]
REFERENCE_FREQ_HZ = 960_000

CV_DIR = r"C:\Users\LOQ\Documents\Github repo\embo\software\Microscope_Imaging_Data\Stroke_Tabulated_Data"
UAS_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
OUT_DIR = os.path.dirname(os.path.abspath(__file__))


def cv_median_size(stroke: int) -> float:
    """Median particle size (um) for one stroke, from the raw per-particle CV data."""
    # filenames aren't perfectly consistent (stroke1 has a " (1)" suffix), so
    # glob instead of hardcoding the exact name.
    matches = glob.glob(os.path.join(CV_DIR, f"particle_measurements_stroke{stroke} *.csv"))
    matches += glob.glob(os.path.join(CV_DIR, f"particle_measurements_stroke{stroke}.csv"))
    if not matches:
        raise FileNotFoundError(f"No CV file found for stroke {stroke}")
    df = pd.read_csv(matches[0])
    return df["particle_size_um"].median()


def _leading_number(folder_name: str) -> int | None:
    """Extract the leading integer from a folder name like '10th stroke' -> 10.

    IMPORTANT: a naive glob like '1*stroke*' also matches '10th stroke',
    '11th stroke', etc. (anything starting with "1") -- that bug silently
    duplicated stroke 10's data into stroke 1 in an earlier version of this
    script. Parsing the exact leading number and comparing with == avoids it.
    """
    digits = ""
    for ch in folder_name.strip():
        if ch.isdigit():
            digits += ch
        else:
            break
    return int(digits) if digits else None


def uas_voltage_at_reference_freq(stroke: int) -> float:
    """UAS voltage (V) closest to REFERENCE_FREQ_HZ, for one stroke."""
    match = None
    for entry in os.listdir(UAS_DIR):
        full_path = os.path.join(UAS_DIR, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if _leading_number(entry) == stroke:
            match = full_path
            break
    if match is None:
        raise FileNotFoundError(f"No UAS folder found for stroke {stroke}")
    csvs = glob.glob(os.path.join(match, "uas_averaged_summary_*.csv"))
    if not csvs:
        raise FileNotFoundError(f"No uas_averaged_summary CSV in {candidates[0]}")
    df = pd.read_csv(csvs[0])
    nearest_row = df.iloc[(df["freq_hz"] - REFERENCE_FREQ_HZ).abs().argsort()[:1]]
    return float(nearest_row["mean_uas_volts"].values[0])


def pearson_r_and_p(x, y):
    """Pearson correlation + two-tailed p-value, no scipy dependency.

    p-value uses the exact closed-form relationship between the
    t-distribution and the regularized incomplete beta function -- the same
    approach used earlier in this project's significance testing, so results
    are directly comparable.
    """
    n = len(x)
    mx, my = sum(x) / n, sum(y) / n
    cov = sum((xi - mx) * (yi - my) for xi, yi in zip(x, y))
    sx = math.sqrt(sum((xi - mx) ** 2 for xi in x))
    sy = math.sqrt(sum((yi - my) ** 2 for yi in y))
    r = cov / (sx * sy)

    df = n - 2
    t = r * math.sqrt(df) / math.sqrt(1 - r**2)
    p = _betai(df / 2, 0.5, df / (df + t * t))
    return r, p


def _betacf(a, b, x):
    MAXIT, EPS, FPMIN = 200, 3e-14, 1e-300
    qab, qap, qam = a + b, a + 1, a - 1
    c = 1.0
    d = 1.0 - qab * x / qap
    if abs(d) < FPMIN:
        d = FPMIN
    d = 1.0 / d
    h = d
    for m in range(1, MAXIT + 1):
        m2 = 2 * m
        aa = m * (b - m) * x / ((qam + m2) * (a + m2))
        d = 1.0 + aa * d
        if abs(d) < FPMIN:
            d = FPMIN
        c = 1.0 + aa / c
        if abs(c) < FPMIN:
            c = FPMIN
        d = 1.0 / d
        h *= d * c
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2))
        d = 1.0 + aa * d
        if abs(d) < FPMIN:
            d = FPMIN
        c = 1.0 + aa / c
        if abs(c) < FPMIN:
            c = FPMIN
        d = 1.0 / d
        de = d * c
        h *= de
        if abs(de - 1.0) < EPS:
            break
    return h


def _betai(a, b, x):
    if x <= 0:
        return 0.0
    if x >= 1:
        return 1.0
    bt = math.exp(
        math.lgamma(a + b) - math.lgamma(a) - math.lgamma(b) + a * math.log(x) + b * math.log(1 - x)
    )
    if x < (a + 1) / (a + b + 2):
        return bt * _betacf(a, b, x) / a
    return 1.0 - bt * _betacf(b, a, 1 - x) / b


def main():
    rows = []
    for s in STROKES:
        rows.append(
            {
                "stroke": s,
                "cv_median_size_um": cv_median_size(s),
                "uas_voltage_v": uas_voltage_at_reference_freq(s),
            }
        )
    result = pd.DataFrame(rows)
    result.to_csv(os.path.join(OUT_DIR, "uas_vs_cv_correlation_data.csv"), index=False)
    print(result.to_string(index=False))
    print(
        "\nNOTE: this script only produces the raw trend/calibration plots and the data "
        "table. Pearson r is NOT computed here -- it assumes a linear relationship, and "
        "determine_relationship_shape.py shows this relationship is a power law, not linear. "
        "See that script for the actual quantitative fit (R^2 per candidate shape)."
    )

    # ---- Plot 1: both trends vs stroke count, dual axis (raw data, no fit/stats claim) ----
    fig, ax1 = plt.subplots(figsize=(11, 6))
    color1 = "tab:blue"
    ax1.set_xlabel("Stroke count")
    ax1.set_ylabel("CV median particle size (um)", color=color1)
    ax1.plot(result["stroke"], result["cv_median_size_um"], marker="o", color=color1, label="CV median size")
    ax1.tick_params(axis="y", labelcolor=color1)

    ax2 = ax1.twinx()
    color2 = "tab:red"
    ax2.set_ylabel(f"UAS voltage @ {REFERENCE_FREQ_HZ/1e6:.2f}MHz (V)", color=color2)
    ax2.plot(result["stroke"], result["uas_voltage_v"], marker="s", color=color2, label="UAS voltage")
    ax2.tick_params(axis="y", labelcolor=color2)

    fig.suptitle("UAS Voltage and CV Median Particle Size Across Strokes (raw data)")
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "uas_vs_cv_trend_comparison.png"), dpi=150)

    # ---- Plot 2: direct calibration-curve view (voltage on x, size on y), raw data only ----
    fig2, ax = plt.subplots(figsize=(9, 6))
    ax.plot(result["uas_voltage_v"], result["cv_median_size_um"], marker="o", markersize=8)
    for _, row in result.iterrows():
        ax.annotate(f"stroke {row.stroke:.0f}", (row.uas_voltage_v, row.cv_median_size_um),
                    textcoords="offset points", xytext=(8, 8), fontsize=9)
    ax.set_xlabel(f"UAS voltage @ {REFERENCE_FREQ_HZ/1e6:.2f}MHz (V)")
    ax.set_ylabel("CV median particle size (um)")
    ax.set_title(f"Voltage vs Size, Raw Data (n={len(result)}) -- see shape_fit_comparison.png for the fitted curve")
    ax.grid(True, alpha=0.3)
    fig2.tight_layout()
    fig2.savefig(os.path.join(OUT_DIR, "uas_vs_cv_calibration_curve.png"), dpi=150)

    print("\nSaved:")
    print("  uas_vs_cv_correlation_data.csv")
    print("  uas_vs_cv_trend_comparison.png")
    print("  uas_vs_cv_calibration_curve.png")


if __name__ == "__main__":
    main()

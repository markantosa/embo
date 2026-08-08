"""
Goal (Step 4 / "Goal A"): does syringe brand (BD vs nipro) systematically bias
the UAS voltage reading, and if so, at which frequencies specifically?

Why a PAIRED test, not a simple mean comparison:
  Both syringes were swept across the same stroke range, so stroke count is a
  natural pairing variable. A plain "compare average BD voltage vs average
  nipro voltage" would conflate two different sources of variation: the huge,
  expected swing driven by stroke count (mixing progress) and the much
  smaller, syringe-specific offset we actually want to isolate. Pairing by
  stroke (BD_stroke_N minus nipro_stroke_N, for every N) cancels out the
  stroke-driven variation and leaves just the syringe effect -- this is the
  same reasoning used earlier in this project for the single-frequency
  version of this test; here we just repeat it at EVERY frequency in the
  sweep instead of picking one or two by hand.

Why every frequency, not just one:
  Testing only 0.89MHz and 0.96MHz earlier found one frequency where the
  syringe difference was highly significant (0.89MHz, p<0.0000001) and one
  where it wasn't (0.96MHz, p=0.14). That's only two samples of a 111-point
  spectrum -- not enough to know whether "most of the spectrum is safe" or
  "we got lucky picking 0.96MHz". Running the same paired test at every
  frequency turns that into an actual map.

A note on folder-name matching: a naive glob pattern like "1*stroke*" also
matches "10th stroke", "11th stroke", etc. (anything starting with "1") --
this exact bug silently corrupted a stroke-1 data point earlier in this
project (it silently substituted stroke 10's data instead). This script
parses the leading integer from each folder name and compares with `==`
instead of using a wildcard match, specifically to avoid repeating that bug.

Output:
  - syringe_pvalue_by_frequency.csv : full table, one row per frequency
  - syringe_significance_spectrum.png : p-value vs frequency, with the
    alpha=0.05 line marked, so you can see at a glance which parts of the
    spectrum are syringe-sensitive vs safe.
"""
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BD_DIR = r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas\BD syringe\26 x gain"
NIPRO_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
OUT_DIR = os.path.dirname(os.path.abspath(__file__))
ALPHA = 0.05

# Statistical significance alone is misleading here: with n=40 paired points,
# even a trivially small, consistent difference (0.02-0.03V) comes out as
# "significant" (p as small as 1e-30) simply because the sample size is large
# enough to detect it precisely -- not because it's a meaningful difference.
#
# PRACTICAL_THRESHOLD_V is NOT a guess -- it's derived in
# transducer_working_range_benchmark.py from the ambient (TX-not-driving-fluid)
# benchmark data: it's the median stroke-driven signal swing OUTSIDE the
# transducer's resonance band (~0.90-1.06MHz, confirmed via that script). A
# syringe-vs-syringe difference smaller than "how much the fluid itself can
# move the reading when off-resonance" isn't distinguishable from ordinary
# off-resonance system wobble, so it isn't practically meaningful. Rerun
# transducer_working_range_benchmark.py and update this constant if new
# ambient/stroke data changes that estimate.
PRACTICAL_THRESHOLD_V = 0.0587

# Transducer's real acoustic resonance band, from the same benchmark script:
# peak fluid-driven response is 1.7183V at 1.01MHz; half-max ("-3dB bandwidth")
# threshold is 0.8592V; the widest contiguous run of frequencies clearing that
# bar is 0.94-1.06MHz (0.92MHz alone also clears it but is an isolated blip
# flanked by sub-threshold neighbors on both sides, so it's excluded as noise
# rather than sustained resonance). Shown on the plot for context: this is
# the only band where the fluid signal itself is strong, so it's also the
# only band where a syringe-swap effect is actually being measured against a
# large signal rather than sitting somewhere in the electronics' subtler
# frequency-dependent quirks.
RESONANCE_BAND_MHZ = (0.94, 1.06)

# Restrict the whole analysis to the operating range the system actually
# uses (per project decision), rather than the full 0.9-2.0MHz diagnostic
# sweep. The full sweep was only ever a tool to find where the transducer
# and the syringe effect live; frequencies outside this range aren't part
# of the design and reporting "significant in X% of the full sweep" invites
# exactly the wrong comparison.
FREQ_RANGE_MHZ = (0.94, 1.06)


def leading_number(folder_name: str):
    """'10th stroke' -> 10. Returns None if the folder name doesn't start with a digit."""
    digits = ""
    for ch in folder_name.strip():
        if ch.isdigit():
            digits += ch
        else:
            break
    return int(digits) if digits else None


def load_all_strokes(base_dir: str, exclude_substrings=()) -> dict[int, pd.DataFrame]:
    """stroke number -> full uas_averaged_summary dataframe (all frequencies)."""
    result = {}
    for entry in os.listdir(base_dir):
        full_path = os.path.join(base_dir, entry)
        if not os.path.isdir(full_path) or "stroke" not in entry.lower():
            continue
        if any(ex in entry for ex in exclude_substrings):
            continue
        stroke = leading_number(entry)
        if stroke is None:
            continue
        import glob
        csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
        if not csvs:
            continue
        # if a stroke folder somehow has duplicates, keep first and warn --
        # better to notice than to silently average/overwrite.
        if stroke in result:
            print(f"WARNING: duplicate stroke {stroke} folder in {base_dir} ({entry}) -- keeping first")
            continue
        result[stroke] = pd.read_csv(csvs[0])
    return result


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


def paired_t_test(diffs: list[float]):
    """Two-tailed paired t-test on a list of (BD - nipro) differences at one frequency."""
    n = len(diffs)
    mean_diff = sum(diffs) / n
    variance = sum((d - mean_diff) ** 2 for d in diffs) / (n - 1)
    sd = math.sqrt(variance)
    se = sd / math.sqrt(n)
    if se == 0:
        return mean_diff, float("inf"), 0.0
    t = mean_diff / se
    df = n - 1
    p = _betai(df / 2, 0.5, df / (df + t * t))
    return mean_diff, t, p


def main():
    bd_strokes = load_all_strokes(BD_DIR, exclude_substrings=["jelly and saline"])
    nipro_strokes = load_all_strokes(NIPRO_DIR, exclude_substrings=["cross check with oscilloscope"])

    common_strokes = sorted(set(bd_strokes) & set(nipro_strokes))
    print(f"Matched strokes (present in both syringes): {len(common_strokes)} -> {common_strokes}")

    # Both datasets share the exact same grid (confirmed 900kHz-2000kHz,
    # 10kHz steps, 111 points), so no rounding/alignment needed. Restricted
    # to FREQ_RANGE_MHZ -- the system's actual operating range -- rather than
    # the full diagnostic sweep.
    all_freqs = sorted(bd_strokes[common_strokes[0]]["freq_hz"].unique())
    all_freqs = [f for f in all_freqs
                 if FREQ_RANGE_MHZ[0] <= f / 1e6 <= FREQ_RANGE_MHZ[1]]

    rows = []
    raw_rows = []  # every underlying (freq, stroke) pair -- for the combined Excel export
    for freq in all_freqs:
        diffs = []
        for s in common_strokes:
            bd_row = bd_strokes[s][bd_strokes[s]["freq_hz"] == freq]
            nipro_row = nipro_strokes[s][nipro_strokes[s]["freq_hz"] == freq]
            if bd_row.empty or nipro_row.empty:
                continue
            bd_v = float(bd_row["mean_uas_volts"].values[0])
            nipro_v = float(nipro_row["mean_uas_volts"].values[0])
            diffs.append(bd_v - nipro_v)
            raw_rows.append({
                "freq_mhz": freq / 1e6,
                "stroke": s,
                "bd_voltage_v": bd_v,
                "nipro_voltage_v": nipro_v,
                "diff_v": bd_v - nipro_v,
            })
        if len(diffs) < 3:
            continue  # not enough matched points to test meaningfully at this frequency
        mean_diff, t, p = paired_t_test(diffs)
        rows.append({
            "freq_mhz": freq / 1e6,
            "mean_diff_v": mean_diff,
            "t_stat": t,
            "p_value": p,
            "n_pairs": len(diffs),
            "significant": p < ALPHA,
        })

    result = pd.DataFrame(rows)
    # Practically meaningful = statistically significant AND large enough to
    # actually matter (see PRACTICAL_THRESHOLD_V comment above). Statistical
    # significance alone, at n=40, flags differences far smaller than the
    # per-stroke signal (~0.1-0.2V) as "significant" -- that's true but not
    # useful for deciding whether syringe choice matters in practice.
    result["practically_meaningful"] = result["significant"] & (result["mean_diff_v"].abs() >= PRACTICAL_THRESHOLD_V)
    result.to_csv(os.path.join(OUT_DIR, "syringe_pvalue_by_frequency.csv"), index=False)

    n_sig = result["significant"].sum()
    n_practical = result["practically_meaningful"].sum()
    print(f"\n{n_sig} / {len(result)} frequencies are statistically significant (p<{ALPHA})")
    print(f"{n_practical} / {len(result)} are ALSO practically meaningful (|diff| >= {PRACTICAL_THRESHOLD_V}V)")
    print(
        f"\n=> {n_sig - n_practical} frequencies are 'significant' only because n=40 is large enough "
        "to detect a tiny, consistent offset -- not because the offset is big enough to matter for "
        "syringe interchangeability."
    )
    print("\nFrequencies where syringe difference is BOTH significant AND practically meaningful:")
    print(result[result.practically_meaningful][["freq_mhz", "mean_diff_v", "p_value"]].to_string(index=False))
    print("\nA few examples of 'significant but trivial' (p<0.05 yet |diff| < threshold):")
    trivial = result[result.significant & ~result.practically_meaningful]
    print(trivial[["freq_mhz", "mean_diff_v", "p_value"]].head(10).to_string(index=False))
    print("\nA few examples where it does NOT matter at all (not significant):")
    print(result[~result.significant][["freq_mhz", "mean_diff_v", "p_value"]].head(10).to_string(index=False))

    n_meaningful = result["practically_meaningful"].sum()
    print(
        f"\nWithin the transducer's resonance band, {FREQ_RANGE_MHZ[0]}-{FREQ_RANGE_MHZ[1]}MHz "
        f"({len(result)} frequencies): {n_meaningful}/{len(result)} "
        f"({n_meaningful/len(result)*100:.0f}%) show a syringe difference big enough to matter."
    )

    # ---- Plot: single merged view -----------------------------------------
    # Previously this was two separate panels (raw diff on top, p-value on
    # bottom) -- readers had to cross-reference both to answer one question:
    # "where does syringe brand matter, how much, and in which direction?"
    # This version answers all three in one plot: the line shows direction +
    # magnitude, the point color shows the significance category, and the
    # threshold is a shaded band instead of a hard-to-see dotted line.
    # Two categories only, not three: a frequency that's statistically
    # significant but below the practical threshold is small enough that it
    # isn't distinguishable from ordinary system wobble (see
    # PRACTICAL_THRESHOLD_V above) -- for the purpose of this chart, that's
    # "doesn't matter", same bucket as "not significant at all".
    colors = ["tab:red" if m else "tab:green" for m in result["practically_meaningful"]]

    fig, ax = plt.subplots(figsize=(11, 7))

    # No resonance-band shading here: the whole analyzed range IS the
    # resonance band (FREQ_RANGE_MHZ == RESONANCE_BAND_MHZ), so shading it
    # would just tint the entire plot with nothing to contrast against.
    ax.axhspan(-PRACTICAL_THRESHOLD_V, PRACTICAL_THRESHOLD_V, color="0.85", zorder=0,
               label=f"Not practically meaningful (|diff| < {PRACTICAL_THRESHOLD_V}V)")
    ax.axhline(0, color="black", linewidth=0.8, zorder=1)

    ax.plot(result["freq_mhz"], result["mean_diff_v"], color="0.4", linewidth=1, zorder=2)
    ax.scatter(result["freq_mhz"], result["mean_diff_v"], c=colors, s=22, zorder=3,
               edgecolors="none")

    # legend entries for the point colors (the line/scatter above doesn't
    # auto-generate these since color varies per point)
    from matplotlib.lines import Line2D
    category_handles = [
        Line2D([0], [0], marker="o", color="none", markerfacecolor="tab:red", markersize=8,
               label="Syringe difference matters (significant & meaningful)"),
        Line2D([0], [0], marker="o", color="none", markerfacecolor="tab:green", markersize=8,
               label="Syringe difference doesn't matter (not significant, or too small)"),
    ]
    handles, labels = ax.get_legend_handles_labels()
    ax.legend(handles=handles + category_handles, loc="upper right", fontsize=9)

    # x-axis covers exactly the analyzed range (0.94-1.06MHz, 13 frequencies
    # at 10kHz steps) -- one explicit tick per frequency so every point is
    # individually readable.
    ax.set_xlim(FREQ_RANGE_MHZ[0], FREQ_RANGE_MHZ[1])
    ax.set_xticks(result["freq_mhz"])
    ax.set_xticklabels([f"{f:.2f}" for f in result["freq_mhz"]], rotation=45)

    ax.set_xlabel("Frequency (MHz)")
    ax.set_ylabel("Mean voltage difference, BD - Nipro (V)")
    ax.set_title(f"Syringe Voltage Offset Within the Transducer's Resonance Band "
                 f"({FREQ_RANGE_MHZ[0]}-{FREQ_RANGE_MHZ[1]}MHz)")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "syringe_significance_spectrum.png"), dpi=150)

    # ---- Combined Excel workbook: raw paired data + summary, one file ----
    # Sheet 1 is the per-(frequency, stroke) BD/nipro voltages that every
    # number in this script and the plot above was computed from -- full
    # traceability back to source. Sheet 2 is the per-frequency summary
    # (same content as the CSV) for a quick-reference view.
    raw_df = pd.DataFrame(raw_rows)
    excel_path = os.path.join(OUT_DIR, "syringe_significance_data.xlsx")
    with pd.ExcelWriter(excel_path, engine="openpyxl") as writer:
        raw_df.to_excel(writer, sheet_name="raw_paired_data", index=False)
        result.to_excel(writer, sheet_name="frequency_summary", index=False)

    print("\nSaved:")
    print("  syringe_pvalue_by_frequency.csv")
    print("  syringe_significance_spectrum.png")
    print("  syringe_significance_data.xlsx (raw_paired_data + frequency_summary sheets)")


if __name__ == "__main__":
    main()

"""
Goal: use the ambient benchmark (RX capturing with TX not driving fluid --
i.e. pure electrical/ADC noise floor, no acoustic signal at all) to answer two
things properly instead of by guesswork:

  1. What practical-significance threshold should the syringe t-test use?
     Previously this was an arbitrary 0.05V guess. The right answer is: a
     difference smaller than the sensor's own noise floor cannot be trusted
     as real regardless of what the p-value says. This script measures that
     noise floor directly, per frequency, from the ambient data.

  2. Where does the transducer actually have usable working range?
     "Working range" = frequencies where fluid-driven signal (stroke-to-stroke
     swing in the nipro baseline data) is large compared to the ambient noise
     floor. Outside that range, most of what you're measuring is more noise
     than signal, which is exactly why the full-spectrum syringe test showed
     so much "significant but trivial" scatter above ~1.3MHz -- the syringe
     test doesn't know the difference between "small but real" and "sensor
     floor drift dressed up as small oscillations".

Data used:
  - Ambient: nipro/26 gain/ambient data, when rx isnt receiving from tx/
             uas_averaged_summary_*.csv -- TX not driving into fluid, so
             mean_uas_volts here is pure baseline (op-amp/ADC offset, which
             drifts smoothly with frequency) and std_uas_volts is the
             repeat-to-repeat electrical noise at that frequency.
  - Signal:  BD and nipro stroke folders (0-40) -- for each frequency, the
             max-min spread of mean_uas_volts across all strokes is the total
             fluid-driven dynamic range the sensor can produce at that freq.

SNR definition used here:
  SNR(freq) = (max_stroke_voltage - min_stroke_voltage) / ambient_std(freq)
  i.e. how many "noise units" wide the actual fluid signal swing is. A
  frequency where fluid strokes barely move the needle more than ambient
  noise does is not a frequency worth trusting for anything -- syringe
  comparison included.

Output:
  - transducer_snr_by_frequency.csv
  - transducer_working_range.png (3-panel: ambient baseline+noise, signal
    dynamic range, and SNR with a recommended-band highlight)
  - Prints a recommended frequency sub-range and a noise-floor-derived
    practical-significance threshold to feed back into
    syringe_significance_full_spectrum.py
"""
import glob
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

AMBIENT_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\ambient data, when rx isnt receiving from tx"
)
BD_DIR = r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas\BD syringe\26 x gain"
NIPRO_DIR = (
    r"C:\Users\LOQ\Documents\Github repo\embo\testing\UAS Related stuff\UAS datas"
    r"\nipro syringe data and analysis\26 gain\test without taking out the saline and jelly"
)
OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# NOTE on what actually limits this transducer: the ambient electrical noise
# floor (median ~0.35mV, see print output) turned out to be negligible
# everywhere -- SNR against pure electrical noise is 10x-60,000x across the
# whole sweep, so a noise-floor-based "usable" cutoff never excludes anything
# and isn't the real constraint. The actual limiting factor is the
# transducer's own acoustic resonance: fluid-driven signal strength (see the
# dynamic-range panel) collapses ~10-30x outside roughly 0.90-1.06MHz. So
# "usable working range" here is defined the standard way resonance bandwidth
# is defined in electronics/acoustics: frequencies where the response is at
# least half of the peak response (the -3dB / half-max / FWHM convention),
# not against the (irrelevant) electrical noise floor.
RESONANCE_FRACTION_THRESHOLD = 0.5


def leading_number(folder_name: str):
    digits = ""
    for ch in folder_name.strip():
        if ch.isdigit():
            digits += ch
        else:
            break
    return int(digits) if digits else None


def load_all_strokes(base_dir: str, exclude_substrings=()) -> dict[int, pd.DataFrame]:
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
        csvs = glob.glob(os.path.join(full_path, "uas_averaged_summary_*.csv"))
        if not csvs:
            continue
        if stroke in result:
            continue
        result[stroke] = pd.read_csv(csvs[0])
    return result


def main():
    ambient_csvs = glob.glob(os.path.join(AMBIENT_DIR, "uas_averaged_summary_*.csv"))
    if not ambient_csvs:
        raise FileNotFoundError(f"No ambient summary CSV found in {AMBIENT_DIR}")
    ambient = pd.read_csv(ambient_csvs[0]).set_index("freq_hz")

    # Ambient rows captured with n_samples=1 have std_uas_volts=0 by
    # construction (can't compute a spread from one sample) -- that's not
    # "zero noise", it's "we didn't measure the noise here". Treat those as
    # missing and fill from the nearest frequency that actually has a
    # multi-sample noise estimate, so the noise floor curve isn't artificially
    # zero at those points.
    ambient.loc[ambient["n_samples"] <= 1, "std_uas_volts"] = None
    ambient["std_uas_volts"] = ambient["std_uas_volts"].interpolate(limit_direction="both")
    # Guard against interpolated/measured noise being exactly 0 (division by
    # zero in the SNR calc below) -- floor it at the smallest nonzero noise
    # value actually observed, since true zero electrical noise isn't real.
    min_nonzero_noise = ambient.loc[ambient["std_uas_volts"] > 0, "std_uas_volts"].min()
    ambient["std_uas_volts"] = ambient["std_uas_volts"].clip(lower=min_nonzero_noise)

    bd_strokes = load_all_strokes(BD_DIR, exclude_substrings=["jelly and saline"])
    nipro_strokes = load_all_strokes(NIPRO_DIR, exclude_substrings=["cross check with oscilloscope"])

    freqs = sorted(ambient.index)
    rows = []
    for freq in freqs:
        bd_vals = [df.loc[df["freq_hz"] == freq, "mean_uas_volts"].values[0]
                   for df in bd_strokes.values() if (df["freq_hz"] == freq).any()]
        nipro_vals = [df.loc[df["freq_hz"] == freq, "mean_uas_volts"].values[0]
                      for df in nipro_strokes.values() if (df["freq_hz"] == freq).any()]
        all_vals = bd_vals + nipro_vals
        if not all_vals:
            continue
        dynamic_range = max(all_vals) - min(all_vals)
        noise = ambient.loc[freq, "std_uas_volts"]
        snr = dynamic_range / noise
        rows.append({
            "freq_mhz": freq / 1e6,
            "ambient_baseline_v": ambient.loc[freq, "mean_uas_volts"],
            "ambient_noise_v": noise,
            "signal_dynamic_range_v": dynamic_range,
            "snr_vs_electrical_noise": snr,
        })

    result = pd.DataFrame(rows)
    peak_response = result["signal_dynamic_range_v"].max()
    resonance_cutoff = RESONANCE_FRACTION_THRESHOLD * peak_response
    result["usable"] = result["signal_dynamic_range_v"] >= resonance_cutoff
    result.to_csv(os.path.join(OUT_DIR, "transducer_snr_by_frequency.csv"), index=False)

    usable = result[result.usable]
    print(f"Peak fluid-driven response: {peak_response:.3f}V")
    print(
        f"{len(usable)} / {len(result)} frequencies are within "
        f"{RESONANCE_FRACTION_THRESHOLD*100:.0f}% of peak response "
        f"(>= {resonance_cutoff:.3f}V dynamic range)"
    )
    if len(usable):
        # contiguous usable band(s) -- report the widest one as "the" working range
        freqs_usable = usable["freq_mhz"].tolist()
        bands, band_start = [], freqs_usable[0]
        prev = freqs_usable[0]
        for f in freqs_usable[1:]:
            if round(f - prev, 3) > 0.011:  # gap larger than one step (0.01MHz)
                bands.append((band_start, prev))
                band_start = f
            prev = f
        bands.append((band_start, prev))
        widest = max(bands, key=lambda b: b[1] - b[0])
        print(f"Usable (resonance) bands: {bands}")
        print(f"=> Recommended working range (widest resonance band): {widest[0]:.2f}-{widest[1]:.2f} MHz")

    # Practical-significance threshold, now grounded in real data instead of a
    # guess: use the typical (median) signal strength OUTSIDE the resonance
    # band as the floor. A syringe difference smaller than "how much the fluid
    # itself moves the reading, off-resonance" isn't distinguishable from
    # normal off-resonance system wobble -- so it isn't practically meaningful.
    off_resonance_typical_swing = result.loc[~result.usable, "signal_dynamic_range_v"].median()
    print(f"\nMedian ambient (electrical) noise floor: {ambient['std_uas_volts'].median():.5f} V (negligible everywhere)")
    print(
        f"Data-derived practical-significance threshold (median off-resonance "
        f"fluid signal swing): {off_resonance_typical_swing:.4f} V"
        f"\n(compare to the previous arbitrary guess of 0.05V used in "
        f"syringe_significance_full_spectrum.py)"
    )

    # ---- Plot ----
    fig, axes = plt.subplots(3, 1, figsize=(13, 12), sharex=True)

    axes[0].plot(result["freq_mhz"], result["ambient_baseline_v"], color="tab:gray", label="ambient baseline")
    axes[0].fill_between(result["freq_mhz"],
                          result["ambient_baseline_v"] - result["ambient_noise_v"],
                          result["ambient_baseline_v"] + result["ambient_noise_v"],
                          color="tab:gray", alpha=0.3, label="+/- noise floor")
    axes[0].set_ylabel("Ambient voltage (V)")
    axes[0].set_title("Ambient Baseline (TX not driving fluid) -- pure electrical offset + noise")
    axes[0].legend()
    axes[0].grid(True, alpha=0.3)

    axes[1].plot(result["freq_mhz"], result["signal_dynamic_range_v"], color="tab:blue")
    axes[1].set_ylabel("Stroke-driven dynamic range (V)")
    axes[1].set_title("Fluid Signal Strength (max-min across all strokes, both syringes)")
    axes[1].grid(True, alpha=0.3)

    colors = ["tab:green" if u else "tab:red" for u in result["usable"]]
    axes[2].scatter(result["freq_mhz"], result["signal_dynamic_range_v"], c=colors, s=15)
    axes[2].axhline(resonance_cutoff, color="gray", linestyle="--",
                     label=f"{RESONANCE_FRACTION_THRESHOLD*100:.0f}% of peak ({resonance_cutoff:.3f}V)")
    axes[2].set_xlabel("Frequency (MHz)")
    axes[2].set_ylabel("Stroke-driven dynamic range (V)")
    axes[2].set_title("Resonance Working Range (green = within 50% of peak response, red = off-resonance)")
    axes[2].legend()
    axes[2].grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "transducer_working_range.png"), dpi=150)

    print("\nSaved:")
    print("  transducer_snr_by_frequency.csv")
    print("  transducer_working_range.png")


if __name__ == "__main__":
    main()

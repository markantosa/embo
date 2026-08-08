"""
One simple graph for the report/group: illustrates how the transducer's
resonance band (0.94-1.06MHz) was determined, in a single clear picture.

Method shown on the graph (matches transducer_working_range_benchmark.py):
  1. For every swept frequency, compute the fluid-driven "dynamic range" =
     max-min of stroke-averaged voltage across all tested strokes (0-40),
     both syringes -- this is how much the fluid itself can move the reading
     at that frequency, benchmarked against the ambient (RX-not-receiving)
     noise floor to confirm it's real signal, not noise.
  2. Find the peak of that curve: 1.7183V at 1.01MHz.
  3. Half of the peak = 0.8592V (the standard half-max/-3dB bandwidth
     convention for defining a resonance's usable range).
  4. The band of frequencies at or ABOVE that half-peak line is the
     transducer's resonance band: 0.94-1.06MHz.
"""
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
CSV_PATH = os.path.join(OUT_DIR, "transducer_snr_by_frequency.csv")

PEAK_FREQ, PEAK_V = 1.01, 1.7183
HALF_PEAK_V = PEAK_V / 2  # 0.8592V
BAND = (0.94, 1.06)


def main():
    df = pd.read_csv(CSV_PATH)

    fig, ax = plt.subplots(figsize=(11, 6.5))

    ax.plot(df["freq_mhz"], df["signal_dynamic_range_v"], color="tab:blue", linewidth=1.8,
            label="Fluid signal strength (max-min voltage across strokes 0-40)")

    ax.axvspan(*BAND, color="gold", alpha=0.25, label=f"Resonance band: {BAND[0]}-{BAND[1]} MHz")

    ax.axhline(HALF_PEAK_V, color="gray", linestyle="--", linewidth=1.2,
               label=f"Half-peak threshold = {HALF_PEAK_V:.4f} V")

    ax.plot(PEAK_FREQ, PEAK_V, "o", color="tab:red", markersize=9, zorder=5)
    ax.annotate(f"Peak: {PEAK_V:.4f} V @ {PEAK_FREQ} MHz",
                xy=(PEAK_FREQ, PEAK_V), xytext=(PEAK_FREQ + 0.15, PEAK_V - 0.05),
                fontsize=10, arrowprops=dict(arrowstyle="->", color="black"))

    ax.set_xlabel("Frequency (MHz)")
    ax.set_ylabel("Stroke-driven dynamic range (V)")
    ax.set_title("How the Transducer's Resonance Band Was Determined")
    ax.legend(loc="upper right")
    ax.grid(True, alpha=0.3)

    fig.tight_layout()
    out_path = os.path.join(OUT_DIR, "resonance_band_concept.png")
    fig.savefig(out_path, dpi=150)
    print(f"Saved: {out_path}")


if __name__ == "__main__":
    main()

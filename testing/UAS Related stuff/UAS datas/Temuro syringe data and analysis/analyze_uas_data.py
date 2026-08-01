"""
Walks the "temuro syringe" folder, now structured as:

    temuro syringe/<frequency>/<stroke>/<drive_voltage>.csv

e.g. "1 MHz/10th stroke/2.20 V.csv" or "0.95 MHz/10th stroke/2.25 v.csv".
The original full 18-point drive-voltage sweep (strokes 0-30, some empty)
lives under "1 MHz/". The new frequency-sweep test (0.90-0.99MHz) only has
"10th stroke" with 3 drive voltages (2.20V, 2.25V, 2.30V) so far.

The actual measured quantity -- received signal strength -- is extracted
from each waveform via FFT: rather than raw max-min (which includes
ambient/broadband noise riding on the signal), we take the FFT of each
capture and read only the magnitude at THAT folder's known drive
frequency, discarding all other frequency content as noise.

A few stroke folders under 1 MHz/ (8th, 10th) have raw scope-auto-named
files (scope_XXX.csv) instead of voltage-labeled names. These still follow
the same 18-step sweep, just unlabeled, so their drive voltage is inferred
by sequence order (sorted by scope file number) matching the same
2.20 -> 3.05V, 0.05V-step sweep used everywhere else.

Output (all in test_results/):
  uas_response_raw.csv        (freq_mhz, stroke, drive_v, received_vpp) -- every point
  uas_response_curves.png     (1MHz data only: received VPP vs drive V, one line per stroke)
  uas_trend_at_ref.png        (1MHz data only: received VPP at a reference drive V, vs stroke)
  uas_freq_sweep.png          (frequency-sweep data: received VPP vs frequency, one line per drive V)
"""
import re
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

DATA_DIR = Path(__file__).parent / "temuro syringe"
OUT_DIR = Path(__file__).parent / "test_results"
OUT_DIR.mkdir(exist_ok=True)

FREQ_RE = re.compile(r"([\d.]+)\s*MHz", re.IGNORECASE)
DRIVE_V_RE = re.compile(r"(\d+\.\d+)")
STROKE_RE = re.compile(r"^(\d+)")
SCOPE_SEQ_RE = re.compile(r"scope_(\d+)", re.IGNORECASE)

# Reference drive voltage to compare strokes against a single point (point 1 trend)
REF_DRIVE_V = 2.60


def freq_mhz_from_folder(folder_name: str) -> float:
    m = FREQ_RE.search(folder_name)
    if not m:
        raise ValueError(f"Can't parse frequency from folder: {folder_name!r}")
    return float(m.group(1))


def stroke_number(folder_name: str) -> int:
    if folder_name.strip().lower() == "no stroke":
        return 0
    m = STROKE_RE.match(folder_name.strip())
    if not m:
        raise ValueError(f"Can't parse stroke number from folder: {folder_name!r}")
    return int(m.group(1))


def received_vpp(csv_file: Path, target_freq_hz: float) -> float:
    """
    Raw scope export: 2 header rows, then 'second,Volt' data rows.

    Returns the peak-to-peak amplitude of ONLY the target_freq_hz component
    of the waveform (via FFT), rather than raw max-min of the full signal --
    this discards ambient/broadband noise that isn't at the drive frequency.
    """
    wf = pd.read_csv(csv_file, skiprows=2, names=["second", "volt"])
    n = len(wf)
    dt = wf["second"].iloc[1] - wf["second"].iloc[0]

    spectrum = np.fft.rfft(wf["volt"].to_numpy())
    freqs = np.fft.rfftfreq(n, d=dt)

    bin_idx = int(np.argmin(np.abs(freqs - target_freq_hz)))
    amplitude = 2 * np.abs(spectrum[bin_idx]) / n  # sine amplitude at that bin
    return float(2 * amplitude)  # peak-to-peak, for comparison with old metric


# --- Pass 1: figure out each stroke's actual drive-voltage sweep set from
# whichever files DO have a labeled voltage, anywhere in the dataset. This
# lets each stroke have its own sweep points (e.g. stroke 20 used 5 points:
# 2.20/2.30/2.40/2.50/2.60V, while strokes 0-10 used 18 points, 2.20-3.05V).
FALLBACK_SWEEP_V = np.linspace(2.20, 3.05, 18)  # old default, strokes 0-10

stroke_voltages: dict[int, set] = {}
for freq_folder in sorted(DATA_DIR.iterdir()):
    if not freq_folder.is_dir():
        continue
    for stroke_folder in sorted(freq_folder.iterdir()):
        if not stroke_folder.is_dir():
            continue
        stroke = stroke_number(stroke_folder.name)
        for f in stroke_folder.glob("*.csv"):
            m = DRIVE_V_RE.search(f.stem)
            if m:
                stroke_voltages.setdefault(stroke, set()).add(round(float(m.group(1)), 2))

# --- Pass 2: extract received VPP for every file, inferring drive voltage
# for unnamed (scope_XXX) files from that stroke's known sweep set.
rows = []
for freq_folder in sorted(DATA_DIR.iterdir()):
    if not freq_folder.is_dir():
        continue
    freq_mhz = freq_mhz_from_folder(freq_folder.name)
    target_freq_hz = freq_mhz * 1e6

    for stroke_folder in sorted(freq_folder.iterdir()):
        if not stroke_folder.is_dir():
            continue
        stroke = stroke_number(stroke_folder.name)
        csv_files = list(stroke_folder.glob("*.csv"))
        if not csv_files:
            print(f"Skipping empty folder: {freq_folder.name}/{stroke_folder.name}")
            continue

        labeled = [(f, DRIVE_V_RE.search(f.stem)) for f in csv_files]
        named = [(f, float(m.group(1))) for f, m in labeled if m]
        unnamed = [f for f, m in labeled if not m]

        if unnamed:
            unnamed_sorted = sorted(
                unnamed, key=lambda f: int(SCOPE_SEQ_RE.search(f.name).group(1))
            )
            known_v = sorted(stroke_voltages.get(stroke, set()))
            sweep_v = known_v if known_v else list(FALLBACK_SWEEP_V)

            if len(unnamed_sorted) != len(sweep_v):
                print(
                    f"WARNING: {freq_folder.name}/{stroke_folder.name} has "
                    f"{len(unnamed_sorted)} unlabeled files but expected "
                    f"{len(sweep_v)} sweep points ({sweep_v}) -- approximating "
                    f"via linspace across the known range. Verify manually."
                )
                inferred_v = np.linspace(min(sweep_v), max(sweep_v), len(unnamed_sorted))
            else:
                inferred_v = sweep_v

            named += list(zip(unnamed_sorted, inferred_v))

        for csv_file, drive_v in named:
            rows.append({
                "freq_mhz": freq_mhz,
                "stroke": stroke,
                "drive_v": round(float(drive_v), 2),
                "received_vpp": received_vpp(csv_file, target_freq_hz),
            })

raw = pd.DataFrame(rows).sort_values(["freq_mhz", "stroke", "drive_v"])
raw.to_csv(OUT_DIR / "uas_response_raw.csv", index=False)

# ============================================================
# 1MHz-only analysis (original full stroke/drive-voltage dataset)
# ============================================================
main = raw[raw["freq_mhz"] == 1.0]
print(main.groupby("stroke")["received_vpp"].agg(["mean", "std", "count"]).to_string())

fig1, ax1 = plt.subplots(figsize=(11, 6))
strokes = sorted(main["stroke"].unique())
colors = plt.cm.viridis(np.linspace(0, 1, len(strokes)))
for stroke, color in zip(strokes, colors):
    sub = main[main["stroke"] == stroke].sort_values("drive_v")
    ax1.plot(sub["drive_v"], sub["received_vpp"], marker="o", markersize=3,
              color=color, label=f"stroke {stroke}")
ax1.set_xlabel("Drive voltage (V)")
ax1.set_ylabel("Received VPP at 1MHz (V, FFT-filtered)")
ax1.set_title("UAS Response Curve per Stroke (FFT-filtered @ 1MHz)")
ax1.grid(True)
ax1.legend(fontsize=7, ncol=2)
fig1.tight_layout()
fig1.savefig(OUT_DIR / "uas_response_curves.png")

ref = main[np.isclose(main["drive_v"], REF_DRIVE_V)]
trend = ref.groupby("stroke")["received_vpp"].agg(mean="mean", std="std").reset_index()
trend.to_csv(OUT_DIR / "uas_trend_at_ref.csv", index=False)
print(f"\nReceived VPP at drive_v={REF_DRIVE_V}V, per stroke (1MHz only):")
print(trend.to_string(index=False))

fig2, ax2 = plt.subplots(figsize=(10, 5))
ax2.errorbar(trend["stroke"], trend["mean"], yerr=trend["std"],
             fmt="-o", color="tab:red", capsize=3)
ax2.set_xlabel("Stroke count")
ax2.set_ylabel(f"Received VPP at 1MHz, drive={REF_DRIVE_V}V (FFT-filtered)")
ax2.set_title("UAS Signal Trend vs Stroke Count (FFT-filtered @ 1MHz, fixed drive voltage)")
ax2.grid(True)
fig2.tight_layout()
fig2.savefig(OUT_DIR / "uas_trend_at_ref.png")

# ============================================================
# Frequency-sweep analysis: only strokes with a genuine multi-frequency
# sweep (>1 distinct frequency tested), averaged across drive voltages
# for a clean one-line-per-stroke comparison.
# ============================================================
freq_counts = raw.groupby("stroke")["freq_mhz"].nunique()
swept_strokes = freq_counts[freq_counts >= 3].index  # excludes stray one-off points
sweep = raw[raw["stroke"].isin(swept_strokes)]

if not sweep.empty:
    sweep_avg = (
        sweep.groupby(["stroke", "freq_mhz"])["received_vpp"]
        .agg(mean="mean", std="std", n="count")
        .reset_index()
        .sort_values(["stroke", "freq_mhz"])
    )
    sweep_avg.to_csv(OUT_DIR / "uas_freq_sweep_raw.csv", index=False)
    print("\nFrequency sweep data (averaged across drive voltages):")
    print(sweep_avg.to_string(index=False))

    fig3, ax3 = plt.subplots(figsize=(10, 5))
    for stroke in sorted(swept_strokes):
        sub = sweep_avg[sweep_avg["stroke"] == stroke]
        ax3.errorbar(sub["freq_mhz"], sub["mean"], yerr=sub["std"],
                     marker="o", capsize=3, label=f"stroke {stroke}")
    ax3.set_xlabel("Frequency (MHz)")
    ax3.set_ylabel("Received VPP (V, FFT-filtered, avg over drive voltages)")
    ax3.set_title("UAS Frequency Sweep Response by Stroke")
    ax3.grid(True)
    ax3.legend(fontsize=9)
    fig3.tight_layout()
    fig3.savefig(OUT_DIR / "uas_freq_sweep.png")
else:
    print("\nNo multi-frequency sweep data found yet.")

plt.show()

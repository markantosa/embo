"""Training-data collection for the eventual segmentation model
(SOFTWARE_TODO.md Layer 3 task 7 — YOLOv8-seg vs Cellpose evaluation).

Unblocked 2026-08-08: the open-top syringe mount was confirmed to cap how
far dark-field lighting alone can improve contrast, so "wait for Layer 2 to
be fully settled before collecting data" was replaced with "current optics
+ software enhancement (preprocessing.enhance_for_display) is the accepted
baseline, start collecting against it." A classical (non-ML) detection
attempt was tried first and rejected — see the conversation this was built
from — confirming a trained model is actually needed here, not just a
tuning problem.

Usage:
    python3 collect_training_data.py --stroke 10 --samples 3 --note "dark-field v2, polarizers crossed"

Each sample saves:
  training_data/stroke_<N>/<timestamp>_raw.npy       — median-stacked, PRE-enhancement (lossless)
  training_data/stroke_<N>/<timestamp>_enhanced.jpg   — what an annotation tool opens
and appends one row to training_data/metadata.csv.

Raw is kept alongside the enhanced image (not just the enhanced image)
specifically so the enhancement recipe (config.CLARITY_*/FLATFIELD_*, still
called "subjective/visual tuning choices, not derived from a measurement"
in config.py) can be revised later and re-applied to already-collected data
instead of needing every sample recaptured from scratch.
"""
import argparse
import csv
import os
import sys
import time
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent / "cv-pipeline"))

import numpy as np
from PIL import Image

import config
from capture import Camera
from preprocessing import enhance_for_display

METADATA_COLUMNS = [
    "timestamp", "stroke_count", "sample_index", "raw_path", "enhanced_path",
    "exposure_us", "analogue_gain", "um_per_pixel", "note",
]


def _append_metadata_row(row: dict) -> None:
    csv_path = os.path.join(config.TRAINING_DATA_DIR, "metadata.csv")
    file_exists = os.path.exists(csv_path)
    with open(csv_path, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=METADATA_COLUMNS)
        if not file_exists:
            writer.writeheader()
        writer.writerow(row)


def collect(stroke_count: int, samples: int, note: str, tag: str = None, start_index: int = 1) -> None:
    stroke_dir = os.path.join(config.TRAINING_DATA_DIR, f"stroke_{stroke_count}")
    os.makedirs(stroke_dir, exist_ok=True)

    camera = Camera()
    try:
        for i in range(samples):
            raw = camera.read_frame_stack()
            if raw is None:
                print(f"WARNING: sample {i} capture failed, skipping", file=sys.stderr)
                continue
            enhanced = enhance_for_display(raw)

            if tag:
                # Explicit naming, e.g. --tag "1strok" --samples 1 with
                # --start-index N -> "1strok_N". One file per invocation
                # is the expected use here (interactive capture-on-request),
                # but start_index+i still increments sanely if --samples>1.
                name = f"{tag}_{start_index + i}"
            else:
                name = time.strftime("%Y%m%dT%H%M%S") + f"_{int(time.time()*1000)%1000:03d}"
            raw_path = os.path.join(stroke_dir, f"{name}_raw.npy")
            enhanced_path = os.path.join(stroke_dir, f"{name}_enhanced.jpg")

            np.save(raw_path, raw)
            Image.fromarray(enhanced).save(enhanced_path)

            _append_metadata_row({
                "timestamp": name,
                "stroke_count": stroke_count,
                "sample_index": start_index + i,
                "raw_path": raw_path,
                "enhanced_path": enhanced_path,
                "exposure_us": getattr(camera, "last_exposure_us", ""),
                "analogue_gain": getattr(camera, "last_analogue_gain", ""),
                "um_per_pixel": config.UM_PER_PIXEL,
                "note": note,
            })
            print(f"sample {i+1}/{samples}: saved {enhanced_path}")
    finally:
        camera.release()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stroke", type=int, required=True, help="stroke count label for this batch")
    parser.add_argument("--samples", type=int, default=1, help="number of captures to take (default 1)")
    parser.add_argument("--note", type=str, default="", help="free-form note (lighting setup, material batch, etc.)")
    parser.add_argument("--tag", type=str, default=None,
                         help='explicit filename prefix, e.g. "1strok" -> "1strok_<n>_enhanced.jpg" '
                              '(default: timestamp-based naming)')
    parser.add_argument("--start-index", type=int, default=1, help="starting number for --tag naming (default 1)")
    args = parser.parse_args()

    collect(args.stroke, args.samples, args.note, tag=args.tag, start_index=args.start_index)


if __name__ == "__main__":
    main()

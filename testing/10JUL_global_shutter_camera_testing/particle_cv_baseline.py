"""
particle_cv_baseline.py

Baseline (classical, non-AI) computer-vision pipeline for sizing irregular,
semi-transparent gel particles (Gelfoam / Lyostypt / EG Gel) from microscope
images captured via RPi camera.

Pipeline: grayscale -> blur -> adaptive/Otsu threshold -> morphological
cleanup -> distance transform + watershed (splits touching particles) ->
per-particle contour metrics -> bubble-candidate flagging -> CSV + overlay.

Metrics reported per ISO 13322-1/13322-2 convention for irregular particles:
  - ECD (area-equivalent circular diameter)
  - Feret max / Feret min (via minAreaRect as a fast proxy)
  - Circularity = 4*pi*Area / Perimeter^2   (1.0 = perfect circle)
  - Solidity = Area / ConvexHullArea

Usage:
    python particle_cv_baseline.py --image sample.jpg --um_per_px 0.85
"""

import argparse
import cv2
import numpy as np
import pandas as pd
from pathlib import Path

# ---- tunable knobs (start here when results look wrong) --------------------
MIN_PARTICLE_AREA_PX = 40      # filter out noise specks
GAUSSIAN_BLUR_KSIZE = 5
MORPH_KERNEL_SIZE = 3
DIST_TRANSFORM_THRESH_RATIO = 0.4   # fraction of max distance -> sure foreground
BUBBLE_CIRCULARITY_THRESH = 0.90
BUBBLE_SOLIDITY_THRESH = 0.95
# -----------------------------------------------------------------------------


def segment_particles(gray):
    """Threshold + watershed split. Returns a labeled image (0 = background)."""
    blur = cv2.GaussianBlur(gray, (GAUSSIAN_BLUR_KSIZE, GAUSSIAN_BLUR_KSIZE), 0)

    # Otsu as a starting point; swap for adaptive threshold if illumination
    # is uneven across the field of view.
    _, thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)

    kernel = np.ones((MORPH_KERNEL_SIZE, MORPH_KERNEL_SIZE), np.uint8)
    opened = cv2.morphologyEx(thresh, cv2.MORPH_OPEN, kernel, iterations=2)

    sure_bg = cv2.dilate(opened, kernel, iterations=3)
    dist = cv2.distanceTransform(opened, cv2.DIST_L2, 5)
    _, sure_fg = cv2.threshold(dist, DIST_TRANSFORM_THRESH_RATIO * dist.max(), 255, 0)
    sure_fg = np.uint8(sure_fg)
    unknown = cv2.subtract(sure_bg, sure_fg)

    _, markers = cv2.connectedComponents(sure_fg)
    markers = markers + 1
    markers[unknown == 255] = 0

    color_for_watershed = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
    cv2.watershed(color_for_watershed, markers)
    return markers


def particle_metrics(markers, um_per_px):
    rows = []
    for label in np.unique(markers):
        if label <= 1:  # 0 = unknown/boundary, 1 = background
            continue
        mask = np.uint8(markers == label) * 255
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            continue
        c = max(contours, key=cv2.contourArea)
        area_px = cv2.contourArea(c)
        if area_px < MIN_PARTICLE_AREA_PX:
            continue

        perimeter = cv2.arcLength(c, True)
        circularity = 4 * np.pi * area_px / (perimeter ** 2) if perimeter > 0 else 0

        hull = cv2.convexHull(c)
        hull_area = cv2.contourArea(hull)
        solidity = area_px / hull_area if hull_area > 0 else 0

        rect = cv2.minAreaRect(c)
        (_, _), (w, h), _ = rect
        feret_max_px, feret_min_px = max(w, h), min(w, h)

        ecd_px = 2 * np.sqrt(area_px / np.pi)

        is_bubble_candidate = (
            circularity > BUBBLE_CIRCULARITY_THRESH and solidity > BUBBLE_SOLIDITY_THRESH
        )

        rows.append({
            "label": int(label),
            "ecd_um": round(ecd_px * um_per_px, 1),
            "feret_max_um": round(feret_max_px * um_per_px, 1),
            "feret_min_um": round(feret_min_px * um_per_px, 1),
            "circularity": round(circularity, 3),
            "solidity": round(solidity, 3),
            "bubble_candidate": is_bubble_candidate,
        })
    return pd.DataFrame(rows)


def draw_overlay(image_bgr, markers, df):
    overlay = image_bgr.copy()
    overlay[markers == -1] = [0, 0, 255]  # watershed boundaries in red
    for _, row in df.iterrows():
        mask = np.uint8(markers == row["label"]) * 255
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            continue
        c = max(contours, key=cv2.contourArea)
        color = (0, 165, 255) if row["bubble_candidate"] else (0, 255, 0)
        cv2.drawContours(overlay, [c], -1, color, 1)
    return overlay


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True, help="Path to input image")
    ap.add_argument("--um_per_px", type=float, required=True,
                     help="Calibration factor from your ruler/micrometer shot")
    ap.add_argument("--outdir", default="results")
    args = ap.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(exist_ok=True)

    image_bgr = cv2.imread(args.image)
    if image_bgr is None:
        raise FileNotFoundError(f"Could not read {args.image}")
    gray = cv2.cvtColor(image_bgr, cv2.COLOR_BGR2GRAY)

    markers = segment_particles(gray)
    df = particle_metrics(markers, args.um_per_px)

    stem = Path(args.image).stem
    csv_path = outdir / f"{stem}_particles.csv"
    df.to_csv(csv_path, index=False)

    overlay = draw_overlay(image_bgr, markers, df)
    overlay_path = outdir / f"{stem}_overlay.png"
    cv2.imwrite(str(overlay_path), overlay)

    real_particles = df[~df["bubble_candidate"]]
    print(f"Detected {len(df)} blobs total, {len(real_particles)} flagged as particles "
          f"({len(df) - len(real_particles)} flagged as bubble candidates)")
    if len(real_particles):
        print(f"Median ECD: {real_particles['ecd_um'].median():.1f} um | "
              f"IQR: {real_particles['ecd_um'].quantile(0.25):.1f}-"
              f"{real_particles['ecd_um'].quantile(0.75):.1f} um")
    print(f"Saved: {csv_path}, {overlay_path}")


if __name__ == "__main__":
    main()

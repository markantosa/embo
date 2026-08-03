"""
capture_and_analyze.py

Classical (non-AI) computer vision pipeline for particle size detection,
running directly on the Raspberry Pi 5 via picamera2.

Captures a set number of images, detects particles in each using adaptive
thresholding + ellipse fitting, converts pixel measurements to real-world
microns using a pre-determined calibration factor, and reports the combined
median + IQR particle size across all captures.

Validated on table salt as a calibration/sanity-check material before use
on real Gelfoam/embolic agent samples (see README.md for salt test results).

Usage (on the Pi):
    python3 capture_and_analyze.py
"""

import time
import cv2
import numpy as np
import matplotlib
matplotlib.use("Agg")  # no display attached to the Pi
import matplotlib.pyplot as plt
from picamera2 import Picamera2

# ============ UPDATE THESE ============
UM_PER_PIXEL = 14.34          # from ruler calibration -- must match capture resolution below
NUM_SHOTS = 3                  # how many photos to capture (respread material between each)
GLARE_CROP_FRAC = 0.78         # crops out right-side glare -- set to 1.0 to disable
LABEL = "Sample"
CAPTURE_WIDTH = 1920
CAPTURE_HEIGHT = 1080
# ========================================

picam2 = Picamera2()
config = picam2.create_still_configuration(main={"size": (CAPTURE_WIDTH, CAPTURE_HEIGHT)})
picam2.configure(config)
picam2.start()
time.sleep(2)  # let auto-exposure settle

all_major = []
for i in range(1, NUM_SHOTS + 1):
    input(f"\nPosition sample #{i}, then press Enter to capture...")

    frame = picam2.capture_array()  # RGB array, no CLI tool involved
    img_color = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
    img = cv2.cvtColor(img_color, cv2.COLOR_BGR2GRAY)

    raw_path = f"capture_{i:02d}.jpg"
    cv2.imwrite(raw_path, img_color)
    print(f"Saved raw capture: {raw_path}")

    h, w = img.shape
    crop = img[:, :int(w * GLARE_CROP_FRAC)]
    blur = cv2.GaussianBlur(crop, (5, 5), 0)

    # Adaptive threshold: compares each pixel to its LOCAL neighborhood brightness
    # instead of one global cutoff -- necessary because lighting isn't perfectly
    # even across the frame (a global/Otsu threshold merged particles into large
    # false blobs when first tried).
    thresh = cv2.adaptiveThreshold(
        blur, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C,
        cv2.THRESH_BINARY, 101, -15
    )
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    overlay = cv2.cvtColor(crop, cv2.COLOR_GRAY2BGR)
    n_this_image = 0
    for c in contours:
        area = cv2.contourArea(c)
        if area < 15 or area > 8000:
            # too small = noise/dust; too large = merged/touching particles
            continue

        if len(c) >= 5:
            # Ellipse fit needs at least 5 points; handles irregular (non-circular)
            # particle shapes better than a simple circle fit.
            (ex, ey), (minor, major), angle = cv2.fitEllipse(c)
            major_um = major * UM_PER_PIXEL
            cv2.ellipse(overlay, ((ex, ey), (minor, major), angle), (0, 255, 0), 2)
        else:
            # Fallback for very small contours that can't support an ellipse fit
            x, y, bw, bh = cv2.boundingRect(c)
            major_um = max(bw, bh) * UM_PER_PIXEL
            cv2.rectangle(overlay, (x, y), (x + bw, y + bh), (0, 165, 255), 2)

        all_major.append(major_um)
        n_this_image += 1

    overlay_path = f"capture_{i:02d}_overlay.jpg"
    cv2.imwrite(overlay_path, overlay)
    print(f"{n_this_image} particles detected -> {overlay_path}")

picam2.stop()

print(f"\n===== RESULTS =====")
print(f"Total particles detected across {NUM_SHOTS} images: {len(all_major)}")
if all_major:
    arr = np.array(all_major)
    median = np.median(arr)
    q1, q3 = np.percentile(arr, [25, 75])
    print(f"Median (major axis): {median:.1f} um | IQR: {q3-q1:.1f} um (Q1={q1:.1f}, Q3={q3:.1f})")
    print(f"Min: {arr.min():.1f} um | Max: {arr.max():.1f} um")

    plt.figure(figsize=(7, 5))
    plt.hist(arr, bins=30, color="#4A90D9", edgecolor="black")
    plt.axvline(median, color="red", linestyle="--", label=f"Median: {median:.0f}um")
    plt.xlabel("Particle size (um, major axis length)")
    plt.ylabel("Count")
    plt.title(f"{LABEL} - Particle Size Distribution (n={len(arr)})")
    plt.legend()
    plt.tight_layout()
    plt.savefig("live_capture_histogram.png", dpi=150)
    print("Histogram saved to live_capture_histogram.png")
else:
    print("No particles detected -- check GLARE_CROP_FRAC, threshold params, or image quality.")

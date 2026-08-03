# Classical CV Particle Segmentation — Salt Calibration Test

## Purpose
Before running this pipeline on real Gelfoam/embolic agent samples, it was
validated on **table salt** as a known, easy-to-segment reference material.
Salt grains are opaque, high-contrast, and roughly uniform — a good sanity
check to confirm the capture → threshold → detect → measure pipeline works
correctly and the calibration factor is sound, before trusting it on harder
(translucent, irregular) real material.

## Method
- **Hardware:** Raspberry Pi 5 + camera module, running `picamera2` directly
  (no CLI capture tool — sidesteps intermittent `rpicam-still` capture bugs).
- **Calibration:** pixel-to-micron conversion factor determined from a ruler
  photograph at the same capture resolution: **`UM_PER_PIXEL = 14.34`**.
- **Segmentation approach:** classical (non-AI) computer vision —
  1. Convert to grayscale, Gaussian blur
  2. **Adaptive thresholding** (not a single global/Otsu threshold) — compares
     each pixel to its local neighborhood brightness, which was necessary
     because lighting was not perfectly even across the frame. A global
     threshold initially merged particles into large false blobs; switching
     to adaptive thresholding fixed this.
  3. Contour detection on the resulting binary mask
  4. Size filtering (contours <15px² treated as noise/dust; >8000px² treated
     as merged/touching particles and excluded)
  5. **Ellipse fitting** per contour (not a circle fit) to account for
     irregular, non-circular particle shapes; major axis length is converted
     to microns using the calibration factor
  6. Aggregate all detected particles across multiple captures into a
     combined median + IQR

No trained model or AI is involved in this pipeline — every step is a fixed,
deterministic image-processing operation.

## Results (salt)
| Metric | Value |
|---|---|
| Total particles detected | 750 |
| Median size | 215 µm |
| IQR | *(see console output for exact Q1/Q3/IQR/min/max — not visible in the histogram screenshot alone)* |

This is a plausible, sensible size range for fine table salt, indicating the
capture pipeline, thresholding fix, and calibration factor are all working
correctly together — this result served as the go/no-go check before moving
to real Gelfoam material testing.

**Calibration check** — ruler tick spacing measured at ~69.5–70.0 px between
1 mm marks, consistent with the `UM_PER_PIXEL = 14.34` calibration factor
used in the script (1000 µm / ~69.75 px ≈ 14.34).

![Ruler calibration check](images/calibration_ruler.png)

![Detected salt particles with ellipse fit overlay](images/salt_overlay.png)

![Salt particle size distribution histogram](images/salt_histogram.png)

## Files
- `capture_and_analyze.py` — the script itself. Captures `NUM_SHOTS` images
  directly from the Pi camera (prompting between each so the sample can be
  repositioned/respread), detects and measures particles in each, and prints
  + plots the combined summary statistics.
- `images/calibration_ruler.png` — ruler shot used to confirm the pixel-to-
  micron calibration factor before trusting particle measurements.
- `images/salt_overlay.png` — detected particles (green ellipse fits) on a
  salt sample frame.
- `images/salt_histogram.png` — resulting particle size distribution
  (n=750, median 215 µm).

## Known limitations
- Adaptive threshold parameters (block size 101, C=-15) were tuned for this
  specific lighting setup and camera distance — they are not guaranteed to
  generalize to different lighting conditions without re-tuning.
- Ellipse-fit major axis is used as the reported "size" here, which is closer
  to a Feret-diameter-style measurement rather than the equivalent circular
  diameter (ECD) convention used elsewhere in this project's later
  microscope-based measurements — noted here for consistency awareness when
  comparing across datasets.
- This pipeline has not been validated against translucent/low-contrast
  material (e.g. real Gelfoam surrogate) with the same accuracy as shown here
  for salt; see project documentation for status on that testing track.

## How to run
```bash
# on the Raspberry Pi
sudo apt install -y python3-picamera2   # if not already installed
python3 capture_and_analyze.py
```
Follow the on-screen prompts to position and capture each sample. Results
print to the console and save as image/plot files in the working directory.

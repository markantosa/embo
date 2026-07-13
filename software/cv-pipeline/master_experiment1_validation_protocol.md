# Master Protocol: Experiment 1 — Complete CV Particle-Sizing Validation

This is the full, self-contained guide covering both tracks:

- **Track 1 (spheres):** proves your camera + pixel-to-micron math + circle-measuring code is correct, using a known, perfectly round reference object.
- **Track 2 (gelatin surrogate + manual measurement):** proves your code correctly measures real, irregular, blob-shaped particles — this is the actual experiment your SRR requires ("compare results with manually measured particle sizes").

Do Track 1 first (it's faster and catches basic bugs cheaply). Track 2 is the one that goes on your SRR slide as your Experiment 1 result.

---

## PART A — Full materials checklist

Gather everything before starting so you're not stopping mid-protocol.

**For Track 1 (spheres):**
- [ ] Cospheric polyethylene microspheres, 400–500 µm (or your ordered band)
- [ ] Dish soap or Tween 20/80 (only needed if dispersing in water — dry dispersal skips this)
- [ ] Clean glass slide, small petri dish, or your clear viewing window

**For Track 2 (gelatin surrogate):**
- [ ] Unflavored gelatin powder (e.g. a baking-aisle sachet) OR agar-agar powder
- [ ] Water (for the gel itself, and separately for the saline/dilution stand-in)
- [ ] A small container/mold to set the gel in (a shallow tray or bowl)
- [ ] A scalpel, sharp knife, or box cutter blade — precision matters more than a kitchen knife here
- [ ] A cutting surface (chopping board or similar)
- [ ] Your two-syringe + three-way stopcock rig, fully assembled
- [ ] A ruler or stage micrometer, if you haven't already established your µm-per-pixel calibration

**Shared, both tracks:**
- [ ] Camera + backlight rig, already focused at your working distance
- [ ] Laptop/RPi with Python, `opencv-python`, and `numpy` installed
- [ ] A fine tool (toothpick, pipette tip) for spreading particles
- [ ] Free image-analysis software: **ImageJ/Fiji** (download from imagej.net/software/fiji) — used for manual measurement in Part C. Install this now if you don't have it, since Part C depends on it.

---

## PART B — Track 1: Sphere calibration validation

### B1. Establish your pixel-to-micron calibration (skip if already done)

1. Photograph a ruler or stage micrometer at your exact working distance — the same distance you use for every other shot in this protocol from now on. Do not change distance/zoom between this step and anything later.
2. Open the image and measure a known distance (e.g. the span between the 0mm and 5mm marks) in pixels. You can do this in ImageJ: open the image, use the straight-line tool to draw along your known distance, then `Analyze > Measure` gives you the pixel length.
3. Compute: `um_per_pixel = known_distance_um / measured_pixels`
4. Write this number down. You'll need it in every script below.

### B2. Disperse the spheres

1. Take a small pinch of the dry microsphere powder — a few milligrams is thousands of particles, far more than needed.
2. **Dry dispersal (recommended for a first pass):** sprinkle directly onto your slide/window.
3. **Wet dispersal (if you need them suspended in fluid):** add one drop of dish soap to a small volume of water first, then sprinkle the spheres in and stir gently — they're hydrophobic and resist wetting without the soap.
4. Use the toothpick to spread the particles into a **single, thin layer with visible gaps between most particles**. A few touching pairs are fine; a solid mat is not — retry the spread if it looks clumped.

### B3. Confirm imaging setup before capturing

Check all of these live in your preview before taking the real shot:

1. Backlight on, particles read as clearly darker than the background (not just faintly gray).
2. Focus locked and sharp — zoom into the live preview and confirm crisp edges on individual spheres.
3. Same working distance as your Step B1 ruler shot.
4. No motion — let everything settle a couple of seconds before capturing.

### B4. Capture images

1. Take **at least 3 separate images**, re-spreading the particles between each shot.
2. Name them clearly, e.g. `sphere_test_01.jpg`, `_02.jpg`, `_03.jpg`.
3. Put all of them in a folder named `sphere_test_images`.
4. Quick sanity check: can you, by eye, tell where one particle ends and the next begins in each photo? If not, retake that shot with a thinner spread.

### B5. Run the sphere measurement script

Update the three marked values, then run this.

```python
import cv2
import numpy as np
import csv
import glob
import os

# ============ UPDATE THESE THREE VALUES ============
UM_PER_PIXEL = 1.62          # from Step B1
KNOWN_SIZE_UM = 450          # midpoint of your sphere's labeled range
IMAGE_FOLDER = "sphere_test_images"
# =====================================================

OUTPUT_CSV = "sphere_measurement_results.csv"
MIN_PARTICLE_PIXELS = 20

def measure_image(path):
    img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if img is None:
        print(f"WARNING: could not read {path}, skipping")
        return []

    blur = cv2.GaussianBlur(img, (5, 5), 0)
    _, thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    diameters_um = []
    overlay = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)

    for c in contours:
        area = cv2.contourArea(c)
        if area < MIN_PARTICLE_PIXELS:
            continue
        (x, y), radius = cv2.minEnclosingCircle(c)
        diameter_px = radius * 2
        diameter_um = diameter_px * UM_PER_PIXEL
        diameters_um.append(diameter_um)
        cv2.circle(overlay, (int(x), int(y)), int(radius), (0, 255, 0), 2)

    overlay_path = path.rsplit(".", 1)[0] + "_overlay.jpg"
    cv2.imwrite(overlay_path, overlay)
    print(f"{os.path.basename(path)}: found {len(diameters_um)} particles, overlay -> {overlay_path}")
    return diameters_um

def main():
    image_paths = glob.glob(os.path.join(IMAGE_FOLDER, "*.jpg")) + \
                  glob.glob(os.path.join(IMAGE_FOLDER, "*.png"))
    if not image_paths:
        print(f"No images found in {IMAGE_FOLDER}.")
        return

    all_diameters = []
    with open(OUTPUT_CSV, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["image", "particle_index", "diameter_um"])
        for path in image_paths:
            diameters = measure_image(path)
            for i, d in enumerate(diameters):
                writer.writerow([os.path.basename(path), i, round(d, 1)])
            all_diameters.extend(diameters)

    if not all_diameters:
        print("No particles detected. Check thresholding/focus.")
        return

    arr = np.array(all_diameters)
    median = np.median(arr)
    q1, q3 = np.percentile(arr, [25, 75])
    error = median - KNOWN_SIZE_UM

    print("\n===== TRACK 1 RESULTS =====")
    print(f"Total particles measured: {len(arr)}")
    print(f"Median diameter: {median:.1f} um")
    print(f"IQR: {q3 - q1:.1f} um (Q1={q1:.1f}, Q3={q3:.1f})")
    print(f"Known sphere size: {KNOWN_SIZE_UM} um")
    print(f"Error (median - known): {error:+.1f} um")
    print("PASS" if abs(error) <= 50 else "FAIL", "- within +/-50um criterion" if abs(error) <= 50 else "- outside +/-50um")

if __name__ == "__main__":
    main()
```

Run it:
```bash
pip install opencv-python numpy --break-system-packages
python measure_spheres.py
```

### B6. Check the output

1. Open every `..._overlay.jpg` file. Confirm: one green circle per visible sphere, no stray circles on dust/noise, no two spheres wrapped in one circle.
2. If the overlays look correct and the printed error is within ±50 µm → **Track 1 passed.** Keep the CSV and one overlay image as evidence.
3. If not, see the troubleshooting notes at the end of this document before moving to Track 2.

---

## PART C — Track 2: Gelatin surrogate + manual measurement (the real Experiment 1)

This is the step that actually satisfies your SRR wording — comparing CV output against a manual measurement, on material that behaves like real Gelfoam (irregular, torn, blob-shaped), not a sphere.

### C1. Make the gel

1. Follow the packet instructions for unflavored gelatin (typically: dissolve gelatin powder into hot water, roughly 1 tablespoon of powder per cup of water for a firm, cuttable gel — start here and adjust next time if it's too soft or too rubbery).
2. If using agar-agar instead: dissolve in boiling water per the packet ratio (usually a bit less powder is needed than gelatin for a comparable firmness), then let it cool.
3. Pour into a shallow tray so you end up with a slab roughly 1–1.5 mm thick (thin enough that your later cubes come out close to the target starting size).
4. Refrigerate until fully set and firm — at least 1–2 hours. It should hold its shape and not feel sticky/tacky when you touch it.

### C2. Cut into starting pieces

1. Turn the set slab out onto your cutting surface.
2. Using the scalpel/sharp blade, cut into small cubes approximately **1–2 mm on each side** — this matches how real Gelfoam prep starts.
3. Aim for a reasonable batch — a few dozen small cubes is enough for one test run.

### C3. Load into the syringe rig

1. Fill one syringe with a small amount of water (standing in for saline) and load your cut gel cubes into it.
2. Connect both syringes via the three-way stopcock as your rig is designed to do.
3. Make sure the stopcock is open in the correct position to allow flow between both syringes (not to the third port).

### C4. Pump to fragment the particles

1. Pump the mixture back and forth between the two syringes **30 times** — this matches the published gelatin-sponge pumping protocol and gives you a real, comparable data point.
2. Keep strokes reasonably consistent in speed and force — wildly uneven strokes make your results harder to interpret later.
3. Optional: if you want a stroke-count trend (this maps to your Experiment 2 as well), pause and extract a small sample at 10, 20, and 30 strokes instead of only doing 30. Label each sample clearly if you do this.

### C5. Extract a sample for imaging

1. Open the stopcock's third port (or your dedicated sampling port) and withdraw a small aliquot of the fragmented slurry.
2. Deposit a thin layer into your clear viewing window, a petri dish, or a glass slide — thin enough that particles aren't stacked on top of each other.
3. Use the toothpick to spread them out with visible gaps, same as you did for the spheres.

### C6. Confirm imaging setup and capture

Same checklist as Track 1:

1. Backlight on, particles clearly darker than background.
2. Focus locked and sharp.
3. Same working distance as your calibration shot (Step B1).
4. No motion, let it settle.
5. Capture **at least 3 images**, re-spreading between each. Save into a folder named `gel_test_images`, named `gel_test_01.jpg`, `_02.jpg`, `_03.jpg`.

### C7. Manually measure a sample of particles (this is the ground truth)

You need real human-measured values to compare your code against. Use ImageJ/Fiji:

1. Open Fiji. Open one of your `gel_test_*.jpg` images (`File > Open`).
2. Set the scale so measurements come out in real units instead of pixels:
   - `Analyze > Set Scale`
   - Enter the distance-in-pixels and known-distance-in-microns from your Step B1 calibration (or, better, draw a line across your ruler-calibration image first and let Fiji compute it directly).
   - Set the unit label to `um`.
3. Select the straight-line tool from the toolbar.
4. For **at least 20–30 particles** in the image (pick a spread across small, medium, and large-looking ones — don't cherry-pick only the clean round-looking ones, since irregular blobs are the point of this test):
   - Draw a line across what you judge to be the particle's longest visible dimension.
   - Press `M` (or `Analyze > Measure`) to log the length.
5. When done, `File > Save As > Results` to export a CSV of your manual measurements. Name it `gel_manual_measurements.csv`.

This manual step is tedious by design — it's supposed to be the slow, careful "ground truth" your automated code gets checked against.

### C8. Run the automated measurement script — with shape-aware sizing

This version uses an ellipse fit (major axis length) instead of `minEnclosingCircle`, since your particles are irregular, not round — a circle fit would systematically overestimate size on a blob shape.

```python
import cv2
import numpy as np
import csv
import glob
import os

# ============ UPDATE THIS VALUE ============
UM_PER_PIXEL = 1.62          # same value from Step B1
IMAGE_FOLDER = "gel_test_images"
# =============================================

OUTPUT_CSV = "gel_cv_measurement_results.csv"
MIN_PARTICLE_PIXELS = 20
MIN_POINTS_FOR_ELLIPSE = 5    # fitEllipse needs at least 5 contour points

def measure_image(path):
    img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
    if img is None:
        print(f"WARNING: could not read {path}, skipping")
        return []

    blur = cv2.GaussianBlur(img, (5, 5), 0)
    _, thresh = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)
    contours, _ = cv2.findContours(thresh, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    results = []
    overlay = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)

    for c in contours:
        area = cv2.contourArea(c)
        if area < MIN_PARTICLE_PIXELS:
            continue

        if len(c) >= MIN_POINTS_FOR_ELLIPSE:
            ellipse = cv2.fitEllipse(c)
            (cx, cy), (minor_axis, major_axis), angle = ellipse
            major_um = major_axis * UM_PER_PIXEL
            minor_um = minor_axis * UM_PER_PIXEL
            aspect_ratio = major_axis / minor_axis if minor_axis > 0 else float("nan")
            cv2.ellipse(overlay, ellipse, (0, 255, 0), 2)
        else:
            # too few points for an ellipse fit -- fall back to bounding box diagonal
            x, y, w, h = cv2.boundingRect(c)
            major_um = max(w, h) * UM_PER_PIXEL
            minor_um = min(w, h) * UM_PER_PIXEL
            aspect_ratio = major_um / minor_um if minor_um > 0 else float("nan")
            cv2.rectangle(overlay, (x, y), (x + w, y + h), (0, 165, 255), 2)

        results.append((major_um, minor_um, aspect_ratio))

    overlay_path = path.rsplit(".", 1)[0] + "_overlay.jpg"
    cv2.imwrite(overlay_path, overlay)
    print(f"{os.path.basename(path)}: found {len(results)} particles, overlay -> {overlay_path}")
    return results

def main():
    image_paths = glob.glob(os.path.join(IMAGE_FOLDER, "*.jpg")) + \
                  glob.glob(os.path.join(IMAGE_FOLDER, "*.png"))
    if not image_paths:
        print(f"No images found in {IMAGE_FOLDER}.")
        return

    all_major = []
    with open(OUTPUT_CSV, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["image", "particle_index", "major_axis_um", "minor_axis_um", "aspect_ratio"])
        for path in image_paths:
            measurements = measure_image(path)
            for i, (major, minor, ar) in enumerate(measurements):
                writer.writerow([os.path.basename(path), i, round(major, 1), round(minor, 1), round(ar, 2)])
                all_major.append(major)

    if not all_major:
        print("No particles detected. Check thresholding/focus.")
        return

    arr = np.array(all_major)
    median = np.median(arr)
    q1, q3 = np.percentile(arr, [25, 75])

    print("\n===== TRACK 2 CV RESULTS (major axis length) =====")
    print(f"Total particles measured: {len(arr)}")
    print(f"Median: {median:.1f} um")
    print(f"IQR: {q3 - q1:.1f} um (Q1={q1:.1f}, Q3={q3:.1f})")
    print("Now compare this median against your manual measurements from Step C7.")

if __name__ == "__main__":
    main()
```

Run it:
```bash
python measure_gel_particles.py
```

### C9. Compare manual vs. automated

```python
import csv
import numpy as np

MANUAL_CSV = "gel_manual_measurements.csv"   # exported from ImageJ/Fiji, Step C7
CV_CSV = "gel_cv_measurement_results.csv"    # from Step C8

def load_manual_lengths(path):
    lengths = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Fiji's exported column is usually called "Length"
            for key in row:
                if key.strip().lower() == "length":
                    lengths.append(float(row[key]))
    return lengths

def load_cv_lengths(path):
    lengths = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            lengths.append(float(row["major_axis_um"]))
    return lengths

manual = np.array(load_manual_lengths(MANUAL_CSV))
cv_auto = np.array(load_cv_lengths(CV_CSV))

manual_median = np.median(manual)
cv_median = np.median(cv_auto)
error = cv_median - manual_median

print("===== TRACK 2 FINAL COMPARISON =====")
print(f"Manual median (ground truth): {manual_median:.1f} um  (n={len(manual)})")
print(f"CV median (automated):        {cv_median:.1f} um  (n={len(cv_auto)})")
print(f"Error (CV - manual):          {error:+.1f} um")
print("PASS" if abs(error) <= 50 else "FAIL", "- within +/-50um success criterion" if abs(error) <= 50 else "- outside +/-50um")
```

Run it:
```bash
python compare_manual_vs_cv.py
```


## Troubleshooting

- **Median off by more than ±50 µm, consistently in one direction (either track):** check your `UM_PER_PIXEL` value first — an error there scales every single measurement in the same direction.
- **Wide IQR, median close:** likely a focus or thresholding inconsistency. Re-check Step B3/C6's focus and lighting checklist.
- **Very few particles detected despite many visible:** your threshold is too strict, or `MIN_PARTICLE_PIXELS` is too high — lower it and re-run.
- **Ellipse fit throws an error:** this happens when a contour has fewer than 5 points (very tiny speck). The script already falls back to a bounding-box estimate for these — check the console output to see how many fell into that fallback path, and treat those specific measurements with more skepticism.
- **Track 2 CV median is much larger than manual median:** possible over-segmentation isn't the issue here (that would undercount); more likely your threshold is including shadow/blur halo around each particle as part of the particle itself — tighten the threshold or improve backlight evenness.

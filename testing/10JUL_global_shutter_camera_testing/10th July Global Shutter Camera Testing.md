# Test Log — RPi Camera Setup + Initial CV Pipeline Test

## Goal
Validate the microscopy-computer-vision capture chain end-to-end on the
RPi and
get a first baseline classical-CV pipeline written and tested.

## What was done today

### 1. RPi SSH connection + camera check
- Connected to the RPi over SSH from laptop.
- Confirmed camera hardware works. This Pi uses the newer `rpicam-*`
  camera tool names (`rpicam-still`, `rpicam-vid`), not the older
  `libcamera-*` names some tutorials still reference.

### 2. Live camera feed for physical alignment
Set up a low-latency network stream to check camera framing and ruler/
sample alignment in real time, instead of guessing with repeated stills.

On the Pi:
```bash
rpicam-vid -t 0 --inline --width 640 --height 480 --framerate 15 \
  --listen -o tcp://0.0.0.0:8888
```

On the laptop:
```bash
ffplay -fflags nobuffer -flags low_delay -framedrop tcp://<pi-ip>:8888
```

See `live_feed_alignment_check.png` — screenshot used to confirm the
ruler was sitting flat/parallel to the camera sensor before capturing
the calibration still.

### 3. Pixel-to-micron calibration

**Attempt 1** (`calibration_v1_initial.jpg`): captured a still of a
1mm-increment ruler in the sample's normal position. Measured pixel
spacing between ruler ticks programmatically (column-brightness
profiling to locate tick centers, then pixel distance between
consecutive 1mm ticks).

- Finding: tick spacing was **not constant** across the frame
  (~78-100px range, ~25% variation) — sign of perspective distortion,
  most likely the ruler not sitting perfectly parallel to the camera
  sensor plane.

**Realignment**: used the live feed to re-flatten the ruler against the
same plane the sample normally sits in, checking that tick spacing
looked visually even left-to-right before capturing again.

**Attempt 2** (`calibration_v2_realigned.jpg`): captured with
`--immediate` flag (skips exposure/focus settling). Re-measured tick
spacing — still showed ~25% variation, likely because skipping the
settle time reintroduced focus/exposure inconsistency rather than the
physical alignment being wrong again.

- **Working calibration value used for today's baseline:**
  `um_per_px ≈ 6.4`, taken from the center-of-frame tick spacing
  (since that's where the actual sample sits).
- **Known limitation:** calibration is not yet tight enough for the
  ±50µm SRR accuracy target. Next attempt should avoid `--immediate`
  (let exposure/focus settle fully) on an already-realigned ruler.

### 4. Baseline CV pipeline (`particle_cv_baseline.py`)
Classical CV pipeline: grayscale → Gaussian blur → Otsu threshold →
morphological cleanup → distance-transform watershed (splits touching
particles) → per-particle contour metrics.

Metrics computed per particle, following ISO 13322-1/13322-2 convention
for irregular particles:
- **ECD** (area-equivalent circular diameter)
- **Feret max / min** (elongation proxy)
- **Circularity** and **Solidity** — used as a heuristic bubble-vs-
  particle filter, since Gelfoam fragments are expected to be
  irregular while air bubbles tend toward high circularity/solidity.

Pipeline was run end-to-end on an RPi-captured image and executes
without errors, producing a CSV of per-particle metrics and an
annotated overlay image (green = particle, orange = bubble candidate,
red = watershed split lines).

### 5. Sample image quality check

See `syringe_sample_view.png` — a captured frame of the syringe with
slurry loaded, using backlighting.

- **Finding:** individual particles are not yet clearly resolved —
  image is blurry/low-contrast even with backlighting in place.
- **Diagnosis:** most likely a focus issue rather than lighting, since
  backlighting was already active. Needs either manual focus ring
  adjustment (if available) or a camera-to-sample distance change (if
  fixed-focus lens) to find the sharp point.

## Summary of today's status
- RPi camera + live-feed + capture chain is working end-to-end.
- Calibration pipeline (ruler → pixel measurement → um_per_px) is
  implemented and produces a usable, if imprecise, value.
- CV baseline script (`particle_cv_baseline.py`) runs end-to-end
  without errors.
- Blocking issue for meaningful data: sample image focus/contrast needs
  improvement before particle detection numbers are trustworthy.

## Next steps
1. Refine camera focus until individual particle edges are visibly
   sharp in the live feed before capturing sample images.
2. Recapture calibration ruler shot without `--immediate`, on an
   already-flattened/parallel ruler mount.
3. Re-run `particle_cv_baseline.py` on a clear sample image; compare
   median ECD / IQR against manual measurement (SRR Experiment 1
   criterion: ±50µm).
4. Begin building a labeled image dataset across all 3 materials
   (Gelfoam / Lyostypt ). 

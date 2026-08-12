"""Central config for cv_verify. Mirrors the role of
../cv-pipeline/config.py and firmware/esp32/include/config.h.
"""

# ── Camera ───────────────────────────────────────────────────────────────
# OV9281-110: global shutter, MONO, CSI (via picamera2/libcamera), no IR-cut
# filter (usable into near-IR, see TODO.md Layer 2 NIR note). Different part
# from the IMX296 Global Shutter Camera ../cv-pipeline/config.py was written
# against — resolution/format below are OV9281-specific.
CAMERA_INDEX = 0
FRAME_WIDTH = 1280
FRAME_HEIGHT = 800
CAMERA_FPS = 60  # OV9281 supports up to ~120fps at lower res; 60 is a starting point, not tuned

# Exposure/gain FALLBACK only — capture.py's Camera now auto-exposes fresh
# against current lighting on every read_frame_stack() call and locks that
# result for the stack, rather than using fixed values from here. That
# replaced an earlier fixed-value approach (500us/4.0x, then a hand-tuned
# 12000us/1.5x) that kept going stale every time the physical lighting
# setup changed — confirmed on real hardware 2026-08-08, see capture.py's
# _autoexpose_and_lock() docstring for the full reasoning. These two
# constants are only read if picamera2's metadata read ever fails.
EXPOSURE_TIME_US = 10000
ANALOGUE_GAIN = 1.5

# ── UART link to ESP32-S3 ───────────────────────────────────────────────
# See firmware/esp32/src/rpi_uart.cpp for the receiving side.
# Pi Zero 2W: confirm /dev/serial0 -> /dev/ttyAMA0 after the disable-bt fix
# (SETUP.md Layer 0 step 7) — simpler/more standard than the Pi 5's RP1 chip.
UART_PORT = "/dev/serial0"
UART_BAUD = 921600

# On-demand capture protocol (see firmware/esp32/include/rpi_uart.h and
# config.h: RPI_CAPTURE_TIMEOUT_MS). The ESP32 gives up and marks the
# request TIMED_OUT if no SIZE reply arrives within this many seconds —
# capture + inference must fit inside it. Keep in sync with firmware's value.
# Bumped from 8.0 (see config.h's matching comment) — detection.py now
# calls Roboflow's hosted Serverless Cloud API, which adds real network
# round-trip + queueing time on top of capture + median-stacking.
CAPTURE_TIMEOUT_S = 20.0
CAPTURE_COMMAND = "CAPTURE"

# PROTOTYPE — raw preview image transfer (see link.py's send_image() and
# testing/CV_Verify_UART_Prototype/esp32/include/rpi_uart.h). MUST match
# that firmware copy's RPI_IMG_MAX_W/H exactly — the receiving side
# validates the header against its own compile-time buffer size and
# silently drops anything larger, so these can't just be bumped up here
# without also rebuilding/reflashing that firmware. 160x100 chosen there
# to fit directly under the ILI9341's 240px width with no scaling needed
# on the display side, and to transfer in well under a second at 921600 baud.
IMG_TRANSFER_W = 160
IMG_TRANSFER_H = 100

# ── Training data collection ─────────────────────────────────────────────
# See collect_training_data.py. One subfolder per stroke-count label under
# this directory; a raw .npy (median-stacked, pre-enhancement — kept
# lossless so the enhancement recipe can be re-run later without
# recapturing) and an enhanced .jpg (what an annotation tool would actually
# open) per sample, plus one shared metadata.csv row per sample.
TRAINING_DATA_DIR = "training_data"

# ── Model weights ───────────────────────────────────────────────────────
# Gitignored — see ../cv-pipeline/README.md. detection.py/sizing.py are
# imported from ../cv-pipeline (see detection_sizing.py in this folder) —
# not duplicated here.
WEIGHTS_PATH = "../cv-pipeline/weights/best.pt"

# ── Roboflow hosted inference (PoC) ──────────────────────────────────────
# Local/offline inference (weights export + self-hosted `inference`
# package) is blocked on the current Roboflow plan ("weights export not
# included", confirmed 2026-08-12) — detection.py uses the hosted
# Serverless Cloud API instead, trading an internet dependency on the Pi
# for being unblocked right now. See SESSION_HANDOFF.md.
#
# ROBOFLOW_API_KEY is intentionally NOT here — read from the
# ROBOFLOW_API_KEY environment variable on the Pi instead (see
# ~/.bashrc), so the key never lives in a committed file.
ROBOFLOW_MODEL_ID = "vincent-santosa/particle-size-ecd/3"
ROBOFLOW_API_URL = "https://serverless.roboflow.com"
# "Optimal" confidence threshold from the v3 model's evaluation page (best
# F1 balance point, mAP@50 51.5%/F1 53.3% on a 10-image test set — small
# sample, treat as a rough starting point, not a precise number). Retune
# from the model's Evaluation > Production Metrics Explorer if it's
# over/under-detecting in practice.
ROBOFLOW_CONFIDENCE = 0.39

# ── Verification-screen outputs ─────────────────────────────────────────
# Per project direction: the eventual on-demand verification screen shows
# a PSD histogram + median statement on one side and the processed image
# actually used for CV on the other. Firmware/UI wiring for that doesn't
# exist yet — this is just where main.py writes the processed image every
# capture, so that work has a real file to read from instead of starting
# from nothing. Overwritten every CAPTURE, not archived per-run.
LAST_CAPTURE_IMAGE_PATH = "last_capture.jpg"

# ── Pixel-to-micron calibration ─────────────────────────────────────────
# Measured 2026-08-08 against a steel ruler held at the intended working
# distance: two major (10.0mm apart) tick marks measured at 493.5px
# (cross-checked against the median 1mm-minor-tick spacing, agreed within
# ~2%) -> 10000um / 493.5px = 20.26 um/px. See scratch/ruler2.jpg and
# scratch/ruler2_peaks.png for the source image/measurement.
#
# This is a ROUGH bench calibration, not the formal one —
# ../cv-pipeline/master_experiment1_validation_protocol.md's Track 1 (known
# microspheres) is still the real validation step before trusting this for
# anything beyond preprocessing/dev work. Re-measure if the lens, working
# distance, or zoom changes at all.
UM_PER_PIXEL = 20.26

# ── Preprocessing ────────────────────────────────────────────────────────
# Both address the low-contrast problem confirmed on real hardware
# (see conversation/TODO.md Layer 2) — the syringe contents currently show
# only a few percent of local contrast even after glare is removed, so
# these aren't optional polish, they're load-bearing before detection.py
# has any chance of finding real edges.

# Number of frames to median-stack per capture. Cancels random sensor
# read/shot noise (visible as speckle in single-frame equalized views)
# while preserving real structure — only valid because captures happen
# between strokes when the syringe contents are momentarily static, not
# during motion. Higher = less noise but longer per-CAPTURE latency; stay
# well under CAPTURE_TIMEOUT_S's budget (see main.py's warning threshold).
STACK_FRAMES = 6

# Gaussian blur radius (px) used to estimate the smooth illumination/glare
# gradient that gets divided out in flat-field correction. Too small and it
# starts smoothing out real particle-scale structure instead of just the
# gradient; too large and it stops tracking real vignette/glare falloff.
# 40px was what worked on the test frames during bring-up — not yet tuned
# against real dark-field-lit syringe frames.
FLATFIELD_BLUR_RADIUS = 40

# Local-contrast ("clarity") enhancement — see preprocessing.clarity_boost().
# Added 2026-08-08 after the open-top syringe mount was confirmed to cap
# how far dark-field lighting alone can improve contrast (physical mount
# needs an open top for easy loading, ruling out a fully enclosed dark-field
# setup) — this is the software-side compensation for that hard physical
# limit, not a substitute for continuing to improve the optics.
# CLARITY_BLUR_RADIUS: neighborhood size (px) the unsharp mask compares
# each pixel against — smaller values boost finer-grained texture.
# CLARITY_AMOUNT: how strongly the local difference gets boosted (1.8 was
# what visually matched a manual Canva slider experiment on a real frame —
# see the conversation this was ported from). Both are subjective/visual
# tuning choices, not derived from a measurement — revisit once real
# training data collection starts and there's a numeric target (e.g.
# segmentation quality) to tune against instead of eyeballing.
CLARITY_BLUR_RADIUS = 20
CLARITY_AMOUNT = 1.8

# Percentile stretch used specifically for the final display/detection
# image (tighter than to_uint8_stretched()'s default 1.0 — matches the
# more aggressive Contrast/Highlights/Shadows push from the Canva
# experiment this was ported from).
DISPLAY_STRETCH_PERCENTILE = 3.0

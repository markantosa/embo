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

# Manual exposure/gain — freeze fast-moving particles without relying on
# strobed illumination (same reasoning as ../cv-pipeline/config.py).
# Untuned placeholders — confirm against real particle velocity through the
# syringe/stopcock during Layer 0/1 bring-up.
EXPOSURE_TIME_US = 500
ANALOGUE_GAIN = 4.0

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
CAPTURE_TIMEOUT_S = 8.0
CAPTURE_COMMAND = "CAPTURE"

# ── Model weights ───────────────────────────────────────────────────────
# Gitignored — see ../cv-pipeline/README.md. detection.py/sizing.py are
# imported from ../cv-pipeline (see detection_sizing.py in this folder) —
# not duplicated here.
WEIGHTS_PATH = "../cv-pipeline/weights/best.pt"

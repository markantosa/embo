"""Central config for the CV pipeline — mirrors the role of
firmware/esp32/include/config.h on the ESP32 side. Keep constants here
rather than scattered through the modules.
"""

# ── Camera ───────────────────────────────────────────────────────────────
# Raspberry Pi Global Shutter Camera (IMX296, CSI, via picamera2/libcamera).
# Reverted back from the USB UVC microscope camera tried earlier — the UVC
# camera's rolling shutter produced confirmed motion blur/skew on fast
# particle movement (see SOFTWARE_TODO.md task 12, 3 July 2026 test notes).
# Global shutter + a short manual exposure freezes motion instead.
CAMERA_INDEX = 0
FRAME_WIDTH = 1280
FRAME_HEIGHT = 720
CAMERA_FPS = 30

# Manual exposure/gain — global shutter's whole point is a controlled,
# short exposure to freeze fast-moving particles instead of relying on
# strobed illumination. Placeholders: not yet tuned against real particle
# velocity through the syringe/stopcock (see SOFTWARE_TODO.md task 1).
EXPOSURE_TIME_US = 500
ANALOGUE_GAIN = 4.0

# ── UART link to ESP32-S3 ───────────────────────────────────────────────
# See firmware/esp32/src/rpi_uart.cpp for the receiving side.
# RPi 5 uses the RP1 I/O chip for UART, unlike earlier Pi models — confirm
# /dev/serial0 actually maps to GPIO14/15 on the real hardware before
# trusting this default (see SOFTWARE_TODO.md task 2).
UART_PORT = "/dev/serial0"
UART_BAUD = 921600

# ── Model weights ───────────────────────────────────────────────────────
# Gitignored — obtain from team drive, see cv-pipeline/README.md.
WEIGHTS_PATH = "weights/best.pt"

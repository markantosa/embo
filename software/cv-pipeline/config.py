"""Central config for the CV pipeline — mirrors the role of
firmware/esp32/include/config.h on the ESP32 side. Keep constants here
rather than scattered through the modules.
"""

# ── Camera ───────────────────────────────────────────────────────────────
# Off-the-shelf USB 2.0 UVC microscope camera (see SOFTWARE_TODO.md task 1).
# Not the originally planned Raspberry Pi Global Shutter Camera — no CSI
# involved, read via OpenCV/v4l2 instead of picamera2.
CAMERA_INDEX = 0
FRAME_WIDTH = 1280
FRAME_HEIGHT = 720
CAMERA_FPS = 30

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

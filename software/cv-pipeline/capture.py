"""Raspberry Pi Global Shutter Camera (IMX296, CSI) capture via picamera2.

See SOFTWARE_TODO.md Layer 1 task 1. Replaces the earlier USB UVC
microscope camera path (OpenCV/v4l2) — the UVC camera's rolling shutter
produced confirmed motion blur/skew on fast particle movement (3 July 2026
stopcock aperture test, see task 12). Global shutter + a short manual
exposure freezes motion without needing strobed illumination.

Requires the `picamera2` system package (installed via `sudo apt install
python3-picamera2`, not pip — see RPI_SETUP_GUIDE.md). The venv must be
created with `--system-site-packages` so it can see it.
"""
from picamera2 import Picamera2

from config import (
    CAMERA_INDEX,
    FRAME_WIDTH,
    FRAME_HEIGHT,
    CAMERA_FPS,
    EXPOSURE_TIME_US,
    ANALOGUE_GAIN,
)


class Camera:
    def __init__(self, index: int = CAMERA_INDEX):
        self._picam2 = Picamera2(camera_num=index)

        # "RGB888" is picamera2's name for the format but it's actually
        # laid out BGR in memory, which is what OpenCV expects — verify
        # this against a known-color test target during bring-up rather
        # than trusting it blind (picamera2 docs are inconsistent on this
        # point across versions).
        config = self._picam2.create_video_configuration(
            main={"size": (FRAME_WIDTH, FRAME_HEIGHT), "format": "RGB888"},
            controls={"FrameRate": CAMERA_FPS},
        )
        self._picam2.configure(config)

        # Manual exposure/gain so frame-to-frame brightness is consistent
        # for sizing, and short enough to freeze fast-moving particles.
        # EXPOSURE_TIME_US/ANALOGUE_GAIN are untuned placeholders — see
        # config.py.
        self._picam2.set_controls({
            "AeEnable": False,
            "ExposureTime": EXPOSURE_TIME_US,
            "AnalogueGain": ANALOGUE_GAIN,
        })

        self._picam2.start()

    def read_frame(self):
        """Return one BGR frame, or None if the read failed."""
        try:
            return self._picam2.capture_array()
        except RuntimeError:
            return None

    def release(self):
        self._picam2.stop()

"""OV9281-110 (global shutter, mono, CSI) capture via picamera2.

Same picamera2/libcamera approach as ../cv-pipeline/capture.py, different
sensor. Requires the `picamera2` system package (`sudo apt install
python3-picamera2`, not pip — venv must be created with
`--system-site-packages` so it can see it). See SETUP.md Layer 0 step 6.
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

        # OV9281 is mono — "R8"/"Y8" (8-bit greyscale) is the natural format,
        # not RGB888. Confirm the exact format string libcamera reports for
        # this sensor during bring-up (`libcamera-hello --list-cameras`
        # shows supported formats) before trusting this blind.
        config = self._picam2.create_video_configuration(
            main={"size": (FRAME_WIDTH, FRAME_HEIGHT), "format": "R8"},
            controls={"FrameRate": CAMERA_FPS},
        )
        self._picam2.configure(config)

        # Manual exposure/gain so frame-to-frame brightness is consistent
        # for sizing, and short enough to freeze fast-moving particles.
        # Untuned placeholders — see config.py.
        self._picam2.set_controls({
            "AeEnable": False,
            "ExposureTime": EXPOSURE_TIME_US,
            "AnalogueGain": ANALOGUE_GAIN,
        })

        self._picam2.start()

    def read_frame(self):
        """Return one greyscale frame (H x W uint8), or None if the read failed."""
        try:
            return self._picam2.capture_array()
        except RuntimeError:
            return None

    def release(self):
        self._picam2.stop()

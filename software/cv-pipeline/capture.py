"""USB 2.0 microscope camera capture (UVC), read via OpenCV/v4l2.

See SOFTWARE_TODO.md Layer 1 task 1. This replaces the originally planned
Raspberry Pi Global Shutter Camera (CSI/picamera2) — the camera in use now
is very likely a rolling-shutter sensor, which matters later for the
strobed-illumination stopcock imaging work (task 12), but doesn't affect
this basic capture path.
"""
import cv2

from config import CAMERA_INDEX, FRAME_WIDTH, FRAME_HEIGHT, CAMERA_FPS


class Camera:
    def __init__(self, index: int = CAMERA_INDEX):
        self._cap = cv2.VideoCapture(index, cv2.CAP_V4L2)
        if not self._cap.isOpened():
            raise RuntimeError(f"could not open camera at index {index}")

        self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
        self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
        self._cap.set(cv2.CAP_PROP_FPS, CAMERA_FPS)

        # Lock auto-exposure/autofocus where the driver supports it, so
        # frame-to-frame brightness/focus is consistent for sizing. UVC
        # driver support for these varies a lot by camera model — confirm
        # these calls actually take effect on the real hardware rather than
        # silently no-op'ing (check with `v4l2-ctl -d /dev/video0 --all`).
        self._cap.set(cv2.CAP_PROP_AUTO_EXPOSURE, 1)  # 1 = manual on most UVC drivers
        self._cap.set(cv2.CAP_PROP_AUTOFOCUS, 0)

    def read_frame(self):
        """Return one BGR frame, or None if the read failed."""
        ok, frame = self._cap.read()
        if not ok:
            return None
        return frame

    def release(self):
        self._cap.release()

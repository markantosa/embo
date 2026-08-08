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
    STACK_FRAMES,
)
from preprocessing import stack_frames


class Camera:
    def __init__(self, index: int = CAMERA_INDEX):
        self._picam2 = Picamera2(camera_num=index)

        # "R8" (raw 8-bit mono) is a valid *sensor* mode (confirmed via
        # `rpicam-hello --list-cameras` on real hardware) but picamera2's
        # main/ISP output stream only accepts its own set of formats
        # (RGB888/YUV420/MJPEG/...), not raw sensor codes — those only
        # exist on the separate `raw` stream. RGB888 here is genuinely
        # RGB-triplicated mono (R=G=B=intensity) from the ISP, same as
        # ../cv-pipeline/capture.py uses for the (color) IMX296 — fine for
        # now since detection.py doesn't care about color. Revisit with a
        # `raw={"format": "R8", ...}` stream instead if/when true 8-bit
        # single-channel data matters (e.g. for calibrating exposure
        # against actual sensor counts rather than ISP-processed values).
        config = self._picam2.create_video_configuration(
            main={"size": (FRAME_WIDTH, FRAME_HEIGHT), "format": "RGB888"},
            controls={"FrameRate": CAMERA_FPS},
        )
        self._picam2.configure(config)

        # Start with auto-exposure on — see _autoexpose_and_lock()'s
        # docstring for why fixed config.py constants turned out to be the
        # wrong approach here. This just gets the camera running; the real
        # exposure decision happens per-capture in read_frame_stack().
        self._picam2.set_controls({"AeEnable": True})
        self._picam2.start()

    def _autoexpose_and_lock(self, settle_frames: int = 4) -> None:
        """Let auto-exposure converge against whatever lighting is
        currently set up, then lock ExposureTime/AnalogueGain to that
        result for AeEnable=False for the rest of the capture.

        Why not just use config.EXPOSURE_TIME_US/ANALOGUE_GAIN directly:
        those were fixed placeholders, and fixed values go stale the
        moment the physical lighting setup changes (confirmed on real
        hardware 2026-08-08 — a value that matched one lighting setup
        overexposed badly under a different one). Re-deriving the right
        exposure from AE itself, every capture, adapts automatically
        instead of needing config.py hand-tuned after every lighting
        change. config.EXPOSURE_TIME_US/ANALOGUE_GAIN are kept only as a
        documented fallback if this ever needs to be bypassed.

        Locking (not leaving AeEnable=True for the whole stack) still
        matters — see read_frame_stack()'s docstring on why the stack
        needs consistent exposure across all n frames.
        """
        self._picam2.set_controls({"AeEnable": True})
        for _ in range(settle_frames):
            self._picam2.capture_metadata()
        metadata = self._picam2.capture_metadata()
        exposure = metadata.get("ExposureTime", EXPOSURE_TIME_US)
        gain = metadata.get("AnalogueGain", ANALOGUE_GAIN)
        self._picam2.set_controls({
            "AeEnable": False,
            "ExposureTime": exposure,
            "AnalogueGain": gain,
        })
        # Exposed for callers that want to log what was actually used (e.g.
        # collect_training_data.py's metadata CSV) — not used internally.
        self.last_exposure_us = exposure
        self.last_analogue_gain = gain

    def read_frame(self):
        """Return one BGR frame (H x W x 3 uint8, R=G=B since the sensor is
        mono), or None if the read failed."""
        try:
            return self._picam2.capture_array()
        except RuntimeError:
            return None

    def read_frame_stack(self, n: int = STACK_FRAMES):
        """Auto-expose against current lighting, lock it, then capture n
        frames back-to-back and median-stack them (see
        preprocessing.stack_frames) to cancel random sensor noise. Locking
        exposure before the stack (rather than per-frame AE) matters:
        median-stacking assumes frames differ only by noise, not by
        brightness — letting AE keep adjusting mid-stack would blend
        genuinely different exposures together and degrade the result
        rather than cleaning it up. Returns one greyscale (H x W) float32
        array, or None if every capture in the batch failed. Only valid
        when the scene is static for the duration — see
        config.STACK_FRAMES's comment.
        """
        self._autoexpose_and_lock()
        frames = []
        for _ in range(n):
            frame = self.read_frame()
            if frame is not None:
                frames.append(frame[..., 0])  # R=G=B (mono), collapse to one channel
        if not frames:
            return None
        return stack_frames(frames)

    def release(self):
        self._picam2.stop()

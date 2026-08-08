"""cv_verify entry point — on-demand, single-shot CV verification.

Replaces ../cv-pipeline/main.py's free-running "capture every 0.1s and
stream" loop with a request/reply loop: idle (sending nothing) until the
ESP32 sends "CAPTURE", then capture one frame, attempt detection+sizing,
and reply. See TODO.md Layer 1 and link.py's module docstring for the wire
protocol.

detection.py/sizing.py are intentionally NOT duplicated here — they're
loop/trigger-agnostic (see TODO.md's closing note), so this imports them
straight from ../cv-pipeline. Both are still NotImplementedError stubs,
blocked on Layer 2 (optics) per SOFTWARE_TODO.md; this loop already handles
that by reporting a status string instead of crashing, same as
../cv-pipeline/main.py did.
"""
import sys
import time
from pathlib import Path

# ../cv-pipeline holds detection.py/sizing.py — see module docstring above
# for why these are imported rather than copied.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "cv-pipeline"))

import config
from capture import Camera
from link import FirmwareLink
from detection import ParticleDetector  # from ../cv-pipeline, stub for now
from sizing import compute_ecd_stats     # from ../cv-pipeline, stub for now

POLL_INTERVAL_S = 0.05  # link.py's readline() already timeouts at 0.1s; this just avoids a tight spin


def handle_capture(camera: Camera, link: FirmwareLink, detector: ParticleDetector) -> None:
    """One capture-and-reply cycle. Must complete well inside
    config.CAPTURE_TIMEOUT_S — see link.py's module docstring.
    """
    started = time.monotonic()

    frame = camera.read_frame()
    if frame is None:
        link.send_status("CV: bad frame, capture aborted")
        return

    try:
        masks = detector.detect(frame)
        # TODO: um_per_pixel=1.0 is a placeholder — needs real calibration
        # against a known reference before this number means anything
        # (SOFTWARE_TODO.md task 8 verification gate).
        stats = compute_ecd_stats(masks, um_per_pixel=1.0)
        link.send_size(stats.median_um, stats.iqr_um)
    except NotImplementedError:
        link.send_status("CV: detection/sizing not implemented yet")
        return

    elapsed = time.monotonic() - started
    if elapsed > config.CAPTURE_TIMEOUT_S * 0.8:
        # Not a hard failure (firmware doesn't hear about this), but a loud
        # local warning — getting close to RPI_CAPTURE_TIMEOUT_MS is worth
        # knowing about before it starts actually timing out intermittently.
        print(f"WARNING: capture+reply took {elapsed:.2f}s, "
              f"budget is {config.CAPTURE_TIMEOUT_S:.2f}s", file=sys.stderr)


def main():
    camera = Camera()
    link = FirmwareLink()
    detector = ParticleDetector(config.WEIGHTS_PATH)

    link.send_status("CV: init OK, waiting for CAPTURE")

    try:
        while True:
            if link.wait_for_capture_request():
                handle_capture(camera, link, detector)
            else:
                time.sleep(POLL_INTERVAL_S)
    except KeyboardInterrupt:
        pass
    finally:
        camera.release()
        link.close()


if __name__ == "__main__":
    main()

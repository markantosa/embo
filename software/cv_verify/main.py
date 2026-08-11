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

import serial
from PIL import Image

# ../cv-pipeline holds detection.py/sizing.py — see module docstring above
# for why these are imported rather than copied. Appended, not inserted at
# index 0 — cv-pipeline has its own config.py, and this folder's own
# modules (including config.py) must resolve first, not be shadowed by it.
sys.path.append(str(Path(__file__).resolve().parent.parent / "cv-pipeline"))

import config
from capture import Camera
from link import FirmwareLink
from preprocessing import enhance_for_display
from detection import ParticleDetector  # from ../cv-pipeline, stub for now
from sizing import compute_ecd_stats     # from ../cv-pipeline, stub for now

POLL_INTERVAL_S = 0.05  # link.py's readline() already timeouts at 0.1s; this just avoids a tight spin


def handle_capture(camera: Camera, link: FirmwareLink, detector: ParticleDetector) -> None:
    """One capture-and-reply cycle. Must complete well inside
    config.CAPTURE_TIMEOUT_S — see link.py's module docstring.
    """
    started = time.monotonic()

    # Median-stack (cancels sensor noise) + flat-field + clarity boost
    # (removes the glare gradient, then locally boosts what contrast is
    # left) before detection — validated interactively against real
    # low-contrast frames during bring-up, see config.py's Preprocessing
    # section. Not optional polish: the open-top syringe mount caps how
    # far dark-field lighting alone can go, so software has to make up the
    # rest of the gap.
    raw = camera.read_frame_stack()
    if raw is None:
        link.send_status("CV: bad frame, capture aborted")
        return
    frame = enhance_for_display(raw)

    # Saved unconditionally (success or NotImplementedError below) — this
    # is the "processed image" half of the eventual verification-screen
    # display (median/histogram on one side, this image on the other, per
    # project direction). Firmware/UI wiring for that isn't built yet;
    # this just makes sure the file exists and is always current so that
    # work has something real to point at instead of starting from zero.
    full_res_image = Image.fromarray(frame)
    try:
        full_res_image.save(config.LAST_CAPTURE_IMAGE_PATH)
    except OSError as e:
        print(f"WARNING: could not save processed image: {e}", file=sys.stderr)

    # PROTOTYPE (see testing/CV_Verify_UART_Prototype): sends the actual
    # captured/enhanced image over UART for on-device display, ahead of
    # detection/sizing existing. Downscaled to IMG_TRANSFER_W/H — MUST
    # match the prototype firmware's RPI_IMG_MAX_W/H exactly, or the
    # firmware-side header validation silently drops the image (see that
    # copy's rpi_uart.cpp). Sent unconditionally, independent of whether
    # detection below succeeds — the image is real regardless of whether
    # sizing is.
    try:
        preview = full_res_image.convert("L").resize(
            (config.IMG_TRANSFER_W, config.IMG_TRANSFER_H), Image.LANCZOS
        )
        link.send_image(config.IMG_TRANSFER_W, config.IMG_TRANSFER_H, preview.tobytes())
    except (OSError, serial.SerialException) as e:
        print(f"WARNING: could not send preview image: {e}", file=sys.stderr)

    try:
        masks = detector.detect(frame)
        # UM_PER_PIXEL measured 2026-08-08 against a ruler at the working
        # distance — see config.py's Pixel-to-micron calibration section
        # for the measurement and its caveats (rough bench value, not the
        # formal Track 1 validation from master_experiment1_validation_protocol.md).
        stats = compute_ecd_stats(masks, um_per_pixel=config.UM_PER_PIXEL)
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

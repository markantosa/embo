"""EMBO CV pipeline entry point.

Current status: Layer 1 (camera + UART bring-up) only — see
SOFTWARE_TODO.md. detection.py/sizing.py (Layers 2-3) are stubs that raise
NotImplementedError, so this loop catches that specifically and reports it
as a status string over UART instead of crashing. That lets the Layer 1
verification gate (camera captures frames, firmware BLE log shows RPi
packets arriving) be exercised today, before real particle sizing exists.
"""
import time

import config
from capture import Camera
from uart_link import FirmwareLink
from detection import ParticleDetector
from sizing import compute_ecd_stats

LOOP_INTERVAL_S = 0.1


def main():
    camera = Camera()
    link = FirmwareLink()
    detector = ParticleDetector(config.WEIGHTS_PATH)

    link.send_status("CAM: init OK")

    try:
        while True:
            frame = camera.read_frame()
            if frame is None:
                link.send_status("CAM: bad frame")
                time.sleep(LOOP_INTERVAL_S)
                continue

            try:
                masks = detector.detect(frame)
                # TODO: um_per_pixel=1.0 is a placeholder — needs real
                # calibration against a known reference before this number
                # means anything (SOFTWARE_TODO.md task 8 verification gate).
                stats = compute_ecd_stats(masks, um_per_pixel=1.0)
                link.send_size(stats.median_um, stats.iqr_um)
            except NotImplementedError:
                link.send_status("CV: detection/sizing not implemented yet")

            time.sleep(LOOP_INTERVAL_S)
    except KeyboardInterrupt:
        pass
    finally:
        camera.release()
        link.close()


if __name__ == "__main__":
    main()

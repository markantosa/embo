"""Equivalent Circular Diameter (ECD) sizing from instance-segmentation
polygons, per ISO 13322 / ISO 9276-6 (area-equivalent circle diameter) —
NOT a bounding-box/Feret approximation, since gelatin/collagen fragments
are irregular ("snowflake"-like), not circular, so those would misrepresent
size.

See docs/EMBO_UAS_CV_Technical_Advisory.txt section 2 Problem C for the
fuller reasoning this was originally scoped against.
"""
from dataclasses import dataclass

import numpy as np


@dataclass
class SizeStats:
    median_um: int
    iqr_um: int
    # Feret diameter (min/max) and solidity are useful secondary signals
    # (see advisory) but are NOT sent over UART — the wire protocol
    # (firmware/esp32/include/rpi_uart.h) only carries median+IQR.


def _polygon_area_px(points: np.ndarray) -> float:
    """Shoelace formula. points is an (N, 2) array of (x, y) pixel coords."""
    x = points[:, 0]
    y = points[:, 1]
    return 0.5 * abs(np.dot(x, np.roll(y, 1)) - np.dot(y, np.roll(x, 1)))


def compute_ecd_stats(masks: list, um_per_pixel: float) -> SizeStats:
    """Compute ECD median/IQR from a list of per-particle polygons (each an
    (N, 2) array of pixel-coordinate points, see detection.py).

    Raises ValueError if masks is empty or none have positive area —
    callers must treat "no particles detected" as a distinct case from a
    successful zero-particle reading, not silently send a fabricated
    SIZE 0 0 over UART.
    """
    if not masks:
        raise ValueError("compute_ecd_stats() called with no detected particles")

    ecds_um = []
    for poly in masks:
        area_px2 = _polygon_area_px(poly)
        if area_px2 <= 0:
            continue
        ecd_px = 2.0 * np.sqrt(area_px2 / np.pi)
        ecds_um.append(ecd_px * um_per_pixel)

    if not ecds_um:
        raise ValueError("compute_ecd_stats() found no particles with positive area")

    arr = np.array(ecds_um)
    median = float(np.median(arr))
    q75, q25 = np.percentile(arr, [75, 25])
    iqr = float(q75 - q25)
    return SizeStats(median_um=round(median), iqr_um=round(iqr))

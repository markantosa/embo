"""Equivalent Circular Diameter (+ Feret/solidity) sizing from instance
masks. STUB — not implemented.

See SOFTWARE_TODO.md Layer 3 task 8 and
docs/EMBO_UAS_CV_Technical_Advisory.txt section 2 Problem C.

Gelatin/collagen fragments are irregular ("snowflake"-like), not circular,
so a naive bounding-box diameter isn't well-defined. ECD (diameter of a
circle with equal projected area, per ISO 13322 / ISO 9276-6) is the
metric to implement here, not a placeholder approximation — a fake sizing
formula would silently poison the PID target check on the firmware side.
"""
from dataclasses import dataclass


@dataclass
class SizeStats:
    median_um: int
    iqr_um: int
    # Feret diameter (min/max) and solidity are useful secondary signals
    # (see advisory) but are NOT sent over UART yet — see SOFTWARE_TODO.md
    # task 9 before adding fields to the firmware protocol.


def compute_ecd_stats(masks, um_per_pixel: float) -> SizeStats:
    """Compute ECD median/IQR from a list of per-particle instance masks.

    TODO (Layer 3 task 8): implement per ISO 13322 / ISO 9276-6. Requires
    `um_per_pixel` calibrated against a known reference (e.g. glass beads
    of known diameter) before any output here can be trusted — see the
    task 8 verification gate in SOFTWARE_TODO.md.
    """
    raise NotImplementedError(
        "compute_ecd_stats() not implemented — see SOFTWARE_TODO.md Layer 3 task 8"
    )

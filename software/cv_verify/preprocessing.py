"""Preprocessing for low-contrast frames — see config.py's Preprocessing
section for why this exists. Both steps were validated interactively
against real captured frames during bring-up (see scratch/test6_flatfield.jpg
and the conversation it came from), not just written from theory.

Both functions operate on greyscale (H x W) float/uint8 arrays, since that's
what the sizing pipeline actually cares about — capture.py's Camera returns
BGR-triplicated mono (R=G=B), so callers should collapse to one channel
(e.g. frame[..., 0]) before calling these.
"""
import numpy as np
from PIL import Image, ImageFilter

from config import FLATFIELD_BLUR_RADIUS, CLARITY_BLUR_RADIUS, CLARITY_AMOUNT, DISPLAY_STRETCH_PERCENTILE


def stack_frames(frames: list) -> np.ndarray:
    """Median-stack a list of greyscale frames (same shape) to cancel
    random sensor noise. Requires the scene to be static across the
    capture — only valid between strokes, not during motion.
    """
    if not frames:
        raise ValueError("stack_frames() called with no frames")
    stack = np.stack([np.asarray(f, dtype=np.float32) for f in frames], axis=0)
    return np.median(stack, axis=0)


def flat_field_correct(frame: np.ndarray) -> np.ndarray:
    """Divide out a heavily-blurred estimate of the illumination/glare
    gradient, isolating local texture. Returns a float32 array centered
    near the frame's original mean brightness (not yet stretched/clipped
    to uint8 — callers decide their own display/threshold scaling since
    that depends on what comes next, e.g. detection.py's own normalization).
    """
    arr = np.asarray(frame, dtype=np.float32)
    im = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))
    bg = np.asarray(im.filter(ImageFilter.GaussianBlur(radius=FLATFIELD_BLUR_RADIUS)), dtype=np.float32)
    return arr / (bg + 1e-3) * np.mean(bg)


def to_uint8_stretched(arr: np.ndarray, percentile: float = 1.0) -> np.ndarray:
    """Percentile-stretch a float array to 0-255 uint8 for display or for
    feeding to a detector that expects standard 8-bit input. Robust to
    outlier pixels (glare hotspots) unlike a plain min/max stretch.
    """
    lo, hi = np.percentile(arr, [percentile, 100 - percentile])
    if hi <= lo:
        return np.zeros_like(arr, dtype=np.uint8)
    return np.clip((arr - lo) / (hi - lo) * 255, 0, 255).astype(np.uint8)


def clarity_boost(u8_frame: np.ndarray, radius: float = CLARITY_BLUR_RADIUS,
                   amount: float = CLARITY_AMOUNT) -> np.ndarray:
    """Local-contrast ("clarity") enhancement via unsharp masking: boost
    the difference between each pixel and its local blurred neighborhood.
    Same operation as a photo editor's Clarity/Sharpness sliders, applied
    programmatically — confirmed 2026-08-08 to noticeably improve visible
    particle-vs-background separation on real low-contrast syringe frames
    (manually verified via a Canva slider experiment before being ported
    here), given the open-top syringe mount caps how far dark-field
    lighting alone can go. Takes and returns a uint8 array — apply this
    AFTER flat_field_correct()+to_uint8_stretched(), not before, so the
    unsharp mask is boosting real local structure rather than the raw
    glare gradient.
    """
    im = Image.fromarray(u8_frame)
    blurred = np.asarray(im.filter(ImageFilter.GaussianBlur(radius=radius)), dtype=np.float32)
    arr = np.asarray(im, dtype=np.float32)
    boosted = arr + amount * (arr - blurred)
    return to_uint8_stretched(boosted, percentile=DISPLAY_STRETCH_PERCENTILE)


def enhance_for_display(raw_stack: np.ndarray) -> np.ndarray:
    """Full low-contrast enhancement chain: flat-field -> stretch ->
    clarity boost. One call for the common case (main.py's capture
    handler, or anything generating the operator-facing processed-image
    display) — see the individual functions' docstrings for why each step
    exists and what order they need to run in.
    """
    return clarity_boost(to_uint8_stretched(flat_field_correct(raw_stack)))

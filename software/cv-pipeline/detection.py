"""Instance segmentation of particles in a frame — Roboflow-hosted RF-DETR
model (Serverless Cloud API), PoC implementation.

Local/offline inference (weights export + self-hosted `inference` package)
is blocked on the current Roboflow plan ("weights export not included",
confirmed 2026-08-12) — this trades an internet dependency for being
unblocked right now. Revisit if/when the plan is upgraded or offline
operation becomes a hard requirement (see cv_verify/SESSION_HANDOFF.md).

Reads ROBOFLOW_MODEL_ID/ROBOFLOW_API_URL/ROBOFLOW_CONFIDENCE from config.py
(cv_verify's copy wins over this file's own config.py when imported via
main.py — see that file's sys.path docstring) and the API key from the
ROBOFLOW_API_KEY environment variable, deliberately not from any committed
file.
"""
import os
import tempfile

import numpy as np
from PIL import Image
from inference_sdk import InferenceHTTPClient

from config import ROBOFLOW_MODEL_ID, ROBOFLOW_API_URL, ROBOFLOW_CONFIDENCE


class ParticleDetector:
    def __init__(self, weights_path: str | None = None):
        # weights_path kept only for call-site compatibility with the
        # original stub (main.py calls ParticleDetector(config.WEIGHTS_PATH))
        # — unused here, see module docstring.
        self.weights_path = weights_path
        api_key = os.environ.get("ROBOFLOW_API_KEY")
        if not api_key:
            raise RuntimeError(
                "ROBOFLOW_API_KEY environment variable not set — export it "
                "before running main.py (see SESSION_HANDOFF.md)"
            )
        self._client = InferenceHTTPClient(api_url=ROBOFLOW_API_URL, api_key=api_key)

    def detect(self, frame: np.ndarray) -> list:
        """Run instance segmentation on one greyscale (H x W) frame.

        Returns a list of polygons, each an (N, 2) float32 array of (x, y)
        pixel coordinates in `frame`'s coordinate space — empty list if
        nothing cleared ROBOFLOW_CONFIDENCE (including "no particles in
        frame", which is a normal outcome, not an error).
        """
        img = Image.fromarray(np.clip(frame, 0, 255).astype(np.uint8))
        # Written to a real file rather than passed as a PIL/array object —
        # matches Roboflow's own documented example exactly
        # (CLIENT.infer("image.jpg", ...)), avoiding any ambiguity about
        # which in-memory input types the SDK does/doesn't accept.
        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=True) as tmp:
            img.save(tmp.name)
            result = self._client.infer(tmp.name, model_id=ROBOFLOW_MODEL_ID)

        # Serverless API returns a single dict for one image; be tolerant
        # of a list wrapper too (some SDK versions/batch paths do this).
        if isinstance(result, list):
            result = result[0] if result else {}

        polygons = []
        for pred in result.get("predictions", []):
            if pred.get("confidence", 0) < ROBOFLOW_CONFIDENCE:
                continue
            pts = pred.get("points")
            if not pts:
                continue
            polygons.append(np.array([[p["x"], p["y"]] for p in pts], dtype=np.float32))
        return polygons


def draw_overlay(image: Image.Image, masks: list) -> Image.Image:
    """Draw segmentation polygon outlines on a greyscale copy of `image` —
    the "photo with segmentation blobs" shown on the ESP32's verification
    screen. Outline-only (not filled), so underlying image detail stays
    visible inside/near each blob. Returns a new image; `image` is not
    modified in place.
    """
    from PIL import ImageDraw

    overlay = image.convert("L").copy()
    draw = ImageDraw.Draw(overlay)
    for poly in masks:
        pts = [(float(px), float(py)) for px, py in poly]
        if len(pts) >= 2:
            draw.polygon(pts, outline=255, width=2)
    return overlay

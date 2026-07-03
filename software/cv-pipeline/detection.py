"""Instance segmentation of particles in a frame. STUB — not implemented.

See SOFTWARE_TODO.md Layer 3 task 7 and
docs/EMBO_UAS_CV_Technical_Advisory.txt section 2 Problem B.

Deliberately not implemented yet: training data collected before the
Layer 2 optical setup (dye/lighting) is finalized isn't valid, since the
optical changes alter what the camera actually sees. Wiring this up early
with a fake/placeholder model would let a broken pipeline look like it
works — better to fail loudly via NotImplementedError until it's real.
"""


class ParticleDetector:
    def __init__(self, weights_path: str | None = None):
        self.weights_path = weights_path
        self._model = None
        # TODO (Layer 3 task 7): load a YOLOv8-seg (ultralytics) or Cellpose
        # model from weights_path. Evaluate both — Cellpose is purpose-built
        # for dense/touching irregular blobs, which is exactly this problem.

    def detect(self, frame):
        """Return a list of per-particle instance masks for one frame.

        TODO: replace with real inference once Layer 2 (optical setup) is
        settled and training data exists.
        """
        raise NotImplementedError(
            "ParticleDetector.detect() not implemented — see SOFTWARE_TODO.md Layer 3 task 7"
        )

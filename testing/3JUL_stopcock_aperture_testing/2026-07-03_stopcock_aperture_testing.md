# Stopcock Aperture Camera Testing — 3 July 2026

Follow-up to [`../3JUL_preliminary_microscope_testing/2026-07-03_preliminary_microscope_testing.md`](../3JUL_preliminary_microscope_testing/2026-07-03_preliminary_microscope_testing.md), testing the stopcock-aperture imaging approach recommended in [`docs/EMBO_UAS_CV_Technical_Advisory.txt`](../../docs/EMBO_UAS_CV_Technical_Advisory.txt) §2 Problem B (moving the imaging site to the stopcock to physically constrain particles closer to a single layer, avoiding the overlap/stacking problem seen imaging through the barrel).

## Setup

Camera repositioned to image directly at the stopcock aperture rather than through the syringe barrel. Two illumination sources tested, each with video and photo recorded:

1. **Test 1 — smartphone backlight illumination** (continuous)
2. **Test 2 — microscope camera's built-in LED lights** (continuous)

No strobed/synced illumination was used in either test — both are continuous lighting.

| | |
|:---:|:---:|
| ![Stopcock aperture test — smartphone backlight](../../assets/STOPCOCK%20APERTURE%20TEST%20BACKLIGHT.jpg) | ![Stopcock aperture test — camera front LED](../../assets/STOPCOCK%20APERTURE%20TEST%20FRONTLIGHT.jpg) |
| *Test 1 — smartphone backlight* | *Test 2 — microscope camera's built-in LED (frontlight)* |

Both stills show the motion blur described below. Video for each test was also recorded but not yet added here — see [the note on embedding video in GitHub markdown](#video) if you want those playable inline.

## Observations

- **Overlap avoided.** Imaging at the stopcock aperture successfully constrains particles to (approximately) a single layer as they transit — confirms the advisory's Problem B fix works as intended for the overlap/stacking issue.
- **Significant motion blur.** Particle movement through the aperture is too fast for the camera to resolve cleanly under continuous lighting — both illumination sources produced blur. Flow velocity through the narrow stopcock bore is much higher than in the barrel, and the exposure time isn't short enough to freeze it.

## Assessment

This is the outcome the advisory predicted, not a new failure mode: §2 Problem B explicitly called out that "flow velocity through the narrow bore will be significantly higher than in the barrel; continuous lighting will produce motion blur," and required strobed, short-duration illumination synced to the camera trigger as part of this approach — not as an optional refinement. Both tests here used continuous lighting only, so the blur is expected, not evidence the stopcock approach has failed.

**This confirms strobed illumination is no longer a deferrable "nice to have" for the stopcock approach — it's required before this imaging site is usable at all.** See [`software/SOFTWARE_TODO.md`](../../software/SOFTWARE_TODO.md) task 12, which already flags the added risk from the camera being (most likely) rolling-shutter rather than the originally planned global-shutter sensor — that risk is now more directly relevant, since strobe timing has to be tight enough to freeze motion within a single row-readout window, not just short enough to avoid blur outright.

## Next steps

- Implement strobed/synced illumination (LED driven in short pulses, triggered relative to camera frame capture) before re-testing at the stopcock aperture
- Re-run this same two-illumination-source comparison once strobing is in place, to isolate whether continuous-vs-strobed is the fix, or whether the rolling-shutter risk also needs addressing
- **Considering a Raspberry Pi Global Shutter Camera** (or similar global-shutter module) instead of continuing with the current USB 2.0 microscope camera — not yet decided. Global shutter would eliminate the rolling-shutter row-skew contribution to the blur, and with a short manual exposure + strong illumination may not even require true strobe sync to freeze motion. Trade-off: this is a camera swap (mounting, working distance, USB vs CSI integration), not a firmware-only fix, and reverses the earlier decision to move away from the Global Shutter Camera — worth deciding deliberately rather than defaulting into it. See `software/cv-pipeline/README.md` and `docs/EMBO_Project_Overview.md` for what would need updating if this swap happens.

## Video

Stills above show the blur; matching video was also recorded for each test but isn't embedded here yet. GitHub only reliably renders an inline, playable video if it's uploaded through GitHub's own web UI (drag-and-drop into an Issue/PR/Discussion or the web file editor) — that generates the correct embed automatically. Committing a raw video file into `assets/` like the JPGs won't play inline the same way, and bloats repo history since git doesn't handle large binaries well (consider Git LFS if more test videos get added over time). Add the videos via GitHub's uploader and paste the resulting embed snippet in here when available.

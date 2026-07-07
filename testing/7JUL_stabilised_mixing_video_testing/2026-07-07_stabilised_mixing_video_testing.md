# Stabilised Mixing Video Testing — 7 July 2026

Follow-up test using a 3D-printed camera mount (rather than handheld positioning) and a red-filtered LED backlight, recording stabilised mixing video for both previously-tested materials.

## Setup

- **3D-printed microscope camera mount**, clamped directly onto the syringe/stopcock assembly — replaces handheld camera positioning from the earlier tests, intended to remove camera-shake as a contributor to blur (separate from the particle-flow motion blur identified in the stopcock aperture test)
- **USB LED panel with a red filter** for backlighting — first test of filtered (rather than plain white or smartphone-flash) illumination
- Camera (microscope USB camera, articulating clamp arm) aimed directly at the stopcock valve junction between the two syringes

![Microscope camera clamped onto syringe setup, 7 July 2026](../../assets/CLAMP%20MICROSCOPE%20CAMERA%20ON%20SYRINGE%207JUL2026.jpg)

*Setup photo: tester holding the syringe/stopcock assembly, with the microscope camera held on its articulating clamp mount pointed down at the stopcock valve junction, and the red-filtered LED panel providing backlighting from the table below.*

## Materials tested

Same two materials as the [3 July preliminary test](../3JUL_preliminary_microscope_testing/2026-07-03_preliminary_microscope_testing.md) — see that doc's caution regarding Lyostypt's validity as a test/validation material, which still applies here.

1. **Dental hemostatic foam**
2. **Lyostypt**

## Video

Stabilised mixing video recorded for each material, hosted on Google Drive (not committed to the repo — consistent with the earlier note on GitHub not reliably rendering repo-committed video inline, and avoiding git repo bloat from large binary files):

- Hemostatic sponge: https://drive.google.com/file/d/1DaJQElAKheHqdw3isQcZj6wH1w7w3FB_/view?usp=sharing
- Lyostypt: https://drive.google.com/file/d/13dB06tnO8pFV2qJgC2flB0C9g66608rP/view?usp=sharing

### Video screenshots

| | |
|:---:|:---:|
| ![Hemostatic sponge — 7 July video screenshot](../../assets/7JUL%20TEST%20SCREENSHOT%20HEMO.png) | ![Lyostypt — 7 July video screenshot](../../assets/7JUL%20TEST%20SCREENSHOT%20LYOSTYPT.png) |
| *Hemostatic sponge* | *Lyostypt* |

Both frames show the red-filtered illumination clearly (strong red/orange cast) and both are still visibly blurred, with no individual particle or fiber boundary resolved sharply enough to make out discrete structure — consistent with the persistent motion-blur finding below. The hemostatic sponge frame shows a faint dark ring shape that may be a bubble outline (consistent with the bubble-visibility pattern from the 3 July tests); the Lyostypt frame shows lighter, patchy brighter regions against the red background, but too blurred at this frame to confirm whether that reflects the fibrous clumping described in the earlier test.

## Observations

- **Red filter helps separate particles from bubbles.** Under red-filtered backlight, bubble outlines appear less sharp/prominent than under plain white light — this reduces the earlier problem (see the 3 July preliminary test) where bubbles were the dominant, clearly-visible feature that could be mistaken for or could obscure actual particles. Note this is a bubble-vs-particle discrimination improvement specifically, not necessarily a full fix for particle-vs-saline contrast (the original index-matching invisibility problem, advisory §2 Problem A) — that still needs separate confirmation.
- **Mount achieved its goal.** The 3D-printed mount successfully produced clear, stable video — camera-shake is confirmed no longer a contributing factor to blur.
- **Particle motion blur persists regardless.** Even with a rigidly mounted, shake-free camera, particle movement during mixing still produces motion blur. This cleanly isolates the two possible blur sources identified after the stopcock aperture test: camera-shake (now ruled out — fixed by this mount) and particle flow velocity (confirmed as the remaining cause).
- **Confirmed imaging location: stopcock aperture** — same imaging site as the [3 July stopcock aperture test](../3JUL_stopcock_aperture_testing/2026-07-03_stopcock_aperture_testing.md), not the barrel.

## Assessment

This test isolates the blur problem cleanly: with camera-shake eliminated by the mount, the remaining blur is attributable entirely to particle flow velocity through the stopcock aperture — exactly the failure mode the advisory predicted and the [3 July stopcock test](../3JUL_stopcock_aperture_testing/2026-07-03_stopcock_aperture_testing.md) already confirmed under continuous lighting. This test doesn't change that conclusion, but it does rule out camera stability as a confounding factor, so the strobed-illumination requirement (`software/SOFTWARE_TODO.md` task 12) stands as the remaining fix, not "get a steadier mount."

The red filter's bubble/particle separation benefit is a useful, independent finding worth carrying into the Layer 2 optical work in `software/SOFTWARE_TODO.md` — it may be worth testing red-filtered light in combination with the planned dye test, rather than treating dye and filtered light as mutually exclusive options.

## Open questions

- Does the red filter also help with particle-vs-saline contrast specifically (not just particle-vs-bubble), or only with bubble suppression? Needs a direct comparison against the dye test to separate these two effects.

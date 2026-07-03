# Preliminary Microscope Testing — 3 July 2026

Initial hands-on test of the USB 2.0 microscope camera + smartphone-flash backlighting setup against two test materials, run through the standard manual mixing procedure. This test is the origin of [`docs/EMBO_UAS_CV_Technical_Advisory.txt`](../../docs/EMBO_UAS_CV_Technical_Advisory.txt) — the UAS/CV findings and required changes documented there were identified here.

## Setup

- Off-the-shelf USB 2.0 microscope camera, pointed at the syringe barrel
- Backlighting: smartphone flash (not the LED panel — this was a bench test, not on the assembled PCB)
- Standard manual mixing procedure (syringe-to-syringe pumping) applied to both materials before imaging

| | |
|:---:|:---:|
| ![Preliminary microscope testing setup 1](../../assets/PRELIMINARY%20MICROSCOPE%20TESTING%201%203JUL26.jpg) | ![Preliminary microscope testing setup 2](../../assets/PRELIMINARY%20MICROSCOPE%20TESTING%202%203JUL26.jpg) |
| *Test setup 1* | *Test setup 2* |

## Materials tested

### 1. Lyostypt

Bovine collagen, **not gelatin** — not a valid Gelfoam-equivalent stand-in (see advisory §3).

- Upon mixing, particles did get shredded and decreased in size
- Stringy fibers clump together, appearing as large "cotton ball"-like clusters under the microscope

![Lyostypt post-mixing, microscope view](../../assets/LYOSTYPT%20POST%20MIXING.jpg)

### 2. Dental hemostatic foam

Not a gelatin equivalent.

- Did not reach a pudding-like consistency after mixing
- Sponge did not decrease in size from shredding through the syringe aperture, compared to its as-cut size

![Dental hemostatic foam post-mixing, microscope view](../../assets/HEMOSTATIC%20SPONGE%20POST%20MIXING.jpg)

## Observations

- **Particle outline was difficult to observe** due to colour similarity between the particles and the surrounding saline — this is the direct precursor observation behind the advisory's Problem A (near index-matched hydrated gelatin/collagen vs. saline, near-invisible under plain backlighting) and the recommended dye/dark-field/cross-polarization fixes.
- **High mixing torque, especially for the hemostatic sponge** — the initial push in mixing was significantly hard. Flags a real torque requirement for the motorized mixing system that should feed into motor/driver sizing and current (`irun`) tuning, not just the optical/CV findings.

## Outcome

These findings led directly to [`docs/EMBO_UAS_CV_Technical_Advisory.txt`](../../docs/EMBO_UAS_CV_Technical_Advisory.txt), which lists the required UAS and CV pipeline changes in priority order. See [`software/SOFTWARE_TODO.md`](../../software/SOFTWARE_TODO.md) for how those changes are being tracked and built.

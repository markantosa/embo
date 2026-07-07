# EMBO CV Pipeline Build-Up TODO

Generated from: `software/cv-pipeline/README.md`, `docs/EMBO_UAS_CV_Technical_Advisory.txt` (July 2026), the firmware UART protocol (`firmware/esp32/src/rpi_uart.cpp`), and the particle-size kinetics model in `docs/EMBO_Project_Overview.md`.

Scope: the Raspberry Pi computer-vision pipeline only. TFT touchscreen UI tasks live in `firmware/FIRMWARE_TODO.md` (tasks 7–10) since that code runs on the ESP32, not the Pi.

Code scaffolding exists (`config.py`, `capture.py`, `uart_link.py`, `detection.py`, `sizing.py`, `main.py`) — Layer 1 (camera + UART) is written but **not yet run on real hardware**, so its checkboxes track implementation, not verification. Layers 2–3 (`detection.py`/`sizing.py`) are deliberately unimplemented stubs that raise `NotImplementedError` rather than fake logic — see the module docstrings.

---

## Layer 1 — Camera + UART bring-up

> Nothing else works until these are done.

### 1. Camera capture `capture.py` ⚠️ written, unverified
- [x] `Camera` class wraps `cv2.VideoCapture`/`v4l2` (not `picamera2`/`libcamera` — camera changed to an off-the-shelf USB 2.0 UVC microscope camera, no CSI involved)
- [x] Frame size/FPS and manual-exposure/autofocus-off requested via `cv2.VideoCapture.set()` — driver support for these varies a lot per UVC camera and is **not yet confirmed to take effect** on the real hardware
- [ ] Check the camera's actual supported resolution/fps combinations (`v4l2-ctl --list-formats-ext`) — USB 2.0 bandwidth (~35–40MB/s effective) caps what's deliverable; don't assume `config.py`'s `FRAME_WIDTH`/`FRAME_HEIGHT`/`CAMERA_FPS` defaults (1280×720@30) are actually achievable until checked
- [ ] Verify stable frame capture at a fixed resolution/exposure with the LED backlight panel powered (confirm the 5V/J8 rail is live — the panel is powered from the main PCB, not GPIO-controlled from the Pi)
- [ ] Confirm focus/working distance on the actual mechanical mount — microscope cameras typically have a short fixed working distance, so the mount planned for the CSI camera may not fit
- [ ] **GATE:** don't judge "camera working" using particle visibility — current backlighting makes hydrated gelatin nearly invisible (see Layer 2). Use an opaque test target or air bubbles to confirm optical alignment first; revisit particle visibility only after Layer 2.

### 2. UART link to ESP32 `uart_link.py` ⚠️ written, unverified
- [x] `FirmwareLink` opens the serial port at 921600 baud via `pyserial`, with `send_size(median_um, iqr_um)` writing `"SIZE <median_um> <iqr_um>\n"` and `send_status(message)` for free-form diagnostics — matches firmware's `sscanf(_buf, "SIZE %d %d")` parser exactly (positive median, non-negative IQR enforced client-side too, raising `ValueError` before a bad packet is even sent)
- [ ] Confirm `config.UART_PORT` (`/dev/serial0`) actually maps to RPi GPIO14/15 on the **Raspberry Pi 5** specifically — Pi 5 uses the RP1 I/O chip, whose UART mapping differs from earlier Pi models this default may have been copied from
- [ ] **Integration test:** confirm via the firmware's BLE log (`"RPi: median=X iqr=Y um"`), not a local print statement — that's the only proof the packet actually parsed correctly on the other end
- [ ] Confirm arbitrary status/error strings sent from the RPi (e.g. `"CAM: init OK"`, `main.py`'s `"CV: detection/sizing not implemented yet"`) are safely ignored by firmware rather than causing a parse error — firmware is designed to discard unmatched lines silently, so this should already be safe, but verify it in practice

---

## Layer 2 — Optical contrast setup

> Per the July 2026 advisory §2 Problem A: hydrated gelatin/collagen is ~80–99% water and near index-matched to saline, so plain backlighting shows bubbles and glare, not particles. Test the cheapest fix first.

### 3. Dye contrast test — highest priority, no hardware change
- [ ] Add visible dye (methylene blue or similar) to the saline; re-run capture; confirm particle contrast visibly improves over plain backlight
- [ ] Save before/after image pairs as the baseline evidence
- [ ] Note dye concentration used, and flag if the dye visibly affects particle buoyancy/behavior during mixing
- [ ] **Test combined with the red-filtered LED backlight** (see below) rather than treating them as alternatives — 7 July test found red filtering helps bubble/particle separation, which is a different effect from dye's particle/saline contrast boost; the two may be complementary

### 3b. Red-filtered LED backlight — confirmed by test (7 July 2026), not yet in the advisory
- [x] **CONFIRMED BY TEST** — red filter on the LED backlight makes bubble outlines less sharp/prominent relative to plain white light, improving particle-vs-bubble discrimination. See `testing/7JUL_stabilised_mixing_video_testing/2026-07-07_stabilised_mixing_video_testing.md`.
- [ ] **Not yet confirmed:** whether the red filter also helps the core particle-vs-saline contrast problem (advisory §2 Problem A), or only bubble suppression — these are two different effects and shouldn't be conflated. Needs a direct side-by-side comparison against plain light and against the dye test.
- [ ] If confirmed as genuinely complementary to dye, add to the standard test lighting configuration rather than treating as a one-off

### 4. Dark-field illumination trial
- [ ] Angle the LED panel obliquely instead of straight backlight (mechanical adjustment — coordinate with the mechanical team on panel mounting)
- [ ] Confirm this suppresses the bulk-transmission near-invisibility problem; test combined with dye, not as a replacement for it

### 5. Cross-polarization trial
- [ ] Add polarizing film to the LED panel + a crossed polarizer on the camera lens
- [ ] Expect glare/bubble-highlight suppression; per the advisory this benefits Lyostypt (birefringent intact collagen) far more than true denatured gelatin sponge, which loses birefringence — treat as a glare-suppression tool for gelatin, not a contrast source
- [ ] Test in combination with dye + dark-field

### 6. Cylindrical-barrel lensing/glare
- [ ] Track separately from the above — this is the same class of fix as the stopcock imaging window in Layer 5 (task 12); don't duplicate effort building two separate optical fixtures

---

## Layer 3 — Detection & sizing model

> Do not resume this layer until Layer 2 confirms particles are actually visible in captured frames — training/tuning a model against near-invisible particles is wasted effort.

### 7. Switch to instance segmentation `detection.py` — stub only
- [x] `ParticleDetector` class scaffolded — `detect()` deliberately raises `NotImplementedError` rather than a fake/placeholder result, so a broken pipeline can't silently look like it works
- [ ] Evaluate YOLOv8-seg vs Cellpose (purpose-built for dense/touching irregular blobs) — bounding boxes cannot resolve overlapping/stacked particles, per-pixel instance masks can (advisory §2 Problem B)
- [ ] Re-annotate training data with segmentation masks, not just bounding boxes — this is a real data-prep cost, budget time for it
- [ ] Retrain using images captured under the finalized Layer 2 optical setup — earlier bounding-box-era training images are not valid once dye/lighting changes the input distribution
- [ ] Implement `ParticleDetector.detect()` for real once the above are done

### 8. Sizing metric — Equivalent Circular Diameter (ECD) `sizing.py` — stub only
- [x] `compute_ecd_stats()` scaffolded with the `SizeStats` return shape — also raises `NotImplementedError` rather than a fake formula, since a wrong sizing calculation would silently poison the firmware PID target check
- [ ] Adopt ECD (diameter of a circle with equal projected area) as the primary size metric per ISO 13322 / ISO 9276-6 — gelatin/collagen fragments are irregular "snowflake"-like, not circular, so a naive diameter isn't well-defined (advisory §2 Problem C)
- [ ] Compute median/IQR over the ECD distribution, replacing any bounding-box-diagonal-based sizing
- [ ] Also compute and log max/min Feret diameter and solidity (area / convex hull area) — secondary signals for "how irregular/branching" the population still is, useful for tracking mixing progress even before they're sent to firmware
- [ ] Calibrate `um_per_pixel` against a known reference (e.g. glass beads of known diameter) — `main.py` currently passes a placeholder `1.0`

---

## Layer 4 — Protocol integration with firmware

### 9. UART packet — ECD median/IQR `uart_link.py`
- [x] Wire format implemented (`FirmwareLink.send_size()`) — sending real ECD-based `median_um`/`iqr_um` is blocked on task 8, not on the transport, which is already done
- [ ] Send ECD-based `median_um`/`iqr_um` using the existing `"SIZE <median_um> <iqr_um>\n"` format — no firmware change needed for this part alone
- [ ] **Coordinate with firmware before adding Feret/solidity fields.** Firmware's `sscanf` parser in `rpi_uart.cpp` silently ignores extra trailing fields rather than erroring — new fields would arrive over UART but never reach firmware logic until `rpi_uart.cpp`/`rpi_uart.h` are explicitly extended (already flagged in `FIRMWARE_TODO.md`). Don't assume sending more data "just works."
- [ ] If extending the protocol, agree an explicit versioning approach with the firmware owner (e.g. a distinct message type) rather than letting the two sides drift out of sync silently

---

## Layer 5 — Validation & materials

### 10. Empirical UAS-vs-CV correlation logging (joint task with firmware)
- [ ] At several stroke counts, simultaneously capture CV-measured true median/IQR and the firmware's BLE `UAS ON` stream (per-frequency attenuation) — see `firmware/FIRMWARE_TODO.md` task 5, which is **BLOCKING** on the firmware side until this is done
- [ ] Sequence this after Layers 2–3 are working — CV output has to be trustworthy before a correlation check against it means anything

### 11. Ground-truth test material sourcing
- [ ] Source verified pharmaceutical-grade absorbable gelatin sponge (e.g. Surgispon) or Chinese-pharmacopoeia-grade 吸收性明胶海绵 for valid ground-truth testing/training material
- [ ] **Do not use Lyostypt as a Gelfoam stand-in** — it's bovine collagen, structurally different, which explains its non-gelling stringy behavior in earlier tests (advisory §3)
- [ ] If using a DIY gelatin foam (freeze-dried foamed gelatin solution, dehydrothermally cross-linked at 100–160°C for 24–72h, no chemical crosslinker) for day-to-day iteration: cross-validate its fragmentation behavior against real purchased sponge before trusting absolute size numbers calibrated on it — cross-link density affects fragmentation kinetics, so DIY and real material may not fragment the same way under identical mixing

### 12. Stopcock optical window + strobed illumination — stretch goal, highest effort
- [x] Overlap avoided — **confirmed by test**, not just theory. Camera repositioned to the stopcock aperture (see `testing/3JUL_stopcock_aperture_testing/`); particles are constrained close enough to a single layer that the barrel-imaging overlap problem (§2 Problem B) is resolved at this imaging site.
- [ ] Scope early if pursuing — requires custom part fabrication (clear stopcock insert or modified bore); coordinate with the mechanical team well ahead of the exhibition deadline
- [x] **CONFIRMED BY TEST (3 July 2026) — motion blur under continuous lighting.** Two continuous-illumination sources tested (smartphone backlight, camera's built-in LED) — both produced significant motion blur from particle flow velocity through the aperture. This is the exact failure mode the advisory predicted for continuous lighting at this imaging site, now observed directly rather than theoretical. See `testing/3JUL_stopcock_aperture_testing/2026-07-03_stopcock_aperture_testing.md`.
- [ ] **No longer deferrable.** Strobed, short-duration illumination synced to the camera trigger is required before this imaging site is usable at all — not an optional refinement layered on top of a working continuous-light setup, since continuous lighting has now been tested and confirmed unusable here.
- [ ] **UPDATE — higher risk now than originally scoped.** The original plan assumed a global-shutter sensor, where strobing mainly compensates for flow velocity. The USB microscope camera now in use is very likely **rolling shutter**, which adds frame skew/distortion on top of blur for anything moving during readout — strobe timing and exposure duration need to be tight enough to freeze motion within a single row-readout window, not just "short enough to avoid blur." Once strobing is implemented, re-test to isolate whether continuous-vs-strobed was the whole problem, or whether rolling-shutter skew is a separate remaining issue.
- [ ] **Go/no-go check first:** verify bubble/cavitation formation risk at the constriction before committing to the full optical build — pressure drop can nucleate bubbles, and this is also the point of maximum shear, so bubble noise may be worse here, not better

---

## Layer 6 — Exhibition readiness

### 13. Model packaging & runtime robustness
- [ ] Package trained weights + inference code for the actual demo Raspberry Pi — weights are gitignored (`weights/`), so confirm the demo unit has the current trained weights, not a stale local copy
- [ ] Handle camera/UART disconnects and bad frames gracefully — keep sending status strings (which firmware safely ignores) rather than crashing the process, matching firmware's assumption that "RPi can send status strings freely"
- [ ] Confirm inference frame rate/latency is fast enough not to starve the ESP32 PID loop of fresh median/IQR updates during a live run

### 14. Live demo rehearsal
- [ ] Full end-to-end run-through on real (or cross-validated DIY) gelatin foam: dye + lighting → segmentation → ECD sizing → UART → firmware PID stop condition
- [ ] Deliberately exercise the edge cases in front of a test audience first — bad frame, UART hiccup, low model confidence — don't let the first occurrence be at the actual exhibition

---

## Verification gates

Complete these checks in order before closing each layer.

| After task | What to verify |
|---|---|
| 1 | Camera captures stable frames; UART link accepted by firmware — BLE log shows `"RPi: median=X iqr=Y um"` |
| 3 | Dye visibly improves particle contrast vs plain backlight in saved before/after images |
| 5 | Dark-field/cross-pol combo suppresses glare/bubble highlights without erasing the dye contrast gained in task 3 |
| 7 | Instance segmentation resolves particles that bounding-box detection merged or missed, on the same test image set |
| 8 | ECD/Feret/solidity outputs are sane on a known reference (e.g. glass beads of known diameter) before trusting them on real gelatin |
| 10 | UAS-vs-CV correlation is either confirmed clean and monotonic (promote UAS beyond diagnostic-only) or confirmed weak (leave it diagnostic-only) — not left undecided |
| 12 | *(if pursued)* No unexpected bubble/cavitation increase at the stopcock constriction, checked before fabricating the optical window |
| 14 | Full end-to-end run completes without manual intervention on real or cross-validated gelatin foam |

---

## Materials & coordination checklist

### Before optical changes
- [ ] Confirm dye choice (methylene blue or alternative) doesn't interfere with downstream chemistry/injection use of the slurry — this is a test rig, but worth a sanity check given the material is nominally a medical embolic agent
- [ ] LED panel angle/mount changes for dark-field trial — coordinate with mechanical team on bracket/mount rework, don't just tape it at an angle for the demo

### Protocol coordination
- [ ] Confirm with firmware owner whether/when `rpi_uart.cpp` gets extended to accept Feret/solidity fields, or whether those stay CV-side-only diagnostics for now
- [ ] Confirm UART baud/pin assignment hasn't changed on a PCB rev before assuming `GPIO47/48` ↔ `GPIO14/15` still applies

### Materials sourcing
- [ ] Surgispon (or equivalent pharma-grade sponge) ordered with enough lead time for both CV training data and mechanical/electrical integration testing
- [ ] DIY gelatin foam recipe (if used) documented with exact cross-link time/temperature per batch, so size calibration drift can be traced back to a specific batch

---

## Notes

- **UART protocol is intentionally minimal.** `"SIZE <median_um> <iqr_um>\n"` is the only format firmware currently parses. Anything else sent by the RPi is silently discarded — useful for free-form status logging, but means new metrics need an explicit protocol decision, not just "send more data."
- **Advisory's own priority order** (given limited time before exhibition, `docs/EMBO_UAS_CV_Technical_Advisory.txt` §4): 1) dye test, 2) empirical UAS-vs-CV correlation check, 3) dark-field/cross-polarization lighting trial, 4) switch to instance segmentation, 5) stopcock optical window + strobed illumination. This TODO's layering follows that same priority, just grouped by build-up dependency rather than a flat list.
- **Don't retrain before Layer 2 is settled.** Every optical change (dye, lighting angle, polarizers) changes what the camera actually sees — training data collected before the optical setup is finalized will need to be recollected, not reused.
- **Cross-reference `firmware/FIRMWARE_TODO.md`** for anything that touches the ESP32 side: UART parsing (task 4), the UAS-vs-CV correlation gate (task 5), and the TFT UI that displays this pipeline's output (tasks 7–10).

# CV Verify — TODO (on-demand, out-of-loop CV check, Pi Zero 2W)

This is a **new, separate workspace** — nothing here touches `software/cv-pipeline/`
(that's your friend's Layer-1-through-3 build-up, left as-is). This folder picks up
from the current firmware reality and rebuilds only what changed.

## Why this doc exists

`../SOFTWARE_TODO.md` was written when CV was meant to be a **continuous stream**
feeding the ESP32's control loop, on a **Pi 5**. Neither is true anymore:

- `firmware/esp32/include/rpi_uart.h:5-8` and `FIRMWARE_TODO.md` task 6 — the
  scheduler now closes the loop on four other sensors (UAS + 2× turbidity + force).
  CV is **optional, on-demand, single-shot** — the ESP32 sends `"CAPTURE\n"` and
  expects one `"SIZE <median_um> <iqr_um>\n"` reply, not a repeating stream.
- We're moving to a **Pi Zero 2W**, not a Pi 5. Weaker CPU (quad A53 @ 1GHz vs
  the 5's Cortex-A76), different camera connector, and — usefully — the RP1-chip
  UART-mapping uncertainty flagged in the old TODO (task 2) mostly goes away since
  the Zero 2W behaves like the well-documented Pi 3/4 UART story, not the 5's RP1 I/O chip.

So the old TODO's Layer 1 (continuous capture loop, streaming UART) needs replacing,
not just extending. Layers 2–3 (optical contrast, detection/sizing) are still valid —
those don't care whether the trigger is a loop or a command.

---

## Layer 0 — Pi Zero 2W bring-up

Different from `RPI_SETUP_GUIDE.md` in a few places worth calling out, not just
"same steps, different board":

- [ ] Flash Raspberry Pi OS **Lite** (64-bit) — no desktop needed, and the Zero 2W
  has less headroom than a 5 for a GUI sitting in the background eating RAM (512MB
  total).
- [ ] **Camera connector:** Zero 2W uses the small MIPI CSI connector, not the
  standard one on a Pi 5/4. The Global Shutter Camera's ribbon cable needs a
  Zero-format adapter cable (sold separately) — confirm you have one before wiring
  anything else.
- [ ] **UART:** same fix as the old guide (§7) — `dtoverlay=disable-bt` in
  `/boot/firmware/config.txt` + `sudo systemctl disable hciuart`, so GPIO14/15 gets
  the real PL011 UART instead of the mini-UART shared with Bluetooth. This is the
  standard Pi 3/Zero 2W story (RP1-specific uncertainty in the old task 2 was a
  Pi-5-only problem).
- [ ] Confirm `/dev/serial0` exists and maps to GPIO14/15 after the above + reboot.
- [ ] `sudo usermod -aG video,dialout $USER`, log out/in.

## Layer 1 — Request/reply UART link (replaces old task 2 + main.py loop)

The whole shape of `main.py` changes: no more `while True: capture, try detect,
sleep(0.1)` free-running loop. Instead, **block waiting for a line from the ESP32**,
and only capture+run CV when `"CAPTURE\n"` arrives.

- [ ] New `link.py` in this folder: open `/dev/serial0` at 921600 baud, blocking
  (or short-poll) read for a line, filter for exactly `"CAPTURE"`.
- [ ] On `CAPTURE`: grab one frame, run detection+sizing, send
  `"SIZE <median_um> <iqr_um>\n"` — **must reuse the exact wire format** your
  friend's `uart_link.py` already used (`sscanf(_buf, "SIZE %d %d")` on the
  firmware side is fixed and shared — don't invent a new format here).
- [ ] **Hard budget: `RPI_CAPTURE_TIMEOUT_MS = 8000`** (`firmware/esp32/include/config.h:157`).
  If capture + inference doesn't reply inside 8s, the ESP32 marks it
  `TIMED_OUT` and moves on. This is the one place Pi Zero 2W's weaker CPU is a real
  risk vs a Pi 5 — budget time explicitly:
  - [ ] Measure raw capture time alone first (no model) — establish the floor.
  - [ ] Once a real detection model exists (Layer 3, still blocked on optics),
    measure YOLOv8-seg/Cellpose inference time on the Zero 2W specifically. If it
    blows the 8s budget, options are: a lighter model, lower input resolution, or
    (last resort) asking firmware to raise `RPI_CAPTURE_TIMEOUT_MS` — coordinate
    before changing firmware.
- [ ] While waiting for `CAPTURE`, it's fine (and expected) to send free-form
  `send_status()` lines — firmware silently discards anything that isn't a valid
  `SIZE` line, same guarantee your friend's version relied on.
- [ ] No more periodic streaming — being idle between `CAPTURE` requests is the
  correct resting state, not a bug.

## Layer 2 — Optical contrast setup

**Reprioritized from `../SOFTWARE_TODO.md` tasks 3–6 — dye is off the table.**
Camera confirmed as **OV9281-110** (global shutter mono, no IR-cut filter,
sensitive out to ~900–1000nm) — consistent with the earlier UVC→global-shutter
pivot, and mono doesn't invalidate the red-filter result (see below).

- [ ] **Dye contrast test — REMOVED, not just deprioritized.** This device operates
  on real material in active medical use (embolization slurry); adding a chemical
  contrast agent to the material isn't an option outside a bench-only test rig.
  This was the advisory's #1 recommendation and the only fix that addresses the
  *fundamental* problem (gelatin ~index-matched to saline) — removing it means
  contrast has to come entirely from illumination geometry instead, and results
  may end up weaker than the earlier dye-assisted petri-dish tests. Flag this
  honestly rather than assuming dark-field alone will fully recover what dye gave up.
- [ ] **Dark-field / oblique illumination — now highest priority (was task 4).**
  Angle the LED panel obliquely instead of straight backlight. This is the standard
  technique for imaging transparent phase objects *without* a stain — unscattered
  light misses the lens, light scattered by a particle enters it, so particles
  read bright against a dark background. Unlike red-filtering/plain backlight
  (which assumed dye handled the core contrast problem), this is built for
  exactly the no-dye case. Mechanical coordination needed for the panel mount
  (per old task 4).
- [ ] **Cross-polarization — second priority (was task 5), pair with dark-field,
  not standalone.** Polarizing film on the LED panel + a crossed polarizer on the
  camera lens (you already have a polarizer). Good for suppressing syringe-wall
  glare/specular reflection on top of dark-field; per the advisory it benefits
  birefringent material more than true denatured gelatin, so don't expect it to
  fix bulk transparency alone.
- [ ] **Red-filtered backlight — still valid, refinement layer (was task 3b).**
  7 July result (bubble/particle discrimination improved) is **not invalidated
  by the mono sensor** — the effect came from restricting illumination
  wavelength (less chromatic scatter/glare reaching the lens), not from Bayer
  color separation, so a mono sensor benefits identically.
- [ ] **NIR illumination — stretch/secondary experiment, not priority-1.** OV9281's
  lack of an IR-cut filter means NIR (~850–950nm) illumination is a legitimate
  extra contrast lever — water/saline absorption differs from visible in some NIR
  bands — without adding anything to the slurry. Needs an NIR LED; only worth
  pursuing once dark-field's baseline result is known, not before.
- [ ] **Fallback if dark-field + polarization still isn't enough:** don't keep
  iterating on lighting indefinitely — reconsider the imaging site instead (the
  stopcock aperture work, old task 12, where particles are constrained to a thin
  layer and scatter differently), or accept CV as a lower-confidence verification
  signal. This is survivable specifically because CV is optional/out-of-loop now
  (Layer 4) — a weak verification signal doesn't stall a mixing run the way it
  would have under the old continuous-fusion design.
- [ ] Cylindrical-barrel lensing/glare (old task 6) — still track together with
  the stopcock imaging-window fallback above, not as a separate optical fixture.

This problem doesn't care whether CV is continuous or on-demand — the reprioritization
above is driven by the no-dye constraint and camera choice, not the trigger model.

## Layer 3 — Detection & sizing model

**Unchanged from `../SOFTWARE_TODO.md` tasks 7–8** — same optics-gate, same
segmentation-vs-bounding-box reasoning, same ECD sizing metric. On-demand vs
continuous doesn't change *what* the model needs to do, only *when* it's
invoked and how forgiving the latency budget is (see Layer 1 above — on-demand
is actually more forgiving, since there's no real-time feed to starve).

## Layer 4 — Verification-specific integration

- [ ] Confirm the ESP32-side trigger path works end-to-end: UI encoder long-press
  or BLE `CAPTURE` command → `rpi_request_capture()` → your Pi replies → BLE log
  shows `RPi: median=X iqr=Y um` (`firmware/esp32/src/rpi_uart.cpp:36`) → UI
  verification screen shows the result (`firmware/esp32/src/ui.cpp`, per
  `FIRMWARE_TODO.md` task 7).
- [ ] Log every verification result locally on the Pi too (timestamp, stroke count
  if you can get it over BLE, median/IQR) — useful for the diagnostic breakage-fit
  cross-check firmware already supports (`calib_breakage_add_point()`,
  `FIRMWARE_TODO.md` task 6) and for your own sanity-checking against the 9-syringe
  fusion calibration.
- [ ] This is explicitly **not** a fusion input anymore (`FIRMWARE_TODO.md`
  task 6) — don't build anything here that assumes the ESP32 is waiting on you to
  keep a control loop alive. Worst case for a slow/failed capture is a timeout
  notice on the verification screen, not a stalled mixing run.

## Verification gate

| After | What to verify |
|---|---|
| Layer 0 | `/dev/serial0` exists, camera visible via `libcamera-hello` |
| Layer 1 | Send `CAPTURE` manually from a terminal/BLE, confirm a `SIZE` reply appears within 8s and firmware's BLE log parses it |
| Layer 3 | Once optics + model are ready, a real `CAPTURE` round-trip returns a sane median/IQR on known material |

## Notes

- Don't edit `../cv-pipeline/` — that's your friend's checkpoint of the old
  continuous-loop approach, kept for reference/reuse of `detection.py`/`sizing.py`
  once Layer 2/3 unblock (those two files' logic is orientation-agnostic — loop vs
  on-demand doesn't change what's inside them, so they're worth importing here
  rather than rewriting from scratch once they're implemented).
- `../RPI_SETUP_GUIDE.md` is still ~90% correct for a Zero 2W (SSH, VS Code
  Remote-SSH, venv setup are all identical) — only §7's camera-connector and
  UART-quirk details differ, both called out in Layer 0 above.

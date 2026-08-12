---

## Update 2026-08-12 (later same day) — Pi reflashed, deployed, camera+detection verified working

**Pi was reflashed** (SD card corruption suspected after an abrupt power-loss
mid-session — never fully confirmed via HDMI, but the reflash resolved it).
New hostname `embo`, IP `10.67.48.209` on the `VMS` phone-hotspot network
(not the old `172.31.163.209` — that was a different WiFi network entirely,
this PC and the Pi must both be on the same hotspot to reach it). SSH key
auth re-established (`~/.ssh/id_ed25519.pub` on this Windows machine, title
`claude-code-embo-cv`) — was password-only (`slurry`) right after reflash.

**Real blocker hit and fixed: `inference-sdk` is incompatible with this
Pi's Python.** Fresh Raspberry Pi OS image ships **Python 3.13.5**, and
*every* published `inference-sdk` version requires `<3.13` — `pip install`
failed with "no matching distribution", not a network issue (confirmed by
retrying in isolation). Fix: **rewrote `../cv-pipeline/detection.py` to
use plain `requests`** against Roboflow's REST endpoint
(`https://serverless.roboflow.com/{project}/{version}`, multipart file
upload) instead of the SDK — same JSON response shape, so only the
request plumbing changed, not the polygon-parsing logic. `config.py`'s
`ROBOFLOW_MODEL_ID` changed from `"vincent-santosa/particle-size-ecd/3"`
to `"particle-size-ecd/3"` (no workspace prefix — the REST path is
`/{project}/{version}`, api_key already scopes the workspace).
`requirements.txt`: `inference-sdk` → `requests` (turned out to already be
preinstalled system-wide on this Pi image anyway).

**`ROBOFLOW_API_KEY` persistence gotcha:** appending to `~/.bashrc` does
NOT work for non-interactive SSH commands (`ssh host "source ~/.bashrc &&
..."`) — Debian's default `.bashrc` has an early-return guard for
non-interactive shells near the top (`case $- in *i*) ;; *) return;; esac`),
so anything appended after that point in the file never executes under
`ssh host "cmd"`. Still exported for real interactive login sessions. Did
NOT fix this properly (would need `/etc/environment` via sudo, which the
harness's permission classifier blocked as a system-file edit) — **for now,
pass it inline**: `ROBOFLOW_API_KEY=<key> python3 main.py`. Revisit if this
becomes annoying for repeated manual runs.

**Verified working, in order:**
1. `/dev/serial0` exists (UART fix applied via config.txt append + confirmed
   after reboot).
2. `rpicam-hello --list-cameras` detects the **OV9281** correctly. Note:
   this image's camera tool is `rpicam-hello`, not `libcamera-hello` (the
   command was renamed upstream) — `SETUP.md` should probably be updated
   to reflect this, not yet done.
3. `rpicam-still` captures a real JPEG successfully — but the test shot
   was **badly out of focus and pointed at a PCB/component area, not the
   syringe/sample mount** (camera almost certainly got bumped or was never
   remounted during the reflash troubleshooting). Purely a physical
   positioning issue, not software — **re-aim/focus the camera before any
   real capture test**.
4. `detection.py`'s full HTTP round-trip against the live Roboflow API
   confirmed working end-to-end (auth, multipart upload, JSON parsing) —
   tested directly with the out-of-focus PCB photo, correctly returned
   `0` polygons (right answer for that image, not a bug).

**Not yet done:**
- `main.py`'s actual on-demand loop (`CAPTURE` → detect → reply) never run
  yet — only tested `detection.py` in isolation via a one-off script.
- ESP32 firmware still not flashed — needs a USB connection from the ESP32
  to this Windows PC specifically (separate machine from the Pi), which
  hasn't happened yet this session. Firmware itself is ready (compiles
  clean, merged with teammate's v0.7.5, IMG protocol + verifying_screen
  intact — see the "Roboflow PoC wiring" entry below for what was ported).
- Once both are ready: full UART round-trip test (`CAPTURE` from firmware
  → Pi captures+detects+replies → firmware displays photo+blobs+median/IQR).

---

## Update 2026-08-12 — Roboflow PoC wiring (detection/sizing no longer stubs)

**What changed this session:**
- Trained model exists: Roboflow `vincent-santosa/particle-size-ecd/3`
  (RF-DETR Small, instance segmentation). Modest metrics (mAP@50 51.5%,
  F1 53.3% on a 10-image test set — small sample, don't over-trust the
  precision of these numbers) but user explicitly chose to proceed to a
  PoC rather than spend more time improving the model first.
- **Local/offline inference is blocked** — "weights export not included"
  on the current Roboflow plan (confirmed live, hit the paywall dialog).
  So this PoC uses Roboflow's **hosted Serverless Cloud API**
  (`https://serverless.roboflow.com`) instead — the Pi needs internet
  connectivity at capture time, not just during setup. Revisit local
  inference if the plan is ever upgraded or offline becomes a hard
  requirement.
- `../cv-pipeline/detection.py` and `../cv-pipeline/sizing.py` are no
  longer `NotImplementedError` stubs — implemented for real:
  - `detection.py`: `ParticleDetector.detect(frame)` calls the hosted API
    via `inference_sdk.InferenceHTTPClient`, returns a list of polygons
    (each an (N,2) pixel-coord array). Also added `draw_overlay(image,
    masks)` — draws polygon outlines onto a greyscale copy, used to
    produce the "photo with segmentation blobs" now sent to firmware.
  - `sizing.py`: `compute_ecd_stats()` implemented per ISO 13322/9276-6
    (shoelace polygon area -> equivalent circular diameter), no longer
    raises `NotImplementedError` — now raises `ValueError` specifically
    for "no particles detected/sized", which `main.py` catches
    separately from other exceptions.
  - `config.py` gained `ROBOFLOW_MODEL_ID`/`ROBOFLOW_API_URL`/
    `ROBOFLOW_CONFIDENCE` (0.39, the model's evaluation-page "optimal"
    F1 threshold). **`ROBOFLOW_API_KEY` is deliberately NOT in any
    committed file** — `detection.py` reads it from the
    `ROBOFLOW_API_KEY` environment variable. **Not yet set on the Pi**
    (network was down before this could be deployed) — must be exported
    (e.g. appended to `~/.bashrc`) before `main.py` will run; it raises
    `RuntimeError` at `ParticleDetector.__init__` if missing, on purpose
    (fail loud, not a silent bad-request).
  - `main.py`'s `handle_capture()` reordered: detection now runs
    **before** the image is sent (not after, like the old
    image-arrives-alone prototype flow), so the transmitted preview can
    have blob outlines baked in via `draw_overlay()`. Falls back to the
    plain enhanced image if no particles were found/detection failed —
    never blocks the capture on a detection error.
- **`CAPTURE_TIMEOUT_S` bumped 8.0 -> 20.0** (and firmware's
  `RPI_CAPTURE_TIMEOUT_MS` 8000 -> 20000 to match) — the hosted API adds
  real network round-trip/queueing on top of capture+inference, and 8s
  didn't budget for that. Not yet measured on real hardware; revisit
  once a few real capture-to-reply timings exist.
- **Real firmware (`firmware/esp32/`, not the throwaway
  `testing/CV_Verify_UART_Prototype/` copy) now has the IMG-over-UART
  protocol and two-column image+result verifying_screen** — previously
  only the prototype had this; real firmware only showed SIZE-line text.
  Ported `rpi_uart.h/.cpp` (IMG receive), `ui_display.h/.cpp`
  (`ui_display_draw_grayscale_image()`), and `verifying_screen.h/.cpp`
  (image left / median+IQR+spread-bar right layout) from the prototype
  into real firmware, **keeping real firmware's IN SPEC/OUT OF SPEC
  check** (against `scheduler_get_target_um()`/`TARGET_TOLERANCE_UM`)
  which the prototype never had — the two features were merged, not a
  straight file copy. **Compiles clean** (`pio run -e embo`, RAM 15%,
  Flash 75%) — not yet flashed to real hardware this session (ESP32 not
  connected via USB yet).
- Python side syntax-checked (`py_compile`) clean. `inference-sdk` and
  `pillow` added to `../requirements.txt`.

**Blocked on hardware/network, not code, as of this update:**
- Pi dropped off the phone-hotspot network mid-session (known flakiness
  pattern for this specific Zero 2W, see Hardware status below) —
  nothing has been deployed to the Pi yet (`scp` never ran). Once it's
  back: `scp` the four changed files
  (`main.py`, `config.py` — from this folder — plus `detection.py`,
  `sizing.py` from `../cv-pipeline`), `pip install -r
  ../requirements.txt` in the venv, export `ROBOFLOW_API_KEY` on the Pi,
  then run the Layer 1 verification gate (camera detected, photo
  capturable, `CAPTURE`/`IMG`+`SIZE` round-trip over UART) before
  declaring the PoC done.
- Firmware not yet flashed — ESP32 needs a USB connection to this
  Windows machine (not the Pi) for `pio run -e embo -t upload`; COM
  port not yet identified.

---

# cv_verify — Session Handoff (2026-08-08)

Read this first in a fresh conversation to pick up exactly where the last
session left off. Written so a new Claude instance (or you) doesn't need to
re-derive any of this.

## Pick up here — active task

**Collecting training data for the particle-segmentation model.** User has
5 physical samples at different (unrecorded-to-Claude, tracked by user)
stroke counts, taking 20 photos of each = 100 total.

**Live protocol:** user says `"<stroke> stroke, photo <n> ready"` in chat →
Claude runs one capture over SSH → confirms saved → waits for the next
"ready". Do not batch ahead of what's been confirmed physically ready.

**Command to run per photo:**
```bash
ssh embo@172.31.163.209 "cd ~/EMBO/software/cv_verify && source .venv/bin/activate && python3 collect_training_data.py --stroke <N> --samples 1 --tag '<N>strok' --start-index <n> --note 'session batch'"
```
**IMPORTANT: `--start-index` defaults to 1 and does NOT auto-increment across
invocations** — you MUST pass `--start-index <n>` explicitly matching the
photo number, or it silently overwrites the previous photo's file *and*
duplicates its metadata.csv row (hit this on photo 2 of this session, had to
manually dedupe metadata.csv — see backup at `training_data/metadata.csv.bak`
on the Pi). Always check the stroke dir listing before running if unsure
what `<n>` should be next.

Saves `training_data/stroke_<N>/<N>strok_<n>_enhanced.jpg` (+ matching
`_raw.npy`, lossless pre-enhancement backup) and appends a row to
`training_data/metadata.csv`.

**Progress so far:** stroke 1 was recaptured on photo 5 once (camera
dislodged, redone with `--start-index 5` before photo 6 — metadata.csv
duplicate row from the redo was manually deduped, see `.bak2` on the Pi).
Stroke 1's photo 20 hit the known transient "Camera frontend has timed
out" warning (see Hardware status below) but self-recovered — file sizes
verified normal (raw exactly matches expected 1280x800 array size), no
re-shoot needed.

**Samples done: stroke 1 (20/20), stroke 3 (20/20), stroke 5 (20/20),
stroke 7 (20/20), stroke 9 (20/20).** **ALL 100 TRAINING IMAGES ARE
COLLECTED — this task is done.** Confirmed via
`training_data/metadata.csv` = 101 lines (1 header + 100 rows, no
duplicates).

**Next: move to the Roboflow/training phase** (see "After ~100 images
collected" plan below). All training data (raw `.npy` + enhanced `.jpg` +
`metadata.csv`) has been pulled from the Pi to
`software/cv_verify/training_data/` on this Windows checkout — verified
40 files/stroke folder (20 raw + 20 enhanced) x 5 folders, metadata.csv =
101 lines. Note: the Pi dropped off the network mid-transfer once (SSH
connection reset, then full timeout) and came back on its own after a
retry a few minutes later — if it happens again it's likely a Pi Zero 2W
WiFi/power flakiness issue, not a code problem.

**Next actual step, not yet started:** upload the `*_enhanced.jpg` files
(NOT the `.npy` raw files — Roboflow wants images) to Roboflow (Instance
Segmentation project, Smart Polygon tool, single class), annotate, export
YOLOv8-seg format, then train in Google Colab (`yolov8n-seg.pt`,
`imgsz=320`, ~150 epochs, export `format=onnx, int8=True`), then send the
`.onnx` back so Claude can benchmark real inference speed on the Pi.

**Repo hygiene note:** `training_data/` (~400MB) and `scratch/` (~11MB) are
gitignored, not committed — they're working data whose real destination is
Roboflow, not git history. Don't remove that `.gitignore` entry to "fix" a
future `git status` showing them as untracked; that's intentional.

---

## Connection

- Pi hostname/IP: `172.31.163.209` (mDNS `embo.local` resolves via
  PowerShell but NOT via git-bash/ssh directly — always use the IP from
  Bash, or re-resolve via `Resolve-DnsName embo.local` in PowerShell first
  if the IP ever changes).
- User: `embo`. **SSH key auth already set up** — `ssh embo@172.31.163.209`
  works passwordless from this Windows machine. Password (`slurry`) is a
  fallback only, was flagged to the user as one-time/disposable.
- sudo is passwordless on the Pi.
- Local repo: `c:\Users\Vincent Santosa\Desktop\EMBO` (this session's
  Windows checkout). Pi repo: `~/EMBO` (separate `git clone` of the same
  GitHub repo, `https://github.com/markantosa/embo.git`). **Files must be
  scp'd between them manually** — no shared filesystem, no auto-sync.
  Pattern used throughout: edit locally with Edit/Write tools, then
  `scp -q <local> embo@172.31.163.209:~/EMBO/software/cv_verify/<name>`.
- Python venv on Pi: `~/EMBO/software/cv_verify/.venv` (created with
  `--system-site-packages` so it can see the apt-installed `picamera2`).
  Always `source .venv/bin/activate` before running anything.
- Scratch/test images from this session live at
  `software/cv_verify/scratch/` (local, gitignored-worthy, not committed) —
  useful history of what's already been tried visually.

## Hardware status (all confirmed working on real hardware)

- **Pi Zero 2W**, Debian 13 (Trixie), kernel 6.18.
- **Camera: OV9281-110**, global shutter, mono, CSI. Detected via explicit
  `dtoverlay=ov9281` in `/boot/firmware/config.txt` (auto-detect doesn't
  recognize non-official sensors). Native format `R8`/`Y10P` at sensor
  level, but picamera2's ISP output stream only accepts `RGB888`/`YUV420`/
  etc. — capture.py uses `RGB888` (genuinely R=G=B triplicated mono).
- **UART**: `/dev/serial0` → `/dev/ttyAMA0` working, via `enable_uart=1` +
  `dtoverlay=disable-bt` in config.txt (frees the real PL011 UART from
  Bluetooth). ESP32 side NOT YET wired/tested end-to-end this session — all
  testing so far has been Pi-camera-only via direct Python calls, not
  through the actual `CAPTURE`/`SIZE` UART round-trip.
- **Physical camera cable/focus/lighting**: user has been iterating live —
  dark-field angle + crossed polarizers currently in place, accepted as
  "best achievable" given the syringe mount needs an open top for loading
  (rules out full enclosure).

## Software architecture (`software/cv_verify/`)

- `config.py` — all tunables, heavily commented with *why*, not just
  values. Read this file in full before touching any constant.
- `capture.py` — `Camera` class. Key method `read_frame_stack()`:
  auto-exposes fresh against current lighting every call, locks it, then
  captures `STACK_FRAMES` (6) frames and median-stacks them. **Do not use
  fixed exposure/gain from config** — that was tried and failed (see
  Decisions Made below).
- `preprocessing.py` — `enhance_for_display()` is the main entry point:
  `flat_field_correct()` → `to_uint8_stretched()` → `clarity_boost()`.
  Each step's docstring explains why it exists.
- `link.py` — UART request/reply protocol to the ESP32 (`CAPTURE` →
  `SIZE <median> <iqr>`). Matches firmware's fixed wire format exactly.
- `main.py` — the on-demand loop: idle until `CAPTURE` arrives, then
  capture+enhance+detect+reply. `detector.detect()` is still a stub
  (imported from `../cv-pipeline/detection.py`, `NotImplementedError`).
- `collect_training_data.py` — see "Pick up here" above.
- `TODO.md` — the layer-by-layer build-up plan, reprioritized for no-dye +
  OV9281 + on-demand-verification (not continuous-loop) constraints.
- `SETUP.md` — from-zero Pi Zero 2W bring-up guide.

## Key numbers (measured on real hardware, not guessed)

- `UM_PER_PIXEL = 20.26` — measured against a steel ruler (two 10mm-apart
  major ticks, cross-validated against minor-tick spacing, ~2% agreement).
  Rough bench calibration, NOT the formal Track 1 validation from
  `../cv-pipeline/master_experiment1_validation_protocol.md` — don't
  upgrade its confidence level without redoing that.
- `RPI_CAPTURE_TIMEOUT_MS = 8000` in firmware (`config.h`) — user has said
  they're open to raising this once a real model exists and gets
  benchmarked, but **this is a firmware change, not made yet, don't touch
  firmware without being asked.**
- ONNX Runtime installs cleanly on this Pi via prebuilt wheel (no source
  build risk, unlike `ultralytics`/`torch` which was NOT installed — too
  heavy, avoid). MobileNetV2 proxy benchmark: ~115ms/inference on Pi Zero
  2W CPU. Real `yolov8n-seg` at 640px would be much heavier (~40x more
  compute by rough scaling) — hence the plan to export at `imgsz=320` +
  INT8 quantization once a real model exists.

## Decisions made this session (don't re-litigate without new evidence)

1. **Dye is off the table** — real medical device, can't add contrast
   agent to patient-bound material. Drove the whole optical strategy.
2. **Fixed exposure/gain in `capture.py` was tried and rejected** — went
   stale every time the lighting setup changed (once underexposed/noisy,
   once overexposed/clipped). Replaced with auto-expose-then-lock per
   capture. Don't revert to fixed values without solving the staleness
   problem differently.
3. **Classical CV (thresholding/blob detection) was tried twice and
   rejected**: once via Difference-of-Gaussians blob detection on the
   flat-fielded image (detections clustered on glare, not spread through
   real content), once via an aggressively-binarized Canva edit (median
   blob size = 1px, i.e. mostly noise). Both independently point the same
   direction: a trained model is genuinely necessary here, not a threshold
   tuning problem. Don't re-attempt classical detection without new
   evidence the underlying image quality changed materially.
4. **Local-contrast enhancement (`clarity_boost`, unsharp-mask style) was
   validated against a manual Canva slider experiment** before being
   ported into code — confirmed to meaningfully help visual separation,
   given the open-top mount caps how far dark-field alone can go.
5. **`sys.path` shadowing bug** (fixed): `main.py` imports
   `detection.py`/`sizing.py` from `../cv-pipeline` — must be
   `sys.path.append`, not `insert(0, ...)`, or `cv-pipeline`'s own
   `config.py` silently shadows this folder's `config.py`.

## Not yet done / open threads

- ESP32 UART round-trip never tested end-to-end this session (Pi-side
  logic only, verified via direct Python calls).
- `detection.py`/`sizing.py` still stubs — blocked on the training data
  collection currently in progress.
- Ringing-artifact question from `clarity_boost` was raised once, user
  judged it "slight glare, not necessary to fix" — deprioritized, not
  resolved. Revisit only if it turns out to matter for annotation quality.
- Camera cable had one transient "Camera frontend has timed out" warning
  during testing (self-recovered) — worth a physical reseat check if it
  recurs, especially mid-collection-session.

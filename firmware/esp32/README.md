# Firmware — ESP32-S3

Arduino/C++ firmware for the EMBO ESP32-S3-WROOM-1-N4 control board (v3.4).
Built and flashed with PlatformIO inside VS Code.

## What this firmware does

| Module | File | Status |
|---|---|---|
| TMC2209 UART init, SpreadCycle, StallGuard | `src/motors.cpp` | ✅ |
| LEDC step generation, limit switch ISRs | `src/motors.cpp` | ✅ |
| Homing routine + stroke counter | `src/motors.cpp` | ✅ |
| AD9833 1MHz DDS init, UAS ADC calibration | `src/uas.cpp` | ✅ |
| Turbidity (APDS9960 ALS + MAX30102 backscatter) | `src/turbidity.cpp` | ⚠️ implemented but **currently disabled** — `turbidity_init()`/`turbidity_update()` are commented out in `main.cpp` (sensors not physically connected on the current bench setup), so this fusion channel reads 0 permanently until re-enabled |
| Force sensing (HX711 x2, shared clock) | `src/force_sensor.cpp` | ✅ read path — fusion input, calibrated scale factors in `calibration.h`. Automatic e-stop on over-force has been **removed** (see Safety below) — `force_sensor_estop_tripped()` still exists but nothing calls it |
| RPi UART receive + on-demand capture protocol | `src/rpi_uart.cpp` | ✅ |
| Sensor-to-physical-unit + 9-point fusion calibration | `src/calibration.cpp` | ✅ structure in place — `SENSOR_CAL_TABLE` is still all placeholders pending the 9-syringe bench session, see `../CALIBRATION.md`. With turbidity disabled and the table unpopulated, fusion currently has at most 2 live channels (UAS, force) and 0 trusted channels until real data is entered |
| Mixing scheduler (closed loop on fused sensor estimate) | `src/scheduler.cpp` | ✅ structure in place — depends on `SENSOR_CAL_TABLE` being real data |
| TFT display (LovyanGFX) + encoder + BTN1 + touch (secondary) | `src/ui.cpp`, `src/ui/screens/*` | ✅ menu-tree UI, see "Operational workflow" below |
| Shared TFT drawing helpers | `src/ui/ui_display.cpp` | ✅ |
| Shared input layer (EC11 ISR, BTN1/EC11-switch debounce, touch tap polling) | `src/ui/ui_input.cpp` | ✅ |
| Session-only mixing options (agent, syringe type, target type) | `src/mixing_options.cpp` | ✅ **cosmetic/labeling only, confirmed** — no calibration constant reads these; no viscosity calibration exists anywhere in the codebase |
| Session-only sound on/off flag | `src/sound_settings.cpp` | ✅ currently has no observable effect (the only sound, a boot chirp, fires before this setting is reachable) — exists for future UI tones |
| Load-cell flash logging (LittleFS) | `src/data_logger.cpp` | ✅ buffers HX711 samples to `/loadcell.csv` for a more trustworthy record than the live stream; controlled by the binary BLE `LOG_CTRL` characteristic, see below |
| NimBLE wireless debug UART (text console) | `src/ble_debug.cpp` | ✅ |
| NimBLE binary telemetry service (second, separate service) | `src/ble_binary_telemetry.cpp` | ✅ built to match a specific bench HTML dashboard, see "BLE binary telemetry" below — not documented elsewhere, and distinct from both the text console above and from `testing/PCB_Test_Firmware_v3_4`'s dashboard |

See [FIRMWARE_TODO.md](../FIRMWARE_TODO.md) for the full build-up task list and hardware checklist, and [`../CALIBRATION.md`](../CALIBRATION.md) for the one place to tweak every raw-sensor-to-physical-value mapping as calibration data comes in.

**This IS closed-loop — just not on CV.** UAS attenuation, APDS9960 turbidity, MAX30102 turbidity, and load-cell force are each calibrated against a 9-point bench dataset (known particle sizes from 9 reference syringes, see `CALIBRATION.md` §5) and fused into a live size estimate the scheduler checks after every stroke. **CV is the one signal that's optional and on-demand** — the RPi only captures a frame and runs CV when the ESP32 explicitly asks (`rpi_request_capture()`); the doctor can trigger it (encoder long-press) to independently double-check the fusion's result, but it never drives the stop decision itself.

**Currently, in practice, fusion has less than its full four channels.** Turbidity is disabled at the source (see table above) and `SENSOR_CAL_TABLE` is unpopulated, so `calib_estimate_particle_size_um()` currently returns `numChannelsUsed == 0` regardless — the scheduler can only stop via `MIXING_MAX_STROKES_SAFETY_CAP` until the 9-syringe bench session runs (and, separately, until turbidity sensors are wired up and `turbidity_init()`/`turbidity_update()` are uncommented in `main.cpp`).

**Why these four sensors weren't closed-loop inputs before now:** `docs/EMBO_UAS_CV_Technical_Advisory.txt` (and this project's own earlier rules) held UAS/turbidity/force to "diagnostic-only until an empirical correlation check proves it" — and until the 9-syringe bench dataset existed, nobody had that proof. Collecting that dataset is what changes them from diagnostic to trustworthy; see `calib_estimate_particle_size_um()` in `calibration.cpp`, which independently checks each channel's calibration curve for monotonicity and excludes any channel that doesn't pass, rather than blindly trusting whatever's plugged in.

**Why no PID even with a live signal now available:** the breakage process is still monotonic and irreversible (mixing only breaks particles smaller, never larger) — overshoot is a ruined batch, not "error to correct from the other side." That rules out a PID regardless of how good the measurement is. Instead, the scheduler strokes at a fixed rate and stops once the fused estimate reads within tolerance for several consecutive checks in a row (debounced against noise) — a hysteresis-style stop condition, not a proportional correction. Full rationale in `scheduler.h`'s header comment.

---

## Operational workflow

The UI is a menu tree (`src/ui.cpp`, `src/ui/screens/*`), driven **by the EC11 knob** (rotate to move a selection or adjust a value, short-press to select/confirm, long-press for a context action) **and BTN1** (Back on menu screens, stop/e-stop on the running screen — see "BTN1's second job" below). Every screen also has touch (XPT2046) handlers for Back/Toggle/Pause/Stop, but **touch is currently non-functional on real hardware** — the SPI bus was reverted to write-only (no MISO) after a boot hang (see "SPI bus" below), so `LGFX::getTouch()` always returns false and `ui_input_poll_touch_tap()` never fires a tap, no matter how complete the per-screen touch code looks. No screen is a dead end because of this: the encoder/BTN1 path covers every navigation action on its own, which is exactly why touch being dark right now isn't currently blocking anything.

Live sensor data stays a BLE-only, diagnostics-only concern for the mixing loop itself (`FORCE ON`/`TURB ON`/`UAS ON`/`FIT`, see below) — the one on-screen exception is the Telemetry screen (Settings > Developer mode > Telemetry), which shows a live readout (load cell, turbidity, UAS, motor position) directly on the TFT.

**Screen tree, as actually wired in `ui.cpp`:**

```
Insert syringe (boot placeholder)
 └─ Start Menu
     ├─ Start ──► Agent Selection (Gelfoam/Lyostypt)
     │             └─► Syringe Type (Terumo/Nipro)
     │                   └─► Target Type (Size/Viscosity)
     │                         └─► Mixing Menu ──► Warning (mount check) ──► Mixing Running ──► End ──► (back to Start Menu)
     │
     ├─ Settings
     │   ├─ Sound toggle
     │   ├─ Motion ──┬─ Home Motors
     │   │            ├─ Move Left Motor (discrete 10-step nudge)
     │   │            ├─ Move Right Motor (discrete 10-step nudge)
     │   │            └─ Stroke Testing (see "Stroke test" below)
     │   └─ Developer mode ──┬─ Telemetry (live on-screen sensor readout)
     │                        └─ UAS Debug Mode (BLE on/off toggle)
     │
     └─ Camera feature ──► mount-check prompt ──► (stub — "feature idea developing in progress")
```

Any screen can reach **VerifyingScreen** (RPi camera capture, `rpi_request_capture()`) via encoder long-press from the Mixing Menu, Warning, or End screens.

Agent (Gelfoam/Lyostypt), Syringe Type, and Target Type (`src/mixing_options.cpp`) are **session-only labels with no calibration effect** — confirmed nothing in `calibration.cpp`/`calibration.h` reads them, and no viscosity-based calibration path exists anywhere in the codebase; Target Type = Viscosity is UI-selectable but not functionally different from Size today.

**Two screen source files exist but are dead code — not reachable from `ui.cpp`'s actual graph above:** `src/ui/screens/developer_mode_screen.*` (an older, self-contained Developer Mode screen with its own inline telemetry — superseded by the plain menu + separate `TelemetryScreen` shown in the tree above) and `src/ui/screens/jog_motor_screen.*` (a continuous encoder-jog screen — superseded by the inline discrete 10-step nudge used by Move Left/Right Motor). Neither is instantiated anywhere; leave them if you're grepping for behavior that doesn't match what you see on the board, or delete them in a future cleanup pass.

**A bench-only back door exists outside this tree:** `ui_open_bench_diagnostics_menu()` (recalibrate UAS baseline, reset the breakage-model fit) is pushed on top of whatever screen is showing, but only in response to the BLE `MENU` command — it's not reachable from any on-screen menu item.

| Stage | Trigger | What happens | Hardware involved |
|---|---|---|---|
| **1. Boot / self-test** | Power on | TMC2209 UART init + SpreadCycle write/readback, AD9833 + UAS baseline sample, HX711 init + fresh tare, then straight to the "Insert syringe" placeholder screen — **no homing happens automatically at boot, and BLE is off by default**. Turbidity init/update are currently commented out in `main.cpp` (sensors not physically connected on the bench) — see "What this firmware does" above. Any `BLOCKING` check failing (see `FIRMWARE_TODO.md`) should halt — no silent bad-data path into the scheduler. | Both TMC2209 modules, AD9833, HX711 x2, status LED |
| **2. Homing** | **Press EC11 knob** on the Mixing Menu while it reads "NOT HOMED" (or the BLE `HOME` command) | `motors_home()` drives both motors to their limit switches and backs off; stroke counter resets to 0. Blocking for the duration of the attempt (same tradeoff already accepted for BLE `HOME`) — nothing can be running yet, since a run itself requires being homed first. On failure, `ui_show_error()` puts the TFT into a persistent fault screen — no way to start a run until the board is rebooted and the fault investigated. The `embo_bench` build (see "Bench testing" below) fakes this step instantly instead and shows a loud on-screen banner so it's never mistaken for a real homed board. | M1/M2 steppers, limit switches (J6/J7), TFT |
| **3. Setup flow** | Start Menu > Start | Agent Selection → Syringe Type → Target Type (all session-only labels, see above) → Mixing Menu, where the encoder adjusts target size (5µm steps, 50–1000µm). | TFT, EC11 |
| **3b. Optional: verify current size** | **Hold EC11 knob** (Mixing Menu, Warning, or End screen) | Requests one RPi capture+CV pass (`rpi_request_capture()`) and shows the median/IQR result on-screen once it replies, color-coded IN SPEC / OUT OF SPEC against the target — an independent double-check, not something that runs automatically, and unrelated to the separate (stub) "Camera feature" menu item. See §"Camera verification" below. | EC11 push-switch, RPi camera, TFT |
| **4. Warning / mount check** | Encoder press on Mixing Menu (once homed) | "Ensure syringe is properly mounted inside" — confirm (encoder press) starts the run; Back (BTN1 short press; the screen's touch Back handler exists but is currently dark, see touch note above) returns to the Mixing Menu without starting anything. | TFT, EC11, BTN1 |
| **5. Running** | Encoder confirm on the Warning screen | Closed loop: the scheduler strokes continuously, and after every stroke reads UAS + force (+ turbidity once re-enabled), converts each through its own 9-point calibration curve, and fuses the trustworthy ones (median) into one size estimate — see `calibration.h`'s `calib_estimate_particle_size_um()`. The screen has a touch **Pause** (holds the run mid-stroke, resumable via `scheduler_pause()`/`scheduler_resume()`) and touch **Stop** (calls the same `scheduler_stop()` as BTN1's short-press) — both currently unreachable with touch dark; **BTN1 short-press (Stop) and long-press (e-stop) are the only ways to affect a running mix today, there is no pause without touch.** | Both motors, UAS chain, HX711 x2, TMC2209 UART, TFT |
| **6. Stop** | **BTN1 short press** (touch Stop exists in code, currently non-functional) | Graceful stop — finishes the in-progress stroke, then holds. | BTN1, both motors |
| **6b. Emergency stop** | **BTN1 held ≥800ms — the ONLY e-stop path.** | Immediate — kills motor power mid-step (`scheduler_emergency_stop()`). **The automatic HX711 over-force e-stop has been removed**: `force_sensor_estop_tripped()`/`HX711_ESTOP_GRAMS` (`calibration.h`) still exist and are still correct as *code*, but `main.cpp`'s `loop()` no longer calls the tripped-check, by deliberate decision — see the comment there. Force readings still feed sensor fusion; they no longer trigger an automatic stop. There is currently **no automatic overforce/jam protection** — a manual BTN1 hold is required. Flag this to whoever owns the safety case before relying on it clinically; see `CALIBRATION.md` §4/§8 for the (currently inert) threshold. | BTN1, both motors |
| **7. Target reached / End screen** | Automatic | The fused sensor estimate reads within `TARGET_TOLERANCE_UM` of the target for `FUSION_CONSECUTIVE_CHECKS_REQUIRED` consecutive stroke-checks in a row (debounced against a single noisy reading), and the scheduler stops itself. If the fusion never converges, a hard `MIXING_MAX_STROKES_SAFETY_CAP` stops the run anyway and flags it as a safety-cap stop, not a confirmed one (`scheduler_hit_safety_cap()`) — a sign to check calibration, not a normal completion. **With turbidity disabled and `SENSOR_CAL_TABLE` unpopulated (see "What this firmware does"), every run currently ends via the safety cap, not a confirmed fusion stop, until both are addressed.** Either way, press-and-hold the encoder knob on the End screen independently confirms the achieved size with a camera capture; BTN1 short-press returns to the Start Menu (the screen's touch "Back to menu" is currently non-functional, same as elsewhere). | TFT |

**Stroke test** (Settings > Motion > Stroke Testing, `stroke_test_screen.cpp`): a bench diagnostic, unrelated to the mixing scheduler's own stroke counter. Drives M1 alone, then M1+M2 concurrently, using its own speed/count/stall-threshold constants (`MOTOR_STROKE_TEST_HZ`, `STROKE_TEST_COUNT_MIN/MAX/DEFAULT`, `STROKE_TEST_STALL_SG_THRESHOLD` — the last is currently 0/disabled, see `config.h`) — for checking mechanical health, not for producing a mixed batch.

**Button/encoder/touch summary** (see `include/config.h` and `src/ui.cpp`):

| Control | Function |
|---|---|
| EC11 rotary | Adjust target particle size (50–1000µm) on the Mixing Menu; move the selection on menu/list screens |
| EC11 push-switch (short press) | Context-dependent: home (if not yet homed) or continue to Warning (Mixing Menu); confirm and start (Warning); select a menu item; toggle (UAS debug mode, sound) |
| EC11 push-switch (held ≥800ms) | Request an optional camera size verification (Mixing Menu, Warning, or End screen) |
| Touchscreen (XPT2046) | Secondary/supplementary — Back, Pause/Stop on the running screen, toggle taps — see individual screens; not required for any navigation path |
| BTN1 (short press) | **Two roles, on mutually exclusive screens** — see "BTN1's second job" below |
| BTN1 (held ≥800ms) | Emergency stop — **only does anything on the Mixing Running screen**; the only e-stop path in this firmware (see 6b above); a no-op everywhere else |

### BTN1's second job

BTN1 was previously overloaded (v2.4/v2.5 firmware used it for start, and a second button for stop), then simplified to exactly one job (stop/e-stop) once the encoder's own switch took over start/confirm — a safety-critical button is easier to trust with one unambiguous function. That principle is preserved, not abandoned: BTN1's short press is now **Back** on every non-running menu/screen, and **remains exactly graceful-stop** on the Mixing Running screen — never both at once, because those are different screens and only one is ever active. Every screen that grants BTN1 the Back behavior gates it behind `!scheduler_is_running()` in code (not just "this screen happens not to be reachable during a run" — see `menu_screen.cpp`'s comment for the reasoning), so even if a future change ever made a menu screen reachable mid-run, BTN1 still couldn't be mistaken for "back" instead of "stop" there. `mixing_running_screen.cpp` itself is untouched by this — its BTN1 handling is exactly what it was before.

The new touch Pause/Stop buttons on the running screen are additional ways to reach `scheduler_pause()`/`scheduler_stop()`, not replacements for BTN1's existing stop/e-stop role there.

### Developer Mode / BLE

BLE is **off by default** — `ble_debug_init()` sets up the NimBLE stack but does not advertise. Turn it on from Start Menu > Settings > Developer mode > UAS Debug Mode (shown on-screen: "turning this mode on will enable Bluetooth"); the same screen shows the current on/off state and lets you toggle it back off. The sibling **Telemetry** screen (Settings > Developer mode > Telemetry, `telemetry_screen.cpp`) shows a live readout (load cells, turbidity, UAS reading, motor position) directly on the TFT — the only screen in this design where raw sensor values are shown on-device rather than BLE-only. (An older, self-contained `developer_mode_screen.cpp` also exists in the source tree with similar inline telemetry, but it's dead code — not reached from `ui.cpp`'s actual menu wiring, see the screen-tree section above.)

### Camera verification (optional, on-demand — independent of the fusion loop)

CV is **never a fusion input** (see above) — it's an **operator-triggered, single-shot check** the doctor can run any time from the Mixing Menu, Warning, or End screen (encoder long-press), to independently confirm what the four onboard sensors already decided. This is unrelated to the Start Menu's separate "Camera feature" item, which is a stub per the current UX design (not yet developed):

1. ESP32 sends `CAPTURE` to the RPi over UART (`rpi_request_capture()`).
2. RPi captures one frame, runs CV once, and replies with `SIZE <median_um> <iqr_um>`.
3. ESP32 displays the result on the TFT (color-coded IN SPEC / OUT OF SPEC against the target) and feeds it into the diagnostic-only breakage-model fit (`calib_breakage_add_point()`) for cross-checking against the fusion estimate in future runs. It does not affect a run already finished or in progress, and it never overrides the fusion-based stop decision.
4. If the RPi doesn't reply within `RPI_CAPTURE_TIMEOUT_MS` (8s, `config.h`), the screen shows a timeout notice instead of hanging indefinitely.

**Future work, not yet implemented:** the RPi may eventually also send a compressed JPEG of the annotated frame (particle circles overlaid) alongside the `SIZE` line, for display on a future UI revision. No wire protocol for that exists yet — see `rpi_uart.h`'s closing comment. This firmware only handles the text `SIZE` reply today.

---

## Flashing via VS Code + PlatformIO

### 1. Install PlatformIO

If you don't have it: open VS Code → Extensions (`Ctrl+Shift+X`) → search **PlatformIO IDE** → Install. Restart VS Code.

### 2. Open the project

Open the folder `firmware/esp32/` in VS Code (not the repo root — PlatformIO needs to see `platformio.ini` at the top level of the opened folder).

```
File → Open Folder → .../EMBO/firmware/esp32
```

PlatformIO will automatically detect the project and download the ESP32-S3 toolchain and all libraries on first open. This takes a few minutes.

### 3. Connect the board

Plug USB-C into **J13** on the EMBO board (the USB-C programming connector). The ESP32-S3 has a native USB CDC/JTAG controller on GPIO19/20 — no USB-UART bridge chip is needed.

Windows should enumerate a COM port automatically. If it doesn't, install the [ESP32-S3 CDC driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/establish-serial-connection.html).

### 4. Build

Open the VS Code terminal (`Ctrl+`` `):

```bash
pio run
```

Or click the **checkmark (✓) Build** button in the PlatformIO toolbar at the bottom of VS Code.

### 5. Flash

```bash
pio run --target upload
```

Or click the **right-arrow (→) Upload** button in the PlatformIO toolbar.

PlatformIO auto-resets the ESP32-S3 into bootloader mode via USB CDC control signals. You don't need to press any buttons for a normal flash.

### 6. Monitor serial output

```bash
pio device monitor
```

Or click the **plug Monitor** button. Baud rate is set to 115200 in `platformio.ini`.

For wireless debug output during a run, connect a BLE terminal app (e.g. **nRF Toolbox** or **Serial Bluetooth Terminal**) to the device named `EMBO-Debug` and subscribe to the Nordic UART TX characteristic.

### Bench testing with no motors/limit switches connected

Homing is never automatic at boot in either build — `setup()` always goes straight to the UI (see "Operational workflow" above), and homing only happens when the operator presses the knob on the Mixing Menu (or sends the BLE `HOME` command). With `[env:embo]` (the real env) and no limit switches connected, that homing attempt will time out after `HOMING_TIMEOUT_MS` (30s) and show a HARDWARE FAULT screen — expected, not a bug.

If you want the knob-press homing attempt to succeed instantly with nothing connected (e.g. to get past "NOT HOMED" and exercise the rest of the UI/BLE console), there's a separate bench-only environment that skips the real limit-switch wait:

```bash
pio run -e embo_bench -t upload
```

This is a genuinely different build (`BENCH_NO_HOMING`, see `motors.cpp`), not a runtime flag — **never flash it to a board that has real hardware attached or that a patient/operator might use.** The only on-hardware sign it's running is a red "BENCH BUILD - NO HOMING" banner across the top of the Mixing Menu (plus a loud BLE/serial log line once you press the knob to home) — if you ever see that banner, a "homed" board hasn't actually checked anything, and starting a "run" will drive the motors with no homed reference position. Re-flash the normal `embo` env before any real use.

---

## BLE debug commands

**BLE is off by default** (see "Developer Mode / BLE" above) — turn it on from Start Menu > Settings > Developer mode > UAS debug mode before trying to connect. Once enabled, connect to `EMBO-Debug` over BLE and write commands to the Nordic UART RX characteristic. Responses appear on the TX characteristic (the same stream as `ble_log()` output).

**This is the one place raw sensor data can be pulled off the board** — the TFT deliberately doesn't show it (see Operational Workflow above), so calibration data collection (`../CALIBRATION.md`) happens through this link, not the screen.

Note: this is a separate BLE service (`EMBO-Debug`, Nordic UART Service) from `testing/PCB_Test_Firmware_v3_4`'s dashboard (`EMBO-PCB-Test-v3.4`, custom GATT + a Web Bluetooth page with live charts). That project is a bench-bring-up tool for an unpopulated/newly-assembled board; this one is the production firmware's diagnostic link. Don't expect the web dashboard to work against this firmware or vice versa.

**Recommended app:** Serial Bluetooth Terminal (Android) or nRF Toolbox (iOS/Android). Select the NUS service, subscribe to TX, and use the input bar to send commands.

### Commands

| Command | Example | Description |
|---|---|---|
| `HOME` | `HOME` | Drives both motors to their limit switches and backs off. Blocks until complete. Logs success or failure. |
| `MOVE <motor> <steps>` | `MOVE 1 400` | Moves motor 1 forward 400 steps at 500 Hz. Use negative steps for reverse: `MOVE 2 -200`. Stops automatically on completion or limit trip. |
| `UAS ON` / `UAS OFF` | `UAS ON` | Streams per-frequency attenuation + the most recent on-demand CV capture's median/IQR (if any) every 200ms — useful for cross-checking UAS against CV independently of the fusion estimate. Trigger captures with `CAPTURE` (below) while this is running to populate the CV side of the log. |
| `FORCE ON` / `FORCE OFF` | `FORCE ON` | Streams calibrated grams for both HX711 channels every 200ms — see `CALIBRATION.md` §4 before trusting the grams figure (tare/scale are still placeholders). |
| `TURB ON` / `TURB OFF` | `TURB ON` | Streams raw APDS9960 ALS + MAX30102 IR/RED counts, plus whether each sensor responded on the I2C bus. **Currently reads stale/zero values** — `turbidity_update()` isn't called from `main.cpp`'s `loop()` right now (sensors not physically connected on the bench, see "What this firmware does"), so this stream just reflects whatever the last real reading was before it got disabled. |
| `FUSION` | `FUSION` | **One-shot, not a stream.** Prints the CURRENT live value of all four fusion channels plus the resulting fused estimate and per-channel trust flags — this is the exact command to run during the 9-syringe bench session: hold a known-size syringe steady, run `FUSION`, copy the printed values into that syringe's row in `SENSOR_CAL_TABLE` (`calibration.cpp`). See `CALIBRATION.md` §5. |
| `CAPTURE` | `CAPTURE` | Manually triggers the same on-demand RPi capture+CV the UI's encoder long-press does — for bench testing without the physical knob. Result arrives as an `RPi: median=... iqr=...` log line once the RPi replies (or a timeout log after `RPI_CAPTURE_TIMEOUT_MS`). |
| `FIT` | `FIT` | Dumps the DIAGNOSTIC-ONLY breakage-model fit (`k`, `D0`, verification point count) plus the current run's stroke count, last fused estimate, and whether the safety cap was hit. Does not reflect what's actually driving the stop condition — that's `FUSION`. |
| `FIT RESET` | `FIT RESET` | Discards the accumulated breakage-model fit. Nothing calls this automatically — use when the material genuinely changes (`CALIBRATION.md` §5). |
| `MENU` | `MENU` | Opens the bench/engineering diagnostics menu (recalibration, fit reset) — separate from the operator-facing Settings screen reachable from the Start Menu (see `ui.h`). |
| `UASFREQ <hz>` / `UASFREQ CLEAR` | `UASFREQ 950000` | Manual UAS frequency override for bench characterization — see "Manual frequency control + sweep" below. |
| `UASSWEEP <start> <end> <step> <dwell_ms>` / `UASSWEEP STOP` | `UASSWEEP 900000 1100000 10000 200` | Automated frequency sweep — see "Manual frequency control + sweep" below. |

Sending an unrecognised command prints the command list back.

### Automatic streaming

| Stream | Trigger | Format | Interval |
|---|---|---|---|
| UAS ADC | `UAS ON` active | `UAS: f0=900kHz(att=0.98) ... \| last CV capture: median=310 iqr=40` | 200ms |
| Force | `FORCE ON` active | `FORCE: ch1=12.3g ch2=11.8g` | 200ms |
| Turbidity | `TURB ON` active | `TURB: als=512(ok) ir=98234 red=87211(ok)` | 200ms |
| StallGuard | While a `MOVE` is running | `SG M1: 512` | 200ms |

StallGuard (`SG_RESULT`) streams automatically whenever a `MOVE` command is in progress — no separate command needed. Higher value = less load on the motor. Valid only while SpreadCycle is active (confirmed at boot via BLE log).

## BLE binary telemetry (second, separate service — undocumented until now)

`src/ble_binary_telemetry.cpp` runs a **second NimBLE service on the same server** as the text-console `EMBO-Debug` service above, with its own custom UUIDs, gated by the same enable/disable toggle (`ble_debug_set_enabled()` — Settings > Developer mode > UAS Debug Mode turns both on together). It exists to drive a specific bench HTML dashboard (`index_logging_to_record_UAS_csv_data.html`, not `testing/PCB_Test_Firmware_v3_4`'s dashboard) rather than a human typing commands:

| Characteristic | Direction | Purpose |
|---|---|---|
| `MOTOR_CMD` | write | Jog a motor (same underlying action as the text console's `MOVE`) |
| `HOME_CMD` | write | Trigger homing (same underlying action as `HOME`) |
| `FREQ_CMD` | write | Manual UAS frequency override (same underlying action as `UASFREQ`) |
| `LOG_CTRL` | write | `0`=stop, `1`=start, `2`=clear, `3`=dump — controls the flash-backed load-cell logger (`src/data_logger.cpp`, writes `/loadcell.csv` to LittleFS) |
| `LOG_DATA` | notify | Chunked read-back of the logged CSV once a `LOG_CTRL` dump (`3`) is requested |
| `TelemetryPacket` | notify, ~40ms | A packed 41-byte struct: UAS volts/frequency, StallGuard results, motor positions, homing/limit-switch flags, raw force counts, ALS/IR/RED turbidity counts, turbidity sensor-ok flags |

**Why a flash-backed logger instead of just streaming:** `data_logger.cpp`'s buffered CSV is a more trustworthy record than the live 40ms notify stream, which can drop packets over a lossy BLE link — start a session, run the test, then dump and inspect the file rather than trying to reconstruct results from the live stream alone.

### Safety

- If the BLE client disconnects mid-move, the active motor is stopped and disabled immediately.
- `MOVE` commands respect the limit switch ISRs — the motor stops if a switch trips before the step count completes.

---

## Manual bootloader recovery

If the board is unresponsive to normal flashing:

1. Hold **SW_boot** (BOOT button, GPIO0)
2. Press and release **SW_reset** (RESET button, EN pin)
3. Release **SW_boot**
4. The board is now in download mode — run `pio run --target upload`

---

## Rebuilding IntelliSense (fix red squiggles)

VS Code may show red squiggles on `Arduino.h` or ESP32 headers because IntelliSense doesn't know the PlatformIO toolchain paths yet. Fix:

```
Ctrl+Shift+P → PlatformIO: Rebuild IntelliSense Index
```

Wait ~30 seconds. Squiggles will clear. This only needs to be done once after first open or after changing `platformio.ini`.

---

## SPI bus — device summary

GPIO35 (MOSI) / GPIO36 (CLK) are shared between the TFT and the AD9833. There is no MISO on this bus — a touch (XPT2046) integration was attempted and **reverted** after it caused a boot hang (board stopped mid-`setup()`, buzzer stuck on) — see the `TOUCH_TODO` comment in `LGFX_Config.h` for what to restore once that's debugged on real hardware with a scope on GPIO12/46. Until then, touch is unused and `ui_input_poll_touch_tap()` always returns -1 (no tap) since `LGFX::getTouch()` safely returns false with no touch panel attached.

| Device | CS | Mode | Max clock | Notes |
|---|---|---|---|---|
| ILI9341 TFT | GPIO39 | Mode 0 | 20 MHz | Via 20-pin IDC ribbon, driven by LovyanGFX (`LGFX_Config.h`) |
| AD9833 DDS | GPIO38 | **Mode 2** | ~10 MHz | Direct trace, no ribbon |

**Integration note (verify on first bring-up):** the TFT is driven by LovyanGFX, which owns `SPI2_HOST` directly via its own `Bus_SPI` instance — it does not go through the Arduino `SPI` (SPIClass) object the way the old TFT_eSPI setup implicitly did. `uas.cpp` still calls `SPI.begin()` for the AD9833 on the same physical MOSI/CLK GPIOs with a different CS. This is fine electrically, but the two software drivers haven't been confirmed not to contend over the same underlying SPI peripheral — see the comment in `uas.cpp`'s `uas_init()`.

**A dedicated-HSPI-host variant of this was tried and reverted** (see `uas.cpp`) after a board hung at the boot splash following that change — moving the AD9833 to its own SPI *host* while it stayed wired to the *same physical MOSI/CLK pads* as the TFT doesn't obviously avoid contention (only one peripheral's signal can drive a given pad at a time), and can't be verified without a scope. Don't re-attempt this without hardware to check it on — if contention shows up, the fix needs verifying against an actual capture of GPIO35/36 during boot, not just reasoning about it.

**Touch (XPT2046) was also tried and reverted, for the same reason.** A `bus_shared=true` config was added, which is LovyanGFX's own supported way to share a bus between a panel and touch (and architecturally different from the AD9833 problem above — it stays inside one framework's bus arbitration rather than mixing two). That should, in principle, avoid the same class of contention — but "should in principle" isn't verified on this board, and a boot hang appeared immediately after adding it, in the same place (`tft.init()`, since that's what brings the touch bus up too). Don't re-add this without a scope confirming GPIO12 (T_CS) and GPIO46 (T_DO) actually behave as expected during `tft.init()`.

## UAS multi-frequency sweep (July 2026)

`uas_update()` no longer holds a single fixed 1MHz tone. Per the [technical advisory](../../docs/EMBO_UAS_CV_Technical_Advisory.txt), a single-frequency attenuation reading isn't reliably invertible to particle size for a polydisperse population, so the AD9833 now sweeps `UAS_NUM_FREQUENCIES` (3, `config.h`) discrete tones every update cycle: 900kHz / 1MHz / 1.1MHz by default. This is a firmware/timing-only change — no board respin.

**UAS is one of the four closed-loop fusion inputs (`scheduler.cpp`), gated by its own monotonicity check.** Once `SENSOR_CAL_TABLE` (`calibration.cpp`) has real bench data, `calib_estimate_particle_size_um()` independently verifies UAS's 9-point calibration curve is actually monotonic before trusting it — if it isn't, that channel is silently excluded from the fusion rather than trusted anyway (see `FusedSizeEstimate.uasTrusted`, readable via the BLE `FUSION` command). CV remains a good *independent* cross-check regardless: connect over BLE, send `UAS ON`, trigger a few `CAPTURE`s across a real mixing run, and compare the per-frequency attenuation against each CV result (both printed on the same log line) — see `FIRMWARE_TODO.md` for the full gate.

Each library calls `SPI.beginTransaction()` with its own settings before every access — mode and clock switch automatically. Never hold one device's CS asserted while accessing another.

### Manual frequency control + sweep (merged from `testing/UAS_Testing_with_multiple_frequency_sweep`)

That bench rig's headline feature — retuning the AD9833 to an arbitrary frequency at runtime, and an automated sweep across a range — is now available on the production BLE debug console (it's not a separate GATT service; see the `EMBO-Debug`/`EMBO-UAS-Sweep` distinction above, which is unchanged):

| Command | Example | Description |
|---|---|---|
| `UASFREQ <hz>` | `UASFREQ 950000` | Retunes the AD9833 and holds it there, bypassing the production 3-tone cycle. `uas_get_attenuation()` keeps reporting its last production reading unchanged the whole time. Refused while a run is in progress. |
| `UASFREQ CLEAR` | `UASFREQ CLEAR` | Resumes the normal `UAS_NUM_FREQUENCIES` production cycle. |
| `UASSWEEP <start_hz> <end_hz> <step_hz> <dwell_ms>` | `UASSWEEP 900000 1100000 10000 200` | Walks the range, dwelling `dwell_ms` at each step, and logs a `SWEEP: freq_hz=... mean_mv=... std_mv=... n=...` line per step. Non-blocking — advances one step per `ble_debug_update()` call, same pattern as `MOVE`'s timed completion, so the safety loop (e-stop, etc.) is never stalled for the sweep's duration. Refused while a run is in progress. |
| `UASSWEEP STOP` | `UASSWEEP STOP` | Cancels an in-progress sweep and restores the production cycle. |

**Safety:** a sweep or manual override is also auto-cancelled if a run somehow starts while one is active (belt-and-braces — `scheduler_start()` already clears any override itself before a run begins, so attenuation fusion during an actual mix can never read a leftover bench frequency), and on BLE disconnect, same as an in-progress `MOVE`.

**Dashboard:** `web/uas_debug_dashboard.html` — open in Chrome/Edge (Web Bluetooth), connect to `EMBO-Debug`, and it gives you quick-command buttons, a manual-frequency card, and a sweep card with a live results table + CSV download (`freq_hz, mean_mv, std_mv, n`) — the same workflow as the bench rig's dashboard, adapted to this project's plain-text NUS console. This is a *different* dashboard from the one driving the binary telemetry service (`index_logging_to_record_UAS_csv_data.html`, see "BLE binary telemetry" above) — the two connect to different characteristics on the same `EMBO-Debug` server and aren't interchangeable.

## UART assignment

| Peripheral | ESP32-S3 UART | GPIO | Baud |
|---|---|---|---|
| TMC2209 motor drivers | UART1 | GPIO4 TX / GPIO44 RX (half-duplex bus, separate pins as of v3.4) | 115200 |
| Raspberry Pi | UART2 | GPIO47 TX / GPIO48 RX | 921600 |
| USB monitor | USB CDC | GPIO19/20 (fixed) | 115200 |

UART1 and UART2 are independent peripherals — do not reassign one to the other's number. TMC2209 TX/RX are genuinely separate ESP32 pins wired to the same physical PDN_UART bus node off-chip — see `docs/EMBO_PCB_Design_Brief_v3_4.txt` §7.3 for why a single shared pin was tried first and dropped.

## I2C — turbidity sensing

| Bus | GPIO | Clock | Devices |
|---|---|---|---|
| Wire (I2C0) | GPIO3 (SDA) / GPIO43 (SCL) | 400kHz | APDS9960 (0x39, ALS) + MAX30102 (0x57, backscatter) |

## ADC

GPIO1 = ADC1_CH0. ADC1 is fully usable while BLE radio is active. ADC2 shares silicon with the RF block and cannot be used for analog while BLE is running — no ADC2 pins are used for analog in this design.

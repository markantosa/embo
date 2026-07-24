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
| Turbidity (APDS9960 ALS + MAX30102 backscatter) | `src/turbidity.cpp` | ✅ read path — one of four closed-loop sensor-fusion inputs, see below |
| Force sensing (HX711 x2, shared clock) | `src/force_sensor.cpp` | ✅ read path — fusion input AND independent e-stop, both pending real calibration data, see `CALIBRATION.md` §4 |
| RPi UART receive + on-demand capture protocol | `src/rpi_uart.cpp` | ✅ |
| Sensor-to-physical-unit + 9-point fusion calibration | `src/calibration.cpp` | ✅ structure in place — `SENSOR_CAL_TABLE` is still all placeholders pending the 9-syringe bench session, see `../CALIBRATION.md` |
| Mixing scheduler (closed loop on fused sensor estimate) | `src/scheduler.cpp` | ✅ structure in place — depends on `SENSOR_CAL_TABLE` being real data |
| TFT display (LovyanGFX) + encoder + one button | `src/ui.cpp` | ✅ loading-screen UI + optional camera-verification screen, no touch |
| NimBLE wireless debug UART | `src/ble_debug.cpp` | ✅ |

See [FIRMWARE_TODO.md](../FIRMWARE_TODO.md) for the full build-up task list and hardware checklist, and [`../CALIBRATION.md`](../CALIBRATION.md) for the one place to tweak every raw-sensor-to-physical-value mapping as calibration data comes in.

**This IS closed-loop — just not on CV.** UAS attenuation, APDS9960 turbidity, MAX30102 turbidity, and load-cell force are each calibrated against a 9-point bench dataset (known particle sizes from 9 reference syringes, see `CALIBRATION.md` §5) and fused into a live size estimate the scheduler checks after every stroke. **CV is the one signal that's optional and on-demand** — the RPi only captures a frame and runs CV when the ESP32 explicitly asks (`rpi_request_capture()`); the doctor can trigger it (encoder long-press) to independently double-check the fusion's result, but it never drives the stop decision itself.

**Why these four sensors weren't closed-loop inputs before now:** `docs/EMBO_UAS_CV_Technical_Advisory.txt` (and this project's own earlier rules) held UAS/turbidity/force to "diagnostic-only until an empirical correlation check proves it" — and until the 9-syringe bench dataset existed, nobody had that proof. Collecting that dataset is what changes them from diagnostic to trustworthy; see `calib_estimate_particle_size_um()` in `calibration.cpp`, which independently checks each channel's calibration curve for monotonicity and excludes any channel that doesn't pass, rather than blindly trusting whatever's plugged in.

**Why no PID even with a live signal now available:** the breakage process is still monotonic and irreversible (mixing only breaks particles smaller, never larger) — overshoot is a ruined batch, not "error to correct from the other side." That rules out a PID regardless of how good the measurement is. Instead, the scheduler strokes at a fixed rate and stops once the fused estimate reads within tolerance for several consecutive checks in a row (debounced against noise) — a hysteresis-style stop condition, not a proportional correction. Full rationale in `scheduler.h`'s header comment.

---

## Operational workflow

How a doctor moves through a run, and which hardware/firmware drives each stage. The TFT is deliberately minimal — a set-target screen and a loading screen, nothing else — by project decision; live sensor data is a BLE-only, diagnostics-only concern (see BLE debug commands below), not something displayed on the device itself.

| Stage | Trigger | What happens | Hardware involved |
|---|---|---|---|
| **1. Boot / self-test** | Power on | TMC2209 UART init + SpreadCycle write/readback, AD9833 + UAS baseline sample, turbidity I2C scan, HX711 pin init. Any `BLOCKING` check failing (see `FIRMWARE_TODO.md`) should halt — no silent bad-data path into the scheduler. | Both TMC2209 modules, AD9833, turbidity sensors, status LED |
| **2. Homing** | Automatic after boot | `motors_home()` drives both motors to their limit switches and backs off; stroke counter resets to 0. On failure, `ui_show_error()` puts the TFT into a persistent fault screen — no way to start a run until the board is rebooted and the fault investigated. | M1/M2 steppers, limit switches (J6/J7), TFT |
| **3. Set target** | — | TFT shows the current target size; doctor loads the syringe (interlock handled by mech team, not sensed by this firmware). | TFT |
| **4. Setpoint adjust** | Turn EC11 encoder | Adjusts target particle size in 5µm steps, clamped to 50–1000µm (`config.h`). Only active on the set-target screen. | EC11 encoder |
| **4b. Optional: verify current size** | **Hold EC11 knob** (set-target or done screen) | Requests one RPi capture+CV pass (`rpi_request_capture()`) and shows the median/IQR result on-screen once it replies, color-coded IN SPEC / OUT OF SPEC against the target — an independent double-check, not something that runs automatically. See §"Camera verification" below. | EC11 push-switch, RPi camera, TFT |
| **5. Start run** | **Press EC11 knob** | Only takes effect when homed. Calls `scheduler_set_target_um()` + `scheduler_start()`. | EC11 push-switch, both motors |
| **6. Running** | — | Closed loop: the scheduler strokes continuously, and after every stroke reads UAS + both turbidity channels + force, converts each through its own 9-point calibration curve, and fuses the trustworthy ones (median) into one size estimate — see `calibration.h`'s `calib_estimate_particle_size_um()`. TFT shows only a "Mixing…" loading screen regardless; raw sensor values and the live fused estimate are available over BLE (`FORCE ON`/`TURB ON`/`UAS ON`/`FIT`, see below) for diagnostics, not on-screen. | Both motors, UAS chain, turbidity sensors, HX711 x2, TMC2209 UART, TFT |
| **7. Stop** | **BTN1 (short press)** | Graceful stop — finishes the in-progress stroke, then holds. | BTN1, both motors |
| **7b. Emergency stop** | **BTN1 (held ≥800ms) OR automatic HX711 over-force** | Immediate — kills motor power mid-step, same code path (`scheduler_emergency_stop()`) regardless of trigger source. This is a hard safety threshold, separate from force's role as a fusion input — see `CALIBRATION.md` §4 and §8. | BTN1 or HX711 x2, both motors |
| **8. Target reached** | Automatic | The fused sensor estimate reads within `TARGET_TOLERANCE_UM` of the target for `FUSION_CONSECUTIVE_CHECKS_REQUIRED` consecutive stroke-checks in a row (debounced against a single noisy reading), and the scheduler stops itself. If the fusion never converges, a hard `MIXING_MAX_STROKES_SAFETY_CAP` stops the run anyway and flags it as a safety-cap stop, not a confirmed one (`scheduler_hit_safety_cap()`) — a sign to check calibration, not a normal completion. Either way, press-and-hold the encoder knob on the done screen (stage 4b) to independently confirm the achieved size with a camera capture. | — |

**Button/encoder summary** (current assignment, see `include/config.h` and `src/ui.cpp`):

| Control | Function |
|---|---|
| EC11 rotary | Adjust target particle size (50–1000µm), set-target screen only |
| EC11 push-switch (short press) | Confirm/start a run; return to set-target from the done/verify screens |
| EC11 push-switch (held ≥800ms) | Request an optional camera size verification (set-target or done screen) |
| BTN1 | **Dedicated stop button, no start function** — short press = graceful stop, held ≥800ms = emergency stop |

BTN1 was previously overloaded (v2.4/v2.5 firmware used it for start, and a second button for stop). As of this build it has exactly one job — this was a deliberate simplification once the encoder's own switch took over start/confirm, since a safety-critical button is easier to trust with one unambiguous function.

### Camera verification (optional, on-demand — independent of the fusion loop)

CV is **never a fusion input** (see above) — it's an **operator-triggered, single-shot check** the doctor can run any time from the set-target or done screens (encoder long-press), to independently confirm what the four onboard sensors already decided:

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

---

## BLE debug commands

Connect to `EMBO-Debug` over BLE and write commands to the Nordic UART RX characteristic. Responses appear on the TX characteristic (the same stream as `ble_log()` output).

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
| `TURB ON` / `TURB OFF` | `TURB ON` | Streams raw APDS9960 ALS + MAX30102 IR/RED counts, plus whether each sensor responded on the I2C bus. |
| `FUSION` | `FUSION` | **One-shot, not a stream.** Prints the CURRENT live value of all four fusion channels plus the resulting fused estimate and per-channel trust flags — this is the exact command to run during the 9-syringe bench session: hold a known-size syringe steady, run `FUSION`, copy the printed values into that syringe's row in `SENSOR_CAL_TABLE` (`calibration.cpp`). See `CALIBRATION.md` §5. |
| `CAPTURE` | `CAPTURE` | Manually triggers the same on-demand RPi capture+CV the UI's encoder long-press does — for bench testing without the physical knob. Result arrives as an `RPi: median=... iqr=...` log line once the RPi replies (or a timeout log after `RPI_CAPTURE_TIMEOUT_MS`). |
| `FIT` | `FIT` | Dumps the DIAGNOSTIC-ONLY breakage-model fit (`k`, `D0`, verification point count) plus the current run's stroke count, last fused estimate, and whether the safety cap was hit. Does not reflect what's actually driving the stop condition — that's `FUSION`. |
| `FIT RESET` | `FIT RESET` | Discards the accumulated breakage-model fit. Nothing calls this automatically — use when the material genuinely changes (`CALIBRATION.md` §5). |

Sending an unrecognised command prints the command list back.

### Automatic streaming

| Stream | Trigger | Format | Interval |
|---|---|---|---|
| UAS ADC | `UAS ON` active | `UAS: f0=900kHz(att=0.98) ... \| last CV capture: median=310 iqr=40` | 200ms |
| Force | `FORCE ON` active | `FORCE: ch1=12.3g ch2=11.8g` | 200ms |
| Turbidity | `TURB ON` active | `TURB: als=512(ok) ir=98234 red=87211(ok)` | 200ms |
| StallGuard | While a `MOVE` is running | `SG M1: 512` | 200ms |

StallGuard (`SG_RESULT`) streams automatically whenever a `MOVE` command is in progress — no separate command needed. Higher value = less load on the motor. Valid only while SpreadCycle is active (confirmed at boot via BLE log).

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

GPIO35 (MOSI) / GPIO36 (CLK) are shared between the TFT and the AD9833. There is no MISO on this bus as of v3.3 — touch's data-out (T_DO) moved to its own pin (GPIO46) and this UI doesn't use touch at all (encoder + one button only, see `ui.cpp`); the AD9833 has no readback register either.

| Device | CS | Mode | Max clock | Notes |
|---|---|---|---|---|
| ILI9341 TFT | GPIO39 | Mode 0 | 20 MHz | Via 20-pin IDC ribbon, driven by LovyanGFX (`LGFX_Config.h`) |
| AD9833 DDS | GPIO38 | **Mode 2** | ~10 MHz | Direct trace, no ribbon |

**Integration note (verify on first bring-up):** the TFT is driven by LovyanGFX, which owns `SPI2_HOST` directly via its own `Bus_SPI` instance — it does not go through the Arduino `SPI` (SPIClass) object the way the old TFT_eSPI setup implicitly did. `uas.cpp` still calls `SPI.begin()` for the AD9833 on the same physical MOSI/CLK GPIOs with a different CS. This is fine electrically, but the two software drivers haven't been confirmed not to contend over the same underlying SPI peripheral — see the comment in `uas.cpp`'s `uas_init()`. Move the AD9833 to `SPI3_HOST` if anything looks wrong on a scope.

## UAS multi-frequency sweep (July 2026)

`uas_update()` no longer holds a single fixed 1MHz tone. Per the [technical advisory](../../docs/EMBO_UAS_CV_Technical_Advisory.txt), a single-frequency attenuation reading isn't reliably invertible to particle size for a polydisperse population, so the AD9833 now sweeps `UAS_NUM_FREQUENCIES` (3, `config.h`) discrete tones every update cycle: 900kHz / 1MHz / 1.1MHz by default. This is a firmware/timing-only change — no board respin.

**UAS is one of the four closed-loop fusion inputs (`scheduler.cpp`), gated by its own monotonicity check.** Once `SENSOR_CAL_TABLE` (`calibration.cpp`) has real bench data, `calib_estimate_particle_size_um()` independently verifies UAS's 9-point calibration curve is actually monotonic before trusting it — if it isn't, that channel is silently excluded from the fusion rather than trusted anyway (see `FusedSizeEstimate.uasTrusted`, readable via the BLE `FUSION` command). CV remains a good *independent* cross-check regardless: connect over BLE, send `UAS ON`, trigger a few `CAPTURE`s across a real mixing run, and compare the per-frequency attenuation against each CV result (both printed on the same log line) — see `FIRMWARE_TODO.md` for the full gate.

Each library calls `SPI.beginTransaction()` with its own settings before every access — mode and clock switch automatically. Never hold one device's CS asserted while accessing another.

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

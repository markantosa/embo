# Firmware — ESP32-S3

Arduino/C++ firmware for the EMBO ESP32-S3-WROOM-1-N8 control board.
Built and flashed with PlatformIO inside VS Code.

## What this firmware does

| Module | File | Status |
|---|---|---|
| TMC2209 UART init, SpreadCycle, StallGuard | `src/motors.cpp` | ✅ |
| LEDC step generation, limit switch ISRs | `src/motors.cpp` | ✅ |
| Homing routine + stroke counter | `src/motors.cpp` | ✅ |
| AD9833 1MHz DDS init, UAS ADC calibration | `src/uas.cpp` | ✅ |
| RPi UART receive + packet parser | `src/rpi_uart.cpp` | ✅ |
| PID control loop | `src/pid.cpp` | ⚠️ motor mapping stub |
| TFT display + encoder + buttons + touch | `src/ui.cpp` | ⚠️ stub |
| NimBLE wireless debug UART | `src/ble_debug.cpp` | ✅ |

See [FIRMWARE_TODO.md](../FIRMWARE_TODO.md) for the full build-up task list and hardware checklist.

---

## Operational workflow

How a doctor moves through a run, and which hardware/firmware drives each stage.

| Stage | Trigger | What happens | Hardware involved |
|---|---|---|---|
| **1. Boot / self-test** | Power on | TMC2209 UART init + SpreadCycle write/readback, AD9833 + UAS baseline sample. Any `BLOCKING` check failing (see `FIRMWARE_TODO.md`) halts here — no silent bad-data path into PID. | Both TMC2209 modules, AD9833, status LED |
| **2. Homing** | Automatic after boot | `motors_home()` drives both motors to their limit switches and backs off; stroke counter resets to 0. | M1/M2 steppers, limit switches (J6/J7) |
| **3. Idle / ready** | — | TFT shows `READY`, current target setpoint, live PSD/IQR (`--` until first RPi packet). Doctor loads the syringe (interlock handled by mech team, not sensed by this firmware). | TFT |
| **4. Setpoint adjust** | Turn EC11 encoder | Adjusts target particle size in 5µm steps, clamped to 50–1000µm (`config.h`). Locked out once a run starts — turning the knob mid-run is a no-op. | EC11 encoder |
| **5. Start run** | **BTN1** | Only takes effect when idle *and* homed. Calls `pid_set_target_um()` + `pid_start()`, enables both motors. Short low chirp if pressed while not ready (not homed, or already running). | BTN1, both motors |
| **6. Running** | — | Closed-loop mixing: UAS attenuation + StallGuard (`SG_RESULT`) feed the PID alongside RPi camera `SIZE` packets. TFT refreshes at ~5Hz with live PSD, IQR, and stroke count. *(Planned, not yet built: a live PSD trend graph on this same screen — see `FIRMWARE_TODO.md` task 7.)* | Both motors, UAS chain, TMC2209 UART, RPi UART, TFT |
| **7. Stop / e-stop** | **BTN2** | Always live, independent of PID/run state — kills PWM and disables both motors directly, then stops the PID loop. Same button/behavior for a normal stop and for an emergency stop. | BTN2, both motors |
| **8. Target reached** | Automatic | PID detects median within `TARGET_TOLERANCE_UM` of the setpoint, stops the run itself (same effect as BTN2 minus the direct motor kill). TFT shows final PSD/IQR/stroke count. | — |

**Button/encoder summary** (current assignment, see `include/config.h` and `src/ui.cpp`):

| Control | Function |
|---|---|
| EC11 rotary | Adjust target particle size (50–1000µm), idle only |
| BTN1 | Start run (idle + homed only) |
| BTN2 | Stop / e-stop — always active |

Known gap: BTN2 currently has one behavior for both "stop after this stroke" and "true emergency stop." Worth revisiting before use on a real syringe.

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

**Recommended app:** Serial Bluetooth Terminal (Android) or nRF Toolbox (iOS/Android). Select the NUS service, subscribe to TX, and use the input bar to send commands.

### Commands

| Command | Example | Description |
|---|---|---|
| `HOME` | `HOME` | Drives both motors to their limit switches and backs off. Blocks until complete. Logs success or failure. |
| `MOVE <motor> <steps>` | `MOVE 1 400` | Moves motor 1 forward 400 steps at 500 Hz. Use negative steps for reverse: `MOVE 2 -200`. Stops automatically on completion or limit trip. |
| `UAS ON` | `UAS ON` | Starts streaming raw ADC readings from the envelope detector every 200ms. |
| `UAS OFF` | `UAS OFF` | Stops UAS streaming. |

Sending an unrecognised command prints the command list back.

### Automatic streaming

| Stream | Trigger | Format | Interval |
|---|---|---|---|
| UAS ADC | `UAS ON` active | `UAS: 1234 mV` | 200ms |
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

All three SPI devices share GPIO35 (MOSI) / GPIO36 (CLK) / GPIO37 (MISO). CS and mode differ per device.

| Device | CS | Mode | Max clock | Notes |
|---|---|---|---|---|
| ILI9341 TFT | GPIO39 | Mode 0 | 20 MHz | Via 20-pin IDC ribbon |
| XPT2046 touch | GPIO42 | Mode 0 | 2 MHz | Via ribbon, shared with TFT |
| AD9833 DDS | GPIO38 | **Mode 2** | ~10 MHz | Direct trace, no ribbon |

## UAS multi-frequency sweep (July 2026)

`uas_update()` no longer holds a single fixed 1MHz tone. Per the [technical advisory](../../docs/EMBO_UAS_CV_Technical_Advisory.txt), a single-frequency attenuation reading isn't reliably invertible to particle size for a polydisperse population, so the AD9833 now sweeps `UAS_NUM_FREQUENCIES` (3, `config.h`) discrete tones every update cycle: 900kHz / 1MHz / 1.1MHz by default. This is a firmware/timing-only change — no board respin.

**UAS is a secondary signal, not a PID input.** The RPi CV pipeline (`median_um`/`iqr_um` over UART) is the sole authoritative size measurement in `pid.cpp`. Before UAS attenuation is trusted for anything beyond raw diagnostic logging, run the correlation check: connect over BLE, send `UAS ON`, and log the per-frequency attenuation + live CV median/IQR (both now printed on the same line) across a real mixing run. Only treat it as a usable trend signal if that relationship is clean and monotonic — see `FIRMWARE_TODO.md` for the full gate.

Each library calls `SPI.beginTransaction()` with its own settings before every access — mode and clock switch automatically. Never hold one device's CS asserted while accessing another.

## UART assignment

| Peripheral | ESP32-S3 UART | GPIO | Baud |
|---|---|---|---|
| TMC2209 motor drivers | UART1 | GPIO4 (half-duplex) | 115200 |
| Raspberry Pi | UART2 | GPIO47 TX / GPIO48 RX | 921600 |
| USB monitor | USB CDC | GPIO19/20 (fixed) | 115200 |

UART1 and UART2 are independent peripherals — do not reassign one to the other's number.

## ADC

GPIO1 = ADC1_CH0. ADC1 is fully usable while BLE radio is active. ADC2 shares silicon with the RF block and cannot be used for analog while BLE is running — no ADC2 pins are used for analog in this design.

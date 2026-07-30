# PCB_Test_Firmware_v3_4_ESPIDF

**ESP-IDF-native variant** of `testing/PCB_Test_Firmware_v3_4/` — identical
behavior/pinout/protocol, built directly against `framework = espidf`
(PlatformIO) instead of the Arduino core. No `Arduino.h` anywhere in `src/`
or `include/`; every Arduino API used by the original (GPIO, timing, ADC,
I2C, SPI, LEDC, UART, BLE) was reimplemented against native ESP-IDF drivers.
See `testing/PCB_Test_Firmware_v3_4/` for the Arduino-framework sibling this
was ported from — keep both in sync if you change behavior, since they're
meant to be functionally identical.

## Notes before building

- **Build with `pio run`** (PlatformIO Core). First run downloads the
  ESP-IDF toolchain (~10+ minutes, several hundred MB) via
  `pioarduino`'s `framework-espidf` package (pinned to ESP-IDF 5.1.4 as of
  this port).
- **Windows + spaces in the path**: ESP-IDF's CMake build system rejects any
  whitespace in the full path (both the project path *and* the PlatformIO
  core/package directory, e.g. `C:\Users\Jane Doe\...`). If your checkout or
  `~/.platformio` lives under a spaced path, work around it with `subst`
  (map a drive letter to a space-free path) rather than a directory
  junction/symlink — junctions get transparently resolved back to the real
  (spaced) path by Windows and by Git, which reintroduces the problem (and,
  for junctions pointed straight at the project subfolder rather than the
  repo root, breaks `git`'s upward directory search for version info).
  `subst Z: "C:\Users\Jane Doe\path\to\repo"` (mapped to the repo root, not
  just the project folder) plus `PLATFORMIO_CORE_DIR` pointed at a similarly
  `subst`-mapped drive for `~/.platformio` is the combination that works.
- `version.txt` (checked into this project) pins `PROJECT_VER` so ESP-IDF's
  build system doesn't try `git describe` on the project directory — useful
  regardless of the whitespace/subst situation above, since this project
  directory isn't a git repo root itself.
- `sdkconfig.defaults` configures the native NimBLE BLE host (`CONFIG_BT_*`)
  and flash size/partition table (4MB single-app, matching the
  ESP32-S3-WROOM-1-N4 on this board). `sdkconfig.esp32-s3` is a generated
  build artifact (gitignored) — delete it and re-run `pio run` if sdkconfig
  ever gets out of sync with `sdkconfig.defaults`.
- **LovyanGFX** builds cleanly against ESP-IDF as-is — `src/hal/LGFX_Config.h`
  needed no framework-specific changes; LovyanGFX detects the absence of
  `ARDUINO` and uses its native ESP-IDF platform code path itself.
- The trickiest ports were the hand-rolled `src/hal/tmc2209_uart.*` TMC2209
  UART register driver (replacing TMCStepper) and the native NimBLE GATT
  server in `src/ble/ble_service.cpp` (replacing NimBLE-Arduino) — see
  comments in those files. Neither has been verified against real hardware
  (no board attached in this environment) — only that they compile and link
  cleanly and that the protocol-level bit positions/CRC/UUID-byte-order were
  worked out by hand against datasheet/library references.

---

Bring-up firmware + Web Bluetooth dashboard for the **v3.4** EMBO main board
(`docs/EMBO_PCB_Design_Brief_v3_4.txt`). Forked from `PCB_Test_Firmware`
(the v2.51-board test rig) with two additions:

- **Load-cell force sensing** — 2x HX711, shared clock, bit-banged driver
  (`src/hal/force_sensor.*`), per brief §10.
- **Turbidity sensing** — APDS9960 (ALS, 0x39) + MAX30102 (backscatter,
  0x57) on the shared I2C bus (`src/hal/turbidity.*`), per brief §9.

`src/hal/{motors,homing,tmc_uart_stream,bus_probe,uas_sensor}.*` and the BLE
transport are carried over from the v2.51 rig — v3.4's changes (220R GPIO
protection resistors, MS1/MS2 address jumpers, PDN pin4/5 correction, F2
backstop fuse, THT buttons/gain resistor) are passive/hardware hardening and
don't move any GPIO assignment this firmware depends on. `config.h` documents
the full v3.4 pin map used here.

BLE UUIDs and the advertised name (`EMBO-PCB-Test-v3.4`) are distinct from
the v2.51 rig's, so both boards' dashboards can be open side-by-side without
cross-connecting.

## Dashboard

Open `web/index.html` in Chrome or Edge (Web Bluetooth required) while the
board is powered and advertising. Every sensor on the v3.4 board shows both
its live numeric reading and a rolling strip-chart: UAS envelope, both load
cells, both turbidity channels, both StallGuard results, both limit
switches, and motor position/homing state. Motor jog/home controls are
carried over unchanged from the v2.51 dashboard.

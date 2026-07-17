# PCB_Test_Firmware_v3_4

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

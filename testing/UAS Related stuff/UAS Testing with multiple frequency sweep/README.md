# UAS Testing with multiple frequency sweep

Standalone fork of `testing/PCB_Test_Firmware_v3_4`, isolated into its own
project folder so this work can't interfere with the original bring-up rig.
Adds **runtime frequency control** for the AD9833 driving the UAS
transducer, which the parent project fixes to a single 1MHz tone.

## What's different from `PCB_Test_Firmware_v3_4`

- **New BLE characteristic, `FREQ_CMD`** (write, float32 LE Hz) —
  `src/ble/ble_service.h/.cpp` — applies a new AD9833 drive frequency
  immediately via `uasSetFrequency()` (`src/hal/uas_sensor.h/.cpp`).
- **`TelemetryPacket` gains a `uasFreqHz` field** (right after `uasVolts`)
  so the dashboard always knows which frequency a given voltage reading was
  taken at.
- **Distinct BLE identity** — advertises as `EMBO-UAS-Sweep` with its own
  UUID base (`8f6a2xxx...`), so it can never cross-connect with the parent
  rig's dashboard if both boards are somehow powered nearby.
- Everything else (motors, load cells, turbidity, homing) is carried over
  unchanged from the parent project, for reference/bring-up only — this
  rig's actual purpose is the UAS frequency sweep.

## Dashboard

Open `web/index_logging to record UAS csv data.html` in Chrome or Edge
(Web Bluetooth required) while the board is powered and advertising.

- **UAS Acoustic Chain** card shows live envelope voltage + current drive
  frequency.
- **Manual Frequency Set** card sets one frequency at a time.
- **Automated Frequency Sweep** card: enter start/end/step (Hz) and a
  dwell time per step (ms), click **Run Sweep** — it walks the range,
  settles, and averages the envelope reading at each step, then lets you
  download a `freq_hz, mean_uas_volts, std_uas_volts, n_samples` CSV.
- **Start Recording / Download CSV** (top bar) continuously logs every raw
  telemetry packet (now including `uas_freq_hz`) regardless of whether a
  sweep is running — same as the parent rig's logging page.

All other sensor cards (load cells, turbidity, motors/StallGuard) behave
identically to the parent project.

#include "ble_debug.h"
#include "ble_binary_telemetry.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "scheduler.h"
#include "calibration.h"
#include "rpi_uart.h"
#include "ui.h"
#include "config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Standard Nordic UART Service UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // notify (ESP32→phone)
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // write  (phone→ESP32)

// MOVE's step rate is now MOTOR_JOG_HZ (config.h) — shared with the UI's
// Settings > Motion > Jog screens, so both bench-move paths use one speed.

static NimBLECharacteristic *_tx_char = nullptr;
static bool _connected = false;
static bool _ble_enabled = false;  // advertising on/off — see ble_debug_set_enabled()

// ── Streaming state ───────────────────────────────────────────────────────────
// All diagnostic streams stay available over this link regardless of what
// the TFT is showing (which is deliberately just a loading screen while a
// run is in progress) — this is the one place raw sensor data can still be
// pulled from the board, for calibration data collection or bring-up.
static bool     _uas_streaming    = false;
static bool     _force_streaming  = false;
static bool     _turb_streaming   = false;
static uint32_t _stream_last_ms = 0;
static const uint32_t STREAM_INTERVAL_MS = 200;

// ── Pending move state ────────────────────────────────────────────────────────
static bool     _move_active      = false;
static uint8_t  _move_motor       = 0;
static uint32_t _move_end_ms      = 0;

// ── UAS bench frequency-sweep state (see UASFREQ/UASSWEEP below) ─────────────
// A sweep steps the AD9833 through a frequency range and reports mean/stddev
// envelope voltage at each step — for finding/verifying attenuation-vs-
// frequency behavior on the bench, e.g. before choosing production
// frequencies in config.h. Advanced from ble_debug_update() every loop
// iteration, never blocking, same rationale as the MOVE timer above: this
// board has a real-time e-stop path (force_sensor) that must never stall.
static bool     _uas_sweep_active        = false;
static float    _uas_sweep_cur_hz        = 0.0f;
static float    _uas_sweep_end_hz        = 0.0f;
static float    _uas_sweep_step_hz       = 0.0f;
static uint32_t _uas_sweep_dwell_ms      = 0;
static uint32_t _uas_sweep_step_start_ms = 0;
static float    _uas_sweep_sum           = 0.0f;
static float    _uas_sweep_sum_sq        = 0.0f;
static uint32_t _uas_sweep_n             = 0;

// ── Incoming command buffer ───────────────────────────────────────────────────
static char     _cmd_buf[64]  = {};
static bool     _cmd_pending  = false;

// ── BLE callbacks ─────────────────────────────────────────────────────────────

static void _uas_sweep_abort(const char *reason) {
    if (!_uas_sweep_active) return;
    _uas_sweep_active = false;
    uas_clear_manual_override();
    ble_log("UASSWEEP: aborted (%s)", reason);
}

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *) override {
        _connected = true;
    }
    void onDisconnect(NimBLEServer *s) override {
        _connected = false;
        // Stop any active debug move so motors don't run unattended after disconnect.
        if (_move_active) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
            _move_active = false;
        }
        _uas_streaming = false;
        _uas_sweep_abort("BLE disconnected");
        uas_clear_manual_override();
        if (_ble_enabled) s->startAdvertising();
    }
};

class RxCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string val = c->getValue();
        if (val.empty()) return;
        // Copy into command buffer — strip trailing CR/LF.
        size_t len = val.size();
        if (len >= sizeof(_cmd_buf)) len = sizeof(_cmd_buf) - 1;
        memcpy(_cmd_buf, val.data(), len);
        while (len > 0 && (_cmd_buf[len-1] == '\n' || _cmd_buf[len-1] == '\r')) len--;
        _cmd_buf[len] = '\0';
        _cmd_pending = true;
        // Processing happens in ble_debug_update() (main loop context),
        // not here in the BLE task, so motor/ADC calls are safe.
    }
};

// ── Command processing ────────────────────────────────────────────────────────

static void _handle_command(const char *cmd) {
    // HOME
    if (strcmp(cmd, "HOME") == 0) {
        if (scheduler_is_running()) {
            ble_log("HOME: refused — a run is in progress (stop it first)");
            return;
        }
        ble_log("CMD: homing...");
        bool ok = motors_home();
        ble_log(ok ? "HOME: done" : "HOME: FAILED — check limit switches");
        return;
    }

    // MOVE <motor 1|2> <steps>   (negative steps = reverse)
    int motor, steps;
    if (sscanf(cmd, "MOVE %d %d", &motor, &steps) == 2
            && (motor == 1 || motor == 2) && steps != 0) {
        if (scheduler_is_running()) {
            ble_log("MOVE: refused — a run is in progress (stop it first)");
            return;
        }
        if (_move_active) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
        }
        bool fwd = (steps > 0);
        uint32_t abs_steps = (uint32_t)(steps < 0 ? -steps : steps);
        uint32_t duration_ms = (abs_steps * 1000UL) / MOTOR_JOG_HZ;
        motor_set_dir(motor, fwd);
        motor_enable(motor, true);
        motor_clear_limit(motor);
        motor_set_speed(motor, MOTOR_JOG_HZ);
        _move_motor  = motor;
        _move_end_ms = millis() + duration_ms;
        _move_active = true;
        ble_log("MOVE: M%d %+d steps (~%lu ms)", motor, steps, duration_ms);
        return;
    }

    // UAS ON / UAS OFF
    if (strcmp(cmd, "UAS ON") == 0) {
        _uas_streaming = true;
        ble_log("UAS: streaming ON");
        return;
    }
    if (strcmp(cmd, "UAS OFF") == 0) {
        _uas_streaming = false;
        ble_log("UAS: streaming OFF");
        return;
    }

    // FORCE ON / FORCE OFF — raw HX711 counts + calibrated grams, both channels
    if (strcmp(cmd, "FORCE ON") == 0) {
        _force_streaming = true;
        ble_log("FORCE: streaming ON");
        return;
    }
    if (strcmp(cmd, "FORCE OFF") == 0) {
        _force_streaming = false;
        ble_log("FORCE: streaming OFF");
        return;
    }

    // TURB ON / TURB OFF — raw APDS9960 ALS + MAX30102 IR/RED counts
    if (strcmp(cmd, "TURB ON") == 0) {
        _turb_streaming = true;
        ble_log("TURB: streaming ON");
        return;
    }
    if (strcmp(cmd, "TURB OFF") == 0) {
        _turb_streaming = false;
        ble_log("TURB: streaming OFF");
        return;
    }

    // FIT — dump the scheduler's diagnostic-only breakage-model fit (k, D0,
    // point count) plus the current/last run's fused-estimate state. NOTE:
    // the breakage model does NOT drive the stop condition any more — the
    // live sensor fusion (FUSION command below) does. This is a cross-check.
    if (strcmp(cmd, "FIT") == 0) {
        ble_log("FIT (diagnostic): k=%.4f D0=%.1fum n=%u target=%uum strokes=%lu "
                "last_fused=%.1fum(chans=%u) safety_cap_hit=%s",
                scheduler_get_fit_k(), scheduler_get_fit_d0_um(),
                scheduler_get_fit_num_points(), scheduler_get_target_um(),
                (unsigned long)scheduler_get_strokes_done(),
                scheduler_get_last_fused_size_um(), scheduler_get_last_fused_num_channels(),
                scheduler_hit_safety_cap() ? "yes" : "no");
        return;
    }

    // FIT RESET — discard the accumulated (diagnostic-only) breakage-model
    // fit. Manual only (nothing calls this automatically, see
    // calibration.h) — use when the material genuinely changes, per
    // CALIBRATION.md §7.
    if (strcmp(cmd, "FIT RESET") == 0) {
        calib_breakage_reset();
        ble_log("FIT: reset — back to fallback constants until new verification points arrive");
        return;
    }

    // FUSION — dumps the CURRENT live reading of all four sensor-fusion
    // channels plus the resulting fused estimate, exactly the numbers
    // needed to fill in one row of SENSOR_CAL_TABLE (calibration.cpp) during
    // the 9-syringe bench calibration session: hold this exact syringe
    // steady, run `FUSION`, and copy the printed uas/apds/max/force values
    // straight into that syringe's row alongside its known size. See
    // CALIBRATION.md §5.
    if (strcmp(cmd, "FUSION") == 0) {
        float uasAtten = 0.0f;
        uint8_t numFreq = uas_get_num_frequencies();
        for (uint8_t i = 0; i < numFreq; i++) uasAtten += uas_get_attenuation(i);
        if (numFreq > 0) uasAtten /= (float)numFreq;
        float apdsRatio = calib_turbidity_ratio_als(turbidity_get_als_clear());
        float maxRatio = calib_turbidity_ratio_backscatter_ir(turbidity_get_ir_raw());
        float forceG = (force_sensor_get_grams_1() + force_sensor_get_grams_2()) / 2.0f;

        FusedSizeEstimate fused = calib_estimate_particle_size_um(uasAtten, apdsRatio, maxRatio, forceG);

        ble_log("FUSION: uas=%.4f apds=%.4f max=%.4f force=%.1fg", uasAtten, apdsRatio, maxRatio, forceG);
        ble_log("FUSION: estimate=%.1fum channels=%u (uas=%s apds=%s max=%s force=%s)",
                fused.sizeUm, fused.numChannelsUsed,
                fused.uasTrusted ? "trusted" : "excluded", fused.turbApdsTrusted ? "trusted" : "excluded",
                fused.turbMaxTrusted ? "trusted" : "excluded", fused.forceTrusted ? "trusted" : "excluded");
        return;
    }

    // CAPTURE — manually trigger the same on-demand RPi capture+CV the UI's
    // encoder long-press does (see ui.cpp), for bench testing without the
    // physical knob. Result appears via the next "RPi: median=... iqr=..."
    // log line once the RPi replies.
    if (strcmp(cmd, "CAPTURE") == 0) {
        bool sent = rpi_request_capture();
        ble_log(sent ? "CAPTURE: requested" : "CAPTURE: already pending, ignored");
        return;
    }

    // MENU — open the TFT settings/diagnostics menu, for bench testing
    // before a physical trigger is wired up (see ui.h / bench_diagnostics_menu.h).
    if (strcmp(cmd, "MENU") == 0) {
        ui_open_bench_diagnostics_menu();
        ble_log("MENU: opened");
        return;
    }

    // UASFREQ <hz> — manually retune the AD9833 and hold it there, bypassing
    // the production UAS_NUM_FREQUENCIES cycle. For finding/verifying
    // attenuation-vs-frequency behavior on the bench (see
    // testing/UAS_Testing_with_multiple_frequency_sweep). uas_get_attenuation()
    // keeps reporting its last production reading, unchanged, the whole
    // time — this only affects uas_read_mv() / the UAS streaming line below.
    float freqHz;
    if (sscanf(cmd, "UASFREQ %f", &freqHz) == 1) {
        if (scheduler_is_running()) {
            ble_log("UASFREQ: refused — a run is in progress (stop it first)");
            return;
        }
        if (freqHz <= 0.0f) {
            ble_log("UASFREQ: invalid frequency");
            return;
        }
        _uas_sweep_abort("manual UASFREQ issued");
        uas_set_manual_override_hz(freqHz);
        ble_log("UASFREQ: set %.0f Hz, reading=%lu mV", freqHz, (unsigned long)uas_read_mv());
        return;
    }
    if (strcmp(cmd, "UASFREQ CLEAR") == 0) {
        _uas_sweep_abort("UASFREQ CLEAR issued");
        uas_clear_manual_override();
        ble_log("UASFREQ: cleared, back to production %u-freq cycle", uas_get_num_frequencies());
        return;
    }

    // UASSWEEP <start_hz> <end_hz> <step_hz> <dwell_ms> — walks the range,
    // settles, and reports mean/stddev envelope voltage at each step. See
    // UASSWEEP STOP to cancel early. Non-blocking — advances a step at a
    // time from ble_debug_update(), same as MOVE above.
    float startHz, endHz, stepHz;
    unsigned long dwellMs;
    if (sscanf(cmd, "UASSWEEP %f %f %f %lu", &startHz, &endHz, &stepHz, &dwellMs) == 4) {
        if (scheduler_is_running()) {
            ble_log("UASSWEEP: refused — a run is in progress (stop it first)");
            return;
        }
        if (startHz <= 0.0f || endHz < startHz || stepHz <= 0.0f || dwellMs == 0) {
            ble_log("UASSWEEP: invalid range");
            return;
        }
        _uas_sweep_active        = true;
        _uas_sweep_cur_hz        = startHz;
        _uas_sweep_end_hz        = endHz;
        _uas_sweep_step_hz       = stepHz;
        _uas_sweep_dwell_ms      = (uint32_t)dwellMs;
        _uas_sweep_sum           = 0.0f;
        _uas_sweep_sum_sq        = 0.0f;
        _uas_sweep_n             = 0;
        uas_set_manual_override_hz(_uas_sweep_cur_hz);
        _uas_sweep_step_start_ms = millis();
        ble_log("UASSWEEP: started %.0f-%.0f Hz, step %.0f Hz, dwell %lu ms",
                startHz, endHz, stepHz, dwellMs);
        return;
    }
    if (strcmp(cmd, "UASSWEEP STOP") == 0) {
        _uas_sweep_abort("stopped by command");
        return;
    }

    ble_log("CMD unknown: \"%s\"", cmd);
    ble_log("Commands: HOME | MOVE <1|2> <steps> | UAS ON|OFF | FORCE ON|OFF | TURB ON|OFF | FUSION | FIT | FIT RESET | CAPTURE | MENU | UASFREQ <hz>|CLEAR | UASSWEEP <start> <end> <step> <dwell_ms>|STOP");
}

// ── Public API ────────────────────────────────────────────────────────────────

void ble_debug_init() {
    NimBLEDevice::init("EMBO-Debug");
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCB());
    ble_binary_telemetry_init(server);

    NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

    _tx_char = svc->createCharacteristic(NUS_TX_UUID,
                   NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rx_char = svc->createCharacteristic(NUS_RX_UUID,
                   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx_char->setCallbacks(new RxCB());

    svc->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(NUS_SERVICE_UUID);
    pAdvertising->setName("EMBO-Debug");

    // Deliberately NOT starting advertising here — BLE is off by default,
}

void ble_debug_set_enabled(bool enabled) {
    if (enabled == _ble_enabled) return;
    _ble_enabled = enabled;
    if (enabled) {
        NimBLEDevice::startAdvertising();
    } else {
        NimBLEDevice::stopAdvertising();
    }
    ble_log("BLE: %s", enabled ? "advertising enabled (EMBO-Debug)" : "advertising disabled");
}

bool ble_debug_is_enabled() { return _ble_enabled; }

void ble_debug_update() {
    ble_binary_telemetry_update();

    // Process any pending command from the BLE RX callback.
    if (_cmd_pending) {
        _cmd_pending = false;
        _handle_command(_cmd_buf);
    }

    // Finish a timed MOVE when duration elapses or limit trips.
    if (_move_active) {
        if (millis() >= _move_end_ms || motor_limit_hit(_move_motor)) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
            _move_active = false;
            ble_log("MOVE: done (M%d)", _move_motor);
        }
    }

    // Belt-and-braces: scheduler_start() already clears any UAS override
    // itself (see scheduler.cpp), but if a run somehow starts while a bench
    // sweep is mid-flight, stop it here too rather than let it keep
    // retuning the DDS underneath an active mix.
    if (scheduler_is_running() && (_uas_sweep_active || uas_has_manual_override())) {
        _uas_sweep_abort("a run started");
        uas_clear_manual_override();
    }

    // Advance the UAS bench sweep, if one is running — every loop
    // iteration, not gated by STREAM_INTERVAL_MS below, so the dwell timing
    // and sample count at each step stay accurate regardless of streaming
    // state.
    if (_uas_sweep_active) {
        float mv = (float)uas_read_mv();
        _uas_sweep_sum    += mv;
        _uas_sweep_sum_sq += mv * mv;
        _uas_sweep_n++;

        if (millis() - _uas_sweep_step_start_ms >= _uas_sweep_dwell_ms) {
            float mean = (_uas_sweep_n > 0) ? (_uas_sweep_sum / _uas_sweep_n) : 0.0f;
            float variance = (_uas_sweep_n > 0) ? (_uas_sweep_sum_sq / _uas_sweep_n) - (mean * mean) : 0.0f;
            if (variance < 0.0f) variance = 0.0f;  // guard tiny negative from float rounding
            ble_log("SWEEP: freq_hz=%.0f mean_mv=%.1f std_mv=%.2f n=%lu",
                    _uas_sweep_cur_hz, mean, sqrtf(variance), (unsigned long)_uas_sweep_n);

            _uas_sweep_cur_hz += _uas_sweep_step_hz;
            if (_uas_sweep_cur_hz > _uas_sweep_end_hz + 0.5f) {
                _uas_sweep_active = false;
                uas_clear_manual_override();
                ble_log("UASSWEEP: done");
            } else {
                uas_set_manual_override_hz(_uas_sweep_cur_hz);
                _uas_sweep_step_start_ms = millis();
                _uas_sweep_sum = _uas_sweep_sum_sq = 0.0f;
                _uas_sweep_n = 0;
            }
        }
    }

    // Periodic streaming.
    uint32_t now = millis();
    if (now - _stream_last_ms < STREAM_INTERVAL_MS) return;
    _stream_last_ms = now;

    if (_uas_streaming) {
        // Per-frequency attenuation alongside the most recent CV capture (if
        // any — CV is on-demand now, not continuous, see rpi_uart.h). UAS is
        // one of the four live sensor-fusion channels (scheduler.cpp) once
        // SENSOR_CAL_TABLE is filled in and this channel passes its
        // monotonicity check — trigger captures with `CAPTURE` (or the UI's
        // encoder long-press) while this is running as an independent
        // cross-check against CV, per the July 2026 technical advisory.
        char line[144];
        int n;
        if (uas_has_manual_override()) {
            n = snprintf(line, sizeof(line), "UAS: [BENCH OVERRIDE] reading=%lu mV", (unsigned long)uas_read_mv());
        } else {
            n = snprintf(line, sizeof(line), "UAS:");
            for (uint8_t i = 0; i < uas_get_num_frequencies() && n < (int)sizeof(line); i++) {
                n += snprintf(line + n, sizeof(line) - n, " f%u=%.0fkHz(att=%.3f)",
                              i, uas_get_frequency_hz(i) / 1000.0f, uas_get_attenuation(i));
            }
        }
        if (n >= (int)sizeof(line)) n = sizeof(line) - 1;  // snprintf can report more than it wrote
        int16_t median = rpi_get_median_um();
        int16_t iqr     = rpi_get_iqr_um();
        snprintf(line + n, sizeof(line) - n, " | last CV capture: median=%d iqr=%d",
                 median, iqr);
        ble_log("%s", line);
    }

    if (_move_active) {
        ble_log("SG M%d: %u", _move_motor, motor_sg_result(_move_motor));
    }

    if (_force_streaming) {
        ble_log("FORCE: ch1=%.1fg ch2=%.1fg", force_sensor_get_grams_1(), force_sensor_get_grams_2());
    }

    if (_turb_streaming) {
        ble_log("TURB: als=%u(%s) ir=%lu red=%lu(%s)",
                turbidity_get_als_clear(), turbidity_apds_ok() ? "ok" : "NOT FOUND",
                (unsigned long)turbidity_get_ir_raw(), (unsigned long)turbidity_get_red_raw(),
                turbidity_max_ok() ? "ok" : "NOT FOUND");
    }
}

void ble_log(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Always mirror to Serial, regardless of BLE connection state — most
    // bench testing/debugging happens over USB serial, not BLE, and this
    // was previously a complete no-op with nothing connected (see
    // _connected check below), which silently swallowed every log call
    // in the codebase (homing, scheduler, TMC init, everything) whenever
    // nobody happened to have a BLE client open.
    Serial.println(buf);

    if (!_connected || !_tx_char) return;
    _tx_char->setValue((uint8_t *)buf, strlen(buf));
    _tx_char->notify();
}

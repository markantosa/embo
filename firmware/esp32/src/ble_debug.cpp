#include "ble_debug.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "scheduler.h"
#include "calibration.h"
#include "rpi_uart.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

// Standard Nordic UART Service UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // notify (ESP32→phone)
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // write  (phone→ESP32)

#define DEBUG_STEP_HZ     500   // step rate used for MOVE commands

static NimBLECharacteristic *_tx_char = nullptr;
static bool _connected = false;

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

// ── Incoming command buffer ───────────────────────────────────────────────────
static char     _cmd_buf[64]  = {};
static bool     _cmd_pending  = false;

// ── BLE callbacks ─────────────────────────────────────────────────────────────

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
        s->startAdvertising();
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
        ble_log("CMD: homing...");
        bool ok = motors_home();
        ble_log(ok ? "HOME: done" : "HOME: FAILED — check limit switches");
        return;
    }

    // MOVE <motor 1|2> <steps>   (negative steps = reverse)
    int motor, steps;
    if (sscanf(cmd, "MOVE %d %d", &motor, &steps) == 2
            && (motor == 1 || motor == 2) && steps != 0) {
        if (_move_active) {
            motor_set_speed(_move_motor, 0);
            motor_enable(_move_motor, false);
        }
        bool fwd = (steps > 0);
        uint32_t abs_steps = (uint32_t)(steps < 0 ? -steps : steps);
        uint32_t duration_ms = (abs_steps * 1000UL) / DEBUG_STEP_HZ;
        motor_set_dir(motor, fwd);
        motor_enable(motor, true);
        motor_clear_limit(motor);
        motor_set_speed(motor, DEBUG_STEP_HZ);
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

    ble_log("CMD unknown: \"%s\"", cmd);
    ble_log("Commands: HOME | MOVE <1|2> <steps> | UAS ON|OFF | FORCE ON|OFF | TURB ON|OFF | FUSION | FIT | FIT RESET | CAPTURE");
}

// ── Public API ────────────────────────────────────────────────────────────────

void ble_debug_init() {
    NimBLEDevice::init("EMBO-Debug");
    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCB());

    NimBLEService *svc = server->createService(NUS_SERVICE_UUID);

    _tx_char = svc->createCharacteristic(NUS_TX_UUID,
                   NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic *rx_char = svc->createCharacteristic(NUS_RX_UUID,
                   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx_char->setCallbacks(new RxCB());

    svc->start();
    NimBLEDevice::startAdvertising();
}

void ble_debug_update() {
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
        char line[128];
        int n = snprintf(line, sizeof(line), "UAS:");
        for (uint8_t i = 0; i < uas_get_num_frequencies() && n < (int)sizeof(line); i++) {
            n += snprintf(line + n, sizeof(line) - n, " f%u=%.0fkHz(att=%.3f)",
                          i, uas_get_frequency_hz(i) / 1000.0f, uas_get_attenuation(i));
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
    if (!_connected || !_tx_char) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    _tx_char->setValue((uint8_t *)buf, strlen(buf));
    _tx_char->notify();
}

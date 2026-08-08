#include <Arduino.h>
#include "esp_log.h"
#include "config.h"
#include "data_logger.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "calibration.h"
#include "ui.h"
#include "rpi_uart.h"
#include "ble_debug.h"
#include "scheduler.h"

void setup() {
    Serial.begin(115200);
    Serial.println("BOOT: starting");

    // Silences the "E (...) i2c.master: ..." spam from the turbidity bus
    // (expected/benign if those sensors aren't physically connected — see
    // turbidity.cpp's retry logic) at the source, rather than needing to
    // grep it out of the serial log afterward. Doesn't affect anything
    // else's logging (targeted at this one driver tag), and doesn't
    // change turbidity_update()'s actual retry behavior at all — this is
    // purely cosmetic, quieting the console.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    data_logger_init();
    Serial.println("BOOT: data_logger_init done");

    motors_init();
    Serial.println("BOOT: motors_init done");
    uas_init();
    Serial.println("BOOT: uas_init done");
    // TURBIDITY DISABLED FOR NOW — sensors aren't physically connected on
    // the current bench setup, and calling this was the source of the
    // continuous I2C bus errors. turbidity_get_als_clear()/get_ir_raw()
    // etc. (turbidity.cpp) default to 0 and stay there with this disabled
    // — scheduler.cpp's fusion math (one of its four inputs) will just see
    // 0 permanently instead of a real reading, same as it effectively was
    // anyway with the sensors unconnected. Re-enable by uncommenting these
    // two lines (here and in loop() below) once the sensors are wired up.
    // turbidity_init();
    // Serial.println("BOOT: turbidity_init done");
    force_sensor_init();   // fusion input AND independent e-stop safety input, see calibration.h
    Serial.println("BOOT: force_sensor_init done");
    force_sensor_tare(); //fresh zero after every boot
    Serial.println("BOOT: force_sensor_tare done");
    ui_init();
    Serial.println("BOOT: ui_init done");
    rpi_uart_init();
    Serial.println("BOOT: rpi_uart_init done");
    ble_debug_init();
    Serial.println("BOOT: ble_debug_init done (BLE off by default — enable via Settings > Developer mode > UAS debug mode)");
    scheduler_init();
    Serial.println("BOOT: scheduler_init done");

    // Deliberately NOT homing here. Startup only brings hardware/UI up —
    // homing is an explicit operator action from the set-target screen
    // (short-press the knob while it reads "NOT HOMED", see
    // mixing_menu_screen.cpp) or the BLE `HOME` command. scheduler_start()
    // independently refuses to start a run while !motors_is_homed(), so
    // there's no path to running un-homed regardless of when homing happens.
    Serial.println("BOOT: setup() complete, entering loop() (not homed yet — home from the UI or BLE)");

    // Load cell data over Serial, ~20Hz — see the CSV header printed once
    // in setup(). Deliberately NOT gated on BLE/ble_log() at all, unlike
    // everything else in this file — the point is to have this work even
    // if BLE is completely disconnected or misbehaving. Converted to
    // grams via calib_hx711_to_grams() (real measured scale factors, see
    // calibration.h) — not raw counts.
    Serial.println("LC,millis_ms,grams1,grams2");
}

void loop() {
    rpi_uart_update();
    uas_update();
    // turbidity_update();  // disabled for now — see the comment in setup()

    // force_sensor_update() still runs every loop — it's still one of the
    // four fusion inputs the scheduler uses for size estimation (see
    // scheduler.h/calibration.h). The automatic load-cell e-stop that used
    // to trigger scheduler_emergency_stop() based on force_sensor_estop_tripped()
    // has been deliberately removed — force_sensor_estop_tripped() and
    // HX711_ESTOP_GRAMS (calibration.h) still exist but nothing calls them
    // anymore. No automatic overforce/jam protection remains; BTN1 (held
    // >=800ms) is a manual e-stop only, requiring an operator to act.
    force_sensor_update();

    // Load cell data over Serial, ~20Hz — see the CSV header printed once
    // in setup(). Deliberately NOT gated on BLE/ble_log() at all, unlike
    // everything else in this file — the point is to have this work even
    // if BLE is completely disconnected or misbehaving.
    static uint32_t lastForcePrintMs = 0;
    if (millis() - lastForcePrintMs >= 50) {
        lastForcePrintMs = millis();
        float grams1 = calib_hx711_to_grams(0, force_sensor_get_raw_1());
        float grams2 = calib_hx711_to_grams(1, force_sensor_get_raw_2());
        Serial.printf("LC,%lu,%.2f,%.2f\n", millis(), grams1, grams2);
        data_logger_log(millis(), grams1, grams2);  // no-op unless a BLE-triggered capture session is active
    }

    ui_update();
    ble_debug_update();
    scheduler_update();
}

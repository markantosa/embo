#include <Arduino.h>
#include "config.h"
#include "motors.h"
#include "uas.h"
#include "turbidity.h"
#include "force_sensor.h"
#include "ui.h"
#include "rpi_uart.h"
#include "ble_debug.h"
#include "scheduler.h"

void setup() {
    Serial.begin(115200);

    motors_init();
    uas_init();
    turbidity_init();     // one of four closed-loop fusion inputs once calibrated, see scheduler.h
    force_sensor_init();   // fusion input AND independent e-stop safety input, see calibration.h
    ui_init();
    rpi_uart_init();
    ble_debug_init();
    scheduler_init();

    // Homing must complete before the main loop. If it fails, halt on the UI
    // error screen rather than silently letting the doctor try to start a
    // run with an un-homed, un-trustworthy stroke position — scheduler_start()
    // also independently refuses to start while !motors_is_homed().
    if (!motors_home()) {
        ui_show_error("HOMING FAILED - check limit switches");
    }
}

void loop() {
    rpi_uart_update();
    uas_update();
    turbidity_update();

    // Automatic e-stop: force is ALSO a safety input, independent of its
    // role as one of the four fusion channels (see calibration.h,
    // scheduler.h) — this threshold applies regardless of what the fused
    // size estimate says, routed into the exact same kill path as the
    // button e-stop, not a separate one.
    if (force_sensor_update() && force_sensor_estop_tripped()) {
        scheduler_emergency_stop();
    }

    ui_update();
    ble_debug_update();
    scheduler_update();
}

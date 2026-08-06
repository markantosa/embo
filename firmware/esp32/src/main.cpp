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
    Serial.println("BOOT: starting");

    motors_init();
    Serial.println("BOOT: motors_init done");
    uas_init();
    Serial.println("BOOT: uas_init done");
    turbidity_init();     // one of four closed-loop fusion inputs once calibrated, see scheduler.h
    Serial.println("BOOT: turbidity_init done");
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

    static uint32_t lastLimitPrintMs = 0;
    if (millis() - lastLimitPrintMs >= 200){
        lastLimitPrintMs = millis();
        Serial.printf("LIMIT M1=%s M2=%s\n",
                        digitalRead(PIN_LIMIT_M1)==LOW ? "CLOSED":"open",
                        digitalRead(PIN_LIMIT_M2)==LOW ? "CLOSED":"open");
    }
}

#include <Arduino.h>
#include "config.h"
#include "motors.h"
#include "uas.h"
#include "ui.h"
#include "rpi_uart.h"
#include "ble_debug.h"
#include "pid.h"

void setup() {
    Serial.begin(115200);

    motors_init();
    uas_init();
    ui_init();
    rpi_uart_init();
    ble_debug_init();
    pid_init();

    // Homing must complete before the main loop. If it fails, the system
    // stays idle — pid_update() guards against !motors_is_homed().
    motors_home();
}

void loop() {
    rpi_uart_update();
    uas_update();
    ui_update();
    ble_debug_update();
    pid_update();
}

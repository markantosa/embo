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

// --Below includes for ui--
#include "hal/LGFX_Config.h"          // was: #include <Adafruit_ILI9341.h>
#include "hal/button_driver.h"
#include "hal/encoder_driver.h"
#include "hal/buzzer_driver.h"
#include "backend/menu_logic.h"
#include "backend/percentage_logic.h"
#include "ui/screen_main_menu.cpp"
#include "ui/screen_percentage.cpp"
#include "ui/boot_logo.h"
#include "ui/app_state_machine.h"

ButtonDriver btn1;
ButtonDriver encoderBtn;
EncoderDriver encoder;
PercentageLogic percentage;
ScreenPercentage percentScreen;
MenuLogic menu;
ScreenMenu menuScreen;
LGFX tft;                              // was: Adafruit_ILI9341 tft(TFT_CS, ...)
BuzzerDriver buzzer(PIN_BUZ_PWM);

AppStateMachine appState;

//FOR SHOWING EMBO BOOT LOGO
void showBootLogo() {
    int16_t x = (tft.width()  - LOGO_WIDTH)  / 2;
    int16_t y = (tft.height() - LOGO_HEIGHT) / 2;
    tft.pushImage(x, y, LOGO_WIDTH, LOGO_HEIGHT, epd_bitmap_embo_logoembologo320240); // was: drawRGBBitmap
    delay(5000);
}

void setup() {
    Serial.begin(115200);
    tft.init();              // was: tft.begin(20000000)
    tft.setRotation(3);      // same API, no change

    btn1.begin(PIN_BTN1);
    encoderBtn.begin(PIN_EC11_SW);
    encoder.begin(PIN_EC11_A, PIN_EC11_B);
    buzzer.begin();
  
    percentage.begin(0);

    showBootLogo();
    buzzer.tone(523,150);

    menu.begin({
        {"Start Mixing", 1},
        {"Calibrate",    2},
        {"Settings",     3}
    });

    appState.begin(tft, btn1, encoder, encoderBtn, percentage, menu, menuScreen, percentScreen);

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

const unsigned long FRAME_DELAY = 33; 
unsigned long lastFrameTime = 0;

void loop() {
    rpi_uart_update();
    uas_update();
    turbidity_update();
    buzzer.update();
    unsigned long currentMillis = millis();

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
    if (currentMillis - lastFrameTime >= FRAME_DELAY) {
    lastFrameTime = currentMillis;

    // Execute your display update code here
    appState.update();
  }
}

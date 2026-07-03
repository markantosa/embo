#include "ui.h"
#include "config.h"
#include "pid.h"
#include "motors.h"
#include "rpi_uart.h"
#include <Arduino.h>
// TFT_eSPI: copy User_Setup.h below into the library config, or set via build flags
#include <TFT_eSPI.h>

// TFT_eSPI pins are set via build_flags in platformio.ini (see User_Setup_Select.h).
// See include/tft_user_setup.h for the pin defines pushed via -D flags.
static TFT_eSPI _tft;

static void encoder_init();
static void buttons_init();
static void buzzer_init();
static void handle_encoder();
static void handle_buttons();
static void beep(uint32_t freq_hz, uint32_t duration_ms);
static void draw_screen(bool force);
static void IRAM_ATTR isr_encoder();

// Quadrature decode — written only from ISR, consumed+cleared in ui_update().
static volatile int32_t _encoder_delta = 0;
static volatile uint8_t _encoder_last_ab = 0;

static uint16_t _target_um = TARGET_SIZE_UM_DEFAULT;

// Button debounce state.
static const uint32_t BTN_DEBOUNCE_MS = 30;
static bool _btn1_last = HIGH, _btn2_last = HIGH;
static uint32_t _btn1_change_ms = 0, _btn2_change_ms = 0;

void ui_init() {
    encoder_init();
    buttons_init();
    buzzer_init();

    _tft.init();
    _tft.setRotation(1);
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setTextSize(2);
    _tft.setCursor(10, 10);
    _tft.print("EMBO");
    delay(500);
    draw_screen(true);
}

void ui_update() {
    handle_encoder();
    handle_buttons();
    draw_screen(false);
}

static void encoder_init() {
    pinMode(PIN_EC11_A, INPUT_PULLUP);
    pinMode(PIN_EC11_B, INPUT_PULLUP);
    pinMode(PIN_EC11_SW, INPUT_PULLUP);

    _encoder_last_ab = (digitalRead(PIN_EC11_A) << 1) | digitalRead(PIN_EC11_B);

    attachInterrupt(digitalPinToInterrupt(PIN_EC11_A), isr_encoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_EC11_B), isr_encoder, CHANGE);
}

static void IRAM_ATTR isr_encoder() {
    uint8_t ab = (digitalRead(PIN_EC11_A) << 1) | digitalRead(PIN_EC11_B);
    uint8_t transition = (_encoder_last_ab << 2) | ab;
    // Standard 2-bit gray code quadrature table: valid CW/CCW transitions only.
    switch (transition) {
        case 0b0001: case 0b0111: case 0b1110: case 0b1000:
            _encoder_delta++; break;
        case 0b0010: case 0b1011: case 0b1101: case 0b0100:
            _encoder_delta--; break;
        default: break;  // bounce / invalid transition — ignore
    }
    _encoder_last_ab = ab;
}

static void buttons_init() {
    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_BTN2, INPUT_PULLUP);
}

static void buzzer_init() {
    // LEDC channel LEDC_CH_BUZ on PIN_BUZ_PWM
    ledcSetup(LEDC_CH_BUZ, 1000, 8);
    ledcAttachPin(PIN_BUZ_PWM, LEDC_CH_BUZ);
    ledcWrite(LEDC_CH_BUZ, 0);   // silent
}

static void beep(uint32_t freq_hz, uint32_t duration_ms) {
    ledcWriteTone(LEDC_CH_BUZ, freq_hz);
    delay(duration_ms);
    ledcWriteTone(LEDC_CH_BUZ, 0);
}

static void handle_encoder() {
    if (pid_is_running()) {
        // Setpoint is locked during a run (pid_set_target_um() no-ops anyway) —
        // drop any accumulated delta so it doesn't jump the moment the run ends.
        _encoder_delta = 0;
        return;
    }

    noInterrupts();
    int32_t delta = _encoder_delta;
    _encoder_delta = 0;
    interrupts();

    if (delta == 0) return;

    int32_t new_target = (int32_t)_target_um + delta * TARGET_SIZE_UM_STEP;
    if (new_target < TARGET_SIZE_UM_MIN) new_target = TARGET_SIZE_UM_MIN;
    if (new_target > TARGET_SIZE_UM_MAX) new_target = TARGET_SIZE_UM_MAX;
    _target_um = (uint16_t)new_target;
    pid_set_target_um(_target_um);
}

static void handle_buttons() {
    uint32_t now = millis();

    bool btn1 = digitalRead(PIN_BTN1);
    if (btn1 != _btn1_last && (now - _btn1_change_ms) > BTN_DEBOUNCE_MS) {
        _btn1_change_ms = now;
        _btn1_last = btn1;
        if (btn1 == LOW) {  // pressed (active low)
            if (!pid_is_running() && motors_is_homed()) {
                pid_set_target_um(_target_um);
                pid_start();
                beep(1500, 100);
            } else {
                beep(400, 50);  // not ready — short low chirp
            }
        }
    }

    // BTN2 = stop / e-stop. Always live, not gated on run state — a doctor
    // needs this to work even if PID/motors are in an unexpected state.
    bool btn2 = digitalRead(PIN_BTN2);
    if (btn2 != _btn2_last && (now - _btn2_change_ms) > BTN_DEBOUNCE_MS) {
        _btn2_change_ms = now;
        _btn2_last = btn2;
        if (btn2 == LOW) {
            motor_set_speed(1, 0);
            motor_set_speed(2, 0);
            motor_enable(1, false);
            motor_enable(2, false);
            pid_stop();
            beep(2500, 200);
        }
    }
}

static void draw_screen(bool force) {
    static uint32_t last_draw_ms = 0;
    static int16_t last_median = -2, last_iqr = -2;
    static uint16_t last_target = 0;
    static bool last_running = false;

    uint32_t now = millis();
    if (!force && (now - last_draw_ms) < 200) return;  // ~5Hz refresh
    last_draw_ms = now;

    int16_t median = rpi_get_median_um();
    int16_t iqr    = rpi_get_iqr_um();
    bool running   = pid_is_running();

    if (!force && median == last_median && iqr == last_iqr
        && _target_um == last_target && running == last_running) {
        return;  // nothing changed, skip redraw to avoid flicker
    }
    last_median = median; last_iqr = iqr; last_target = _target_um; last_running = running;

    _tft.fillScreen(TFT_BLACK);
    _tft.setTextSize(2);

    _tft.setCursor(10, 10);
    _tft.setTextColor(running ? TFT_GREEN : TFT_YELLOW, TFT_BLACK);
    _tft.print(running ? "RUNNING" : (motors_is_homed() ? "READY" : "HOMING"));

    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.setCursor(10, 40);
    _tft.printf("Target: %u um", _target_um);

    _tft.setCursor(10, 70);
    if (median < 0) {
        _tft.print("PSD: --");
    } else {
        _tft.printf("PSD: %d um", median);
    }

    _tft.setCursor(10, 100);
    if (iqr < 0) {
        _tft.print("IQR: --");
    } else {
        _tft.printf("IQR: %d um", iqr);
    }

    _tft.setCursor(10, 130);
    _tft.printf("Strokes: %lu", (unsigned long)motor_get_stroke_count());
}

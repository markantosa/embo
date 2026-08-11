#include "ui_input.h"
#include "config.h"
#include "ui_display.h"
#include "ui.h"
#include <Arduino.h>

// ── EC11 rotary encoder — quadrature decode (Buxton table), ported from
// testing/display_ui_testing/src/hal/encoder_driver.cpp, validated there as
// the most responsive option on the bench. ──────────────────────────────────

#define R_START      0x0
#define R_CW_FINAL   0x1
#define R_CW_BEGIN   0x2
#define R_CW_NEXT    0x3
#define R_CCW_BEGIN  0x4
#define R_CCW_FINAL  0x5
#define R_CCW_NEXT   0x6
#define DIR_NONE     0x0
#define DIR_CW       0x10
#define DIR_CCW      0x20

static const uint8_t _encTable[7][4] = {
    {R_START,     R_CW_BEGIN,  R_CCW_BEGIN, R_START},
    {R_CW_NEXT,   R_START,     R_CW_FINAL,  R_START | DIR_CW},
    {R_CW_NEXT,   R_CW_BEGIN,  R_START,     R_START},
    {R_CW_NEXT,   R_CW_BEGIN,  R_CW_FINAL,  R_START},
    {R_CCW_NEXT,  R_START,     R_CCW_BEGIN, R_START},
    {R_CCW_NEXT,  R_CCW_FINAL, R_START,     R_START | DIR_CCW},
    {R_CCW_NEXT,  R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

static volatile uint8_t _encState = R_START;
static volatile int _encPendingStep = 0;

static void IRAM_ATTR _isrEncoder() {
    uint8_t a = digitalRead(PIN_EC11_A);
    uint8_t b = digitalRead(PIN_EC11_B);
    uint8_t pinState = (a << 1) | b;
    _encState = _encTable[_encState & 0xF][pinState];
    uint8_t result = _encState & 0x30;
    if (result == DIR_CW) _encPendingStep -= 1;
    else if (result == DIR_CCW) _encPendingStep += 1;
}

int ui_input_read_encoder_step() {
    noInterrupts();
    int step = _encPendingStep;
    _encPendingStep = 0;
    interrupts();
    return step;
}

// ── Generic short/long press debounce, shared by EC11's switch and BTN1 ──────

struct ButtonState {
    uint8_t  pin;
    bool     down          = false;
    uint32_t downAtMs      = 0;
    bool     longFired     = false;
    uint32_t debounceMs;
    uint32_t longpressMs;
};

// Button-press feedback tone — quick and subtle, distinct from the
// louder/longer end-of-mixing chirp (see mixing_running_screen.cpp/
// end_screen.cpp). Gated on Settings > Sound via ui_chirp() itself, not
// checked here — every caller of ui_input_poll_enc_sw()/poll_btn1()
// throughout the whole app gets this for free, since it's hooked at the
// shared _pollButton() level rather than needing to be added per-screen.
#define BUTTON_CHIRP_HZ    800
#define BUTTON_CHIRP_MS    30

static ButtonEvent _pollButton(ButtonState &s) {
    bool down = (digitalRead(s.pin) == LOW);

    if (down) {
        if (!s.down) {
            s.down = true;
            s.downAtMs = millis();
            s.longFired = false;
        } else if (!s.longFired && (millis() - s.downAtMs) >= s.longpressMs) {
            s.longFired = true;
            ui_chirp(BUTTON_CHIRP_HZ, BUTTON_CHIRP_MS);
            return ButtonEvent::LONG_PRESS;
        }
    } else {
        if (s.down && !s.longFired && (millis() - s.downAtMs) >= s.debounceMs) {
            s.down = false;
            ui_chirp(BUTTON_CHIRP_HZ, BUTTON_CHIRP_MS);
            return ButtonEvent::SHORT_PRESS;
        }
        s.down = false;
    }
    return ButtonEvent::NONE;
}

static ButtonState _encSw{PIN_EC11_SW, false, 0, false, EC11_SW_DEBOUNCE_MS, EC11_SW_LONGPRESS_MS};
static ButtonState _btn1{PIN_BTN1,     false, 0, false, BTN1_DEBOUNCE_MS,    BTN1_LONGPRESS_MS};

ButtonEvent ui_input_poll_enc_sw() { return _pollButton(_encSw); }
ButtonEvent ui_input_poll_btn1()   { return _pollButton(_btn1); }

void ui_input_init() {
    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_EC11_SW, INPUT_PULLUP);
    pinMode(PIN_EC11_A, INPUT_PULLUP);
    pinMode(PIN_EC11_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_EC11_A), _isrEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_EC11_B), _isrEncoder, CHANGE);
    // TEMP DIAGNOSTIC — remove once resolved. Logs the EC11 switch pin's
    // raw state over 500ms right after boot to catch any spurious
    // LOW pulses (electrical noise/chatter) that could fire a debounced
    // press with zero physical interaction.
    for (uint8_t i = 0; i < 10; i++) {
        Serial.printf("EC11_SW raw read #%u: %s\n", i, digitalRead(PIN_EC11_SW) == LOW ? "LOW (pressed)" : "HIGH (idle)");
        delay(50);
    }
}

// ── Touch ─────────────────────────────────────────────────────────────────────

static bool _touchWasDown = false;

int ui_input_poll_touch_tap(const TouchButton *buttons, uint8_t count) {
    int32_t x = 0, y = 0;
    bool down = ui_display_tft().getTouch(&x, &y);

    int hit = -1;
    if (down && !_touchWasDown) {  // only the touch-DOWN edge counts as a tap
        for (uint8_t i = 0; i < count; i++) {
            const TouchButton &b = buttons[i];
            if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
                hit = (int)i;
                ui_chirp(BUTTON_CHIRP_HZ, BUTTON_CHIRP_MS);
                break;
            }
        }
    }
    _touchWasDown = down;
    return hit;
}

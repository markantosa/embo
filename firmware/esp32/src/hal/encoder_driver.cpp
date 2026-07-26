#include "encoder_driver.h"

EncoderDriver* EncoderDriver::_instance = nullptr;

// --- Quadrature state machine (Buxton table) ---
// Encodes CLK/DT transitions; only fires DIR_CW/DIR_CCW on a full, valid detent.
#define R_START      0x0
#define R_CW_FINAL   0x1
#define R_CW_BEGIN   0x2
#define R_CW_NEXT    0x3
#define R_CCW_BEGIN  0x4
#define R_CCW_FINAL  0x5
#define R_CCW_NEXT   0x6

#define DIR_NONE 0x0
#define DIR_CW   0x10
#define DIR_CCW  0x20

static const uint8_t ttable[7][4] = {
    {R_START,     R_CW_BEGIN,  R_CCW_BEGIN, R_START},               // R_START
    {R_CW_NEXT,   R_START,     R_CW_FINAL,  R_START | DIR_CW},       // R_CW_FINAL
    {R_CW_NEXT,   R_CW_BEGIN,  R_START,     R_START},                // R_CW_BEGIN
    {R_CW_NEXT,   R_CW_BEGIN,  R_CW_FINAL,  R_START},                // R_CW_NEXT
    {R_CCW_NEXT,  R_START,     R_CCW_BEGIN, R_START},                // R_CCW_BEGIN
    {R_CCW_NEXT,  R_CCW_FINAL, R_START,     R_START | DIR_CCW},      // R_CCW_FINAL
    {R_CCW_NEXT,  R_CCW_FINAL, R_CCW_BEGIN, R_START},                // R_CCW_NEXT
};

void EncoderDriver::begin(uint8_t clkPin, uint8_t dtPin) {
    _clkPin = clkPin;
    _dtPin = dtPin;
    pinMode(_clkPin, INPUT_PULLUP);
    pinMode(_dtPin, INPUT_PULLUP);
    _lastEdgeMicros = micros();

    _instance = this;
    // Now interrupt on BOTH pins, both edges — needed for full quadrature decoding
    attachInterrupt(digitalPinToInterrupt(_clkPin), isrHandler, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_dtPin),  isrHandler, CHANGE);
}

int EncoderDriver::readStep() {
    noInterrupts();
    int step = _pendingStep;
    _pendingStep = 0;
    interrupts();
    return step;
}

void IRAM_ATTR EncoderDriver::isrHandler() {
    if (_instance) _instance->handleInterrupt();
}

void IRAM_ATTR EncoderDriver::handleInterrupt() {
    uint8_t clkState = digitalRead(_clkPin);
    uint8_t dtState  = digitalRead(_dtPin);
    uint8_t pinState = (clkState << 1) | dtState;

    _state = ttable[_state & 0xF][pinState];
    uint8_t result = _state & 0x30;   // DIR_CW / DIR_CCW / DIR_NONE

    if (result != DIR_NONE) {
        int direction = (result == DIR_CW) ? -1 : 1;

        // --- Acceleration based on time since last full detent ---
        unsigned long now = micros();
        unsigned long delta = now - _lastEdgeMicros;
        _lastEdgeMicros = now;

        int multiplier;
        if (delta < 4000) {
            multiplier = 8;
        } else if (delta < 12000) {
            multiplier = 3;
        } else {
            multiplier = 1;
        }

        _pendingStep += direction * multiplier;
    }
}
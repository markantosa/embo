#include "encoder_driver.h"

EncoderDriver* EncoderDriver::_instance = nullptr;

void EncoderDriver::begin(uint8_t clkPin, uint8_t dtPin) {
    _clkPin = clkPin;
    _dtPin = dtPin;
    pinMode(_clkPin, INPUT_PULLUP);
    pinMode(_dtPin, INPUT_PULLUP);
    _lastClkState = digitalRead(_clkPin);

    _instance = this;
    attachInterrupt(digitalPinToInterrupt(_clkPin), isrHandler, FALLING);
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
    bool clkState = digitalRead(_clkPin);
    if (clkState != _lastClkState) {
        bool dtState = digitalRead(_dtPin);
        _pendingStep += (dtState != clkState) ? -1 : 1;
        _lastClkState = clkState;
    }
}
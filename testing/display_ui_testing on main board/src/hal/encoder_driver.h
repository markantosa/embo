#pragma once
#include <Arduino.h>

class EncoderDriver {
public:
    void begin(uint8_t clkPin, uint8_t dtPin);
    int readStep();

private:
    static void IRAM_ATTR isrHandler();
    void IRAM_ATTR handleInterrupt();

    static EncoderDriver* _instance;
    uint8_t _clkPin, _dtPin;
    volatile uint8_t _state = 0;          // quadrature state machine
    volatile int _pendingStep = 0;
    volatile unsigned long _lastEdgeMicros = 0;
};
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
    volatile bool _lastClkState;
    volatile int _pendingStep = 0;
};
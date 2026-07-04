#pragma once
#include <Arduino.h>

class EncoderDriver {
public:
    void begin(uint8_t clkPin, uint8_t dtPin) {
        _clkPin=clkPin;
        _dtPin=dtPin;
        pinMode(_clkPin, INPUT_PULLUP);
        pinMode(_dtPin, INPUT_PULLUP);
        _lastClkState = digitalRead(_clkPin);
    
        _instance = this;
        attachInterrupt(digitalPinToInterrupt(_clkPin), isrHandler, CHANGE);
        
    }
    //Call every loop(). Returns -1,0 or +1 for direction, consuming the pending step.
    int readStep(){
        noInterrupts();
        int step = _pendingStep;
        _pendingStep = 0;
        interrupts();
        return step;
    }
private:
    static void  IRAM_ATTR isrHandler() {
        if (_instance) _instance->handleInterrupt();
    }
    
    void IRAM_ATTR handleInterrupt() {
        bool clkState = digitalRead(_clkPin);
        if (clkState != _lastClkState) {
            bool dtState = digitalRead(_dtPin);
            _pendingStep += (dtState != clkState) ? 1 : -1;
            _lastClkState = clkState;
        }
    }

    static EncoderDriver* _instance;
    uint8_t _clkPin, _dtPin;
    volatile bool _lastClkState;
    volatile int _pendingStep = 0;
};

EncoderDriver* EncoderDriver::_instance = nullptr;
#pragma once
#include <Arduino.h>

class ButtonDriver{
public:
    void begin(uint8_t pin) {
        _pin = pin;
        pinMode(_pin, INPUT_PULLUP);
        _lastState = digitalRead(_pin);
    }
    //call every loop(). returns true exactly once per clean press
    bool wasPressed(){
        bool reading = digitalRead(_pin);
        bool pressedEdge = false;

        if (reading!=_lastState && (millis() - _lastChangeTime)> BUTTON_DEBOUNCE_MS) {
            _lastChangeTime = millis();
            if (reading == LOW) {
                pressedEdge = true;
            }
            _lastState = reading;
        }
        return pressedEdge;
    }

private:
    uint8_t _pin;
    bool _lastState;
    unsigned long _lastChangeTime = 0;
};
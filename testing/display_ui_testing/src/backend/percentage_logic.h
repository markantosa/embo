#pragma once
#include <config.h>

class PercentageLogic {
public:
    void begin(int startValue = 0) {
        _value = startValue;
    }

    void applyStep(int step) {
        if (step == 0) return;
        _value += step * PERCENT_STEP;
        if (_value < PERCENT_MIN) _value = PERCENT_MIN;
        if (_value > PERCENT_MAX) _value = PERCENT_MAX;
    }

    int getValue() const { return _value; }

private:
    int _value = 0;
};
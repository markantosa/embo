#include "buzzer_driver.h"

BuzzerDriver::BuzzerDriver(uint8_t pin, uint8_t pwmChannel)
    : _pin(pin), _pwmChannel(pwmChannel) {}

void BuzzerDriver::begin() {
    // Modern arduino-esp32 core (3.x / IDF5) API.
    ledcAttachChannel(_pin, 2000, PWM_RESOLUTION_BITS, _pwmChannel);
    ledcWrite(_pin, 0);
}

void BuzzerDriver::tone(uint32_t frequencyHz, uint32_t durationMs) {
    ledcAttachChannel(_pin, frequencyHz, PWM_RESOLUTION_BITS, _pwmChannel);
    ledcWrite(_pin, PWM_DUTY_50PCT);
    _playing = true;
    _startMs = millis();
    _durationMs = durationMs;
}

void BuzzerDriver::stop() {
    ledcDetach(_pin);
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    _playing = false;
    _durationMs = 0;
}

void BuzzerDriver::update() {
    if (_playing && _durationMs > 0 && (millis() - _startMs >= _durationMs)) {
        stop();
    }
}

bool BuzzerDriver::isPlaying() const {
    return _playing;
}
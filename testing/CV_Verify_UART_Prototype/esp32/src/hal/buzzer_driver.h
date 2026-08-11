#pragma once
#include <Arduino.h>

class BuzzerDriver {
public:
    explicit BuzzerDriver(uint8_t pin, uint8_t pwmChannel = 0);

    void begin();

    // Non-blocking: starts tone, auto-stops after durationMs (0 = play until stop() called)
    void tone(uint32_t frequencyHz, uint32_t durationMs = 0);
    void stop();

    // Call every loop() iteration to handle auto-stop timing
    void update();

    bool isPlaying() const;

private:
    uint8_t _pin;
    uint8_t _pwmChannel;
    bool _playing = false;
    uint32_t _startMs = 0;
    uint32_t _durationMs = 0;

    static constexpr uint8_t PWM_RESOLUTION_BITS = 8;
    static constexpr uint8_t PWM_DUTY_50PCT = 128;
};
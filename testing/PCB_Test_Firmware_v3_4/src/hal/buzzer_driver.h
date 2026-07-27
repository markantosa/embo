#pragma once
#include <Arduino.h>

// LEDC-driven buzzer (arduino-esp32 3.x pin-based LEDC API).
class BuzzerDriver {
public:
	explicit BuzzerDriver(uint8_t pin) : _pin(pin) {}

	void begin() {
		ledcAttach(_pin, 2000, 8);
		ledcWrite(_pin, 0);
	}

	// Non-blocking: starts a tone, auto-stops after durationMs (0 = until stop() is called).
	void tone(uint32_t frequencyHz, uint32_t durationMs = 0) {
		ledcWriteTone(_pin, frequencyHz);
		_playing = true;
		_startMs = millis();
		_durationMs = durationMs;
	}

	void stop() {
		ledcWrite(_pin, 0);
		_playing = false;
		_durationMs = 0;
	}

	// Call every loop() iteration to service a timed tone's auto-stop.
	void update() {
		if (_playing && _durationMs > 0 && (millis() - _startMs >= _durationMs)) {
			stop();
		}
	}

	bool isPlaying() const { return _playing; }

private:
	uint8_t _pin;
	bool _playing = false;
	uint32_t _startMs = 0;
	uint32_t _durationMs = 0;
};

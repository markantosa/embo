#pragma once
#include "port_compat.h"
#include "driver/ledc.h"

// LEDC-driven buzzer, native ESP-IDF driver/ledc.h API (replaces
// arduino-esp32 3.x's pin-based ledcAttach/ledcWrite/ledcWriteTone wrapper).
class BuzzerDriver {
public:
	explicit BuzzerDriver(uint8_t pin) : _pin(pin) {}

	void begin() {
		ledc_timer_config_t timerCfg = {};
		timerCfg.speed_mode = LEDC_LOW_SPEED_MODE;
		timerCfg.timer_num = LEDC_TIMER_0;
		timerCfg.duty_resolution = LEDC_TIMER_8_BIT;
		timerCfg.freq_hz = 2000;
		timerCfg.clk_cfg = LEDC_AUTO_CLK;
		ledc_timer_config(&timerCfg);

		ledc_channel_config_t chCfg = {};
		chCfg.speed_mode = LEDC_LOW_SPEED_MODE;
		chCfg.channel = LEDC_CHANNEL_0;
		chCfg.timer_sel = LEDC_TIMER_0;
		chCfg.intr_type = LEDC_INTR_DISABLE;
		chCfg.gpio_num = _pin;
		chCfg.duty = 0;
		chCfg.hpoint = 0;
		ledc_channel_config(&chCfg);
	}

	// Non-blocking: starts a tone, auto-stops after durationMs (0 = until stop() is called).
	void tone(uint32_t frequencyHz, uint32_t durationMs = 0) {
		ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, frequencyHz);
		ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 128); // ~50% duty
		ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
		_playing = true;
		_startMs = millis();
		_durationMs = durationMs;
	}

	void stop() {
		ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
		ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
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

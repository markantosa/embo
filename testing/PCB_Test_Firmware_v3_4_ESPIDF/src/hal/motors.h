#pragma once

#include "port_compat.h"
#include "tmc2209_uart.h"
#include <esp_timer.h>
#include <cstdint>

// Drives one syringe-plunger axis: TMC2209 UART config + STEP pulse
// generation via a hardware timer (chosen over LEDC PWM so we get an
// exact, ISR-counted step position for the homing/step-count display).
class StepperAxis {
public:
	void begin(uint8_t uartAddr, int stepPin, int dirPin,
	           int enPin, uint8_t timerIndex, uint16_t runCurrentMa, uint16_t holdCurrentMa);

	// dir: -1 = backward, 0 = stop, +1 = forward. speedPct: 0-100, mapped to JOG_MIN..MAX_SPS.
	// Sets the ramp target; call updateRamp() every loop() iteration to actually glide there.
	void jog(int8_t dir, uint8_t speedPct);
	void stop(); // immediate — no decel ramp, for safety (watchdog/limit-switch stop)

	// Advances current speed toward the target set by jog(), one step per call.
	// Cheap to call every loop() iteration — internally throttled to ~100Hz.
	void updateRamp();

	void enable(bool en);

	int32_t position() const { return _position; }
	void resetPosition() { _position = 0; }

	// StallGuard result (0-1023, lower = more load). Requires en_spreadcycle=1 (set in begin()).
	uint16_t readStallGuard();

	// Re-asserts en_spreadcycle and reads it back; returns true if confirmed set.
	bool confirmSpreadCycle();

	// UART link check: 0 = good, non-zero = bad connection.
	uint8_t testConnection();

	void onTimerTick(); // called from the static ISR trampoline only

private:
	uint8_t _uartAddr = 0;
	int _stepPin = -1, _dirPin = -1, _enPin = -1;
	volatile int32_t _position = 0;
	esp_timer_handle_t _timer = nullptr;

	uint32_t _currentSps = 0; // 0 = stopped
	uint32_t _targetSps = 0;
	uint32_t _lastRampMs = 0;

	void applySpeed(uint32_t sps); // reconfigures the step timer's period
};

// Two fixed axis instances + ISR trampolines.
extern StepperAxis motorM1;
extern StepperAxis motorM2;

void motorsInit();

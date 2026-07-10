#pragma once

#include <Arduino.h>
#include <TMCStepper.h>
#include <esp_timer.h>

// Drives one syringe-plunger axis: TMC2209 UART config + STEP pulse
// generation via a hardware timer (chosen over LEDC PWM so we get an
// exact, ISR-counted step position for the homing/step-count display).
class StepperAxis {
public:
	void begin(HardwareSerial &serial, uint8_t uartAddr, int stepPin, int dirPin,
	           int enPin, uint8_t timerIndex, uint16_t runCurrentMa, uint16_t holdCurrentMa);

	// dir: -1 = backward, 0 = stop, +1 = forward. speedPct: 0-100, mapped to JOG_MIN..MAX_SPS.
	void jog(int8_t dir, uint8_t speedPct);
	void stop();

	void enable(bool en);

	int32_t position() const { return _position; }
	void resetPosition() { _position = 0; }

	// StallGuard result (0-1023, lower = more load). Requires en_spreadcycle=1 (set in begin()).
	uint16_t readStallGuard();

	// Re-asserts en_spreadcycle and reads it back; returns true if confirmed set.
	bool confirmSpreadCycle();

	void onTimerTick(); // called from the static ISR trampoline only

private:
	TMC2209Stepper *_driver = nullptr;
	int _stepPin = -1, _dirPin = -1, _enPin = -1;
	volatile int32_t _position = 0;
	esp_timer_handle_t _timer = nullptr;
};

// Two fixed axis instances + ISR trampolines (arduino-esp32 timer ISRs need
// a plain function pointer, not a member function).
extern StepperAxis motorM1;
extern StepperAxis motorM2;

void motorsInit(HardwareSerial &tmcSerial);

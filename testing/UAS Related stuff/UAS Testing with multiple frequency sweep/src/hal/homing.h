#pragma once

#include <Arduino.h>
#include "motors.h"

// Homing state machine for one axis. Non-blocking: call update() every
// loop iteration; it drives the motor toward the limit switch, stops
// immediately on trigger (hard real-time, independent of BLE), backs off
// a fixed number of steps, then zeroes the position counter.
enum class HomingState { IDLE, SEEKING, BACKING_OFF, DONE };

class HomingSequence {
public:
	void begin(StepperAxis *axis, int limitPin, int8_t seekDir);
	void start();
	void update(); // call every loop() iteration
	bool isHoming() const { return _state == HomingState::SEEKING || _state == HomingState::BACKING_OFF; }
	bool justCompleted(); // true once, on the loop iteration homing finishes

private:
	StepperAxis *_axis = nullptr;
	int _limitPin = -1;
	int8_t _seekDir = -1;
	HomingState _state = HomingState::IDLE;
	int32_t _backoffTarget = 0;
	bool _completedFlag = false;

	bool limitTriggered() const { return digitalRead(_limitPin) == LOW; } // switch pulls to GND when closed
};

extern HomingSequence homingM1;
extern HomingSequence homingM2;

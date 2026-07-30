#include "homing.h"
#include "config.h"

HomingSequence homingM1;
HomingSequence homingM2;

void HomingSequence::begin(StepperAxis *axis, int limitPin, int8_t seekDir) {
	_axis = axis;
	_limitPin = limitPin;
	_seekDir = seekDir;
	pinMode(_limitPin, INPUT_PULLUP); // switch closes to GND (§7); no external pull-up on board
	_state = HomingState::IDLE;
}

void HomingSequence::start() {
	if (limitTriggered()) {
		// Already resting on the switch: back off first so seeking has room to re-trigger it.
		_axis->resetPosition();
		_backoffTarget = HOMING_BACKOFF_STEPS;
		_axis->jog(-_seekDir, (uint8_t)(HOMING_SPEED_SPS * 100 / JOG_MAX_SPS));
		_state = HomingState::BACKING_OFF;
		return;
	}
	uint8_t speedPct = (uint8_t)constrain<uint32_t>((uint32_t)HOMING_SPEED_SPS * 100 / JOG_MAX_SPS, 1u, 100u);
	_axis->jog(_seekDir, speedPct);
	_state = HomingState::SEEKING;
}

void HomingSequence::update() {
	switch (_state) {
	case HomingState::SEEKING:
		if (limitTriggered()) {
			_axis->stop();
			_axis->resetPosition();
			uint8_t speedPct = (uint8_t)constrain<uint32_t>((uint32_t)HOMING_SPEED_SPS * 100 / JOG_MAX_SPS, 1u, 100u);
			_axis->jog(-_seekDir, speedPct);
			_backoffTarget = HOMING_BACKOFF_STEPS;
			_state = HomingState::BACKING_OFF;
		}
		break;

	case HomingState::BACKING_OFF:
		if (labs(_axis->position()) >= _backoffTarget) {
			_axis->stop();
			_axis->resetPosition();
			_state = HomingState::DONE;
			_completedFlag = true;
		}
		break;

	default:
		break;
	}
}

bool HomingSequence::justCompleted() {
	if (_completedFlag) {
		_completedFlag = false;
		return true;
	}
	return false;
}

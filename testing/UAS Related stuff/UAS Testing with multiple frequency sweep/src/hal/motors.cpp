#include "motors.h"
#include "config.h"
#include <esp_timer.h>

StepperAxis motorM1;
StepperAxis motorM2;

// esp_timer (ESP-IDF) is used instead of the LEDC PWM peripheral so each
// step pulse can be counted in software -> reliable position for homing.
struct AxisTimerCtx {
	StepperAxis *axis;
	int stepPin;
	int8_t dir;
};
static AxisTimerCtx ctxM1{&motorM1, PIN_STEP_M1, 0};
static AxisTimerCtx ctxM2{&motorM2, PIN_STEP_M2, 0};
static esp_timer_handle_t timerM1 = nullptr;
static esp_timer_handle_t timerM2 = nullptr;

static void IRAM_ATTR stepPulse(void *arg) {
	AxisTimerCtx *ctx = static_cast<AxisTimerCtx *>(arg);
	if (ctx->dir == 0) return;
	digitalWrite(ctx->stepPin, HIGH);
	delayMicroseconds(2);
	digitalWrite(ctx->stepPin, LOW);
	ctx->axis->onTimerTick();
}

void StepperAxis::begin(Stream &serial, uint8_t uartAddr, int stepPin, int dirPin,
                         int enPin, uint8_t timerIndex, uint16_t runCurrentMa, uint16_t holdCurrentMa) {
	_stepPin = stepPin;
	_dirPin = dirPin;
	_enPin = enPin;

	pinMode(_stepPin, OUTPUT);
	pinMode(_dirPin, OUTPUT);
	pinMode(_enPin, OUTPUT);
	digitalWrite(_stepPin, LOW);
	digitalWrite(_dirPin, LOW);
	enable(false);

	_driver = new TMC2209Stepper(&serial, TMC_R_SENSE, uartAddr);
	_driver->begin();
	_driver->toff(4);
	_driver->rms_current(runCurrentMa, (float)holdCurrentMa / (float)runCurrentMa);
	_driver->pdn_disable(true);   // PDN pin used for UART, not step/dir enable
	_driver->I_scale_analog(false);
	_driver->en_spreadCycle(true);
	// Datasheet: "In a multiple node system, set SENDDELAY minimum 2 to ensure
	// clean bus transitions" — we have 2 drivers sharing one UART bus, and this
	// was left at hardware default (8 bit-times) this whole session. Writes
	// don't need a reply to land, so this can take effect even while reads
	// (test_connection, SG_RESULT) are still failing.
	_driver->senddelay(4);

	AxisTimerCtx *ctx = (timerIndex == 0) ? &ctxM1 : &ctxM2;
	ctx->axis = this;
	ctx->stepPin = _stepPin;
	ctx->dir = 0;

	esp_timer_create_args_t args = {};
	args.callback = &stepPulse;
	args.arg = ctx;
	args.name = (timerIndex == 0) ? "m1step" : "m2step";
	esp_timer_handle_t *handle = (timerIndex == 0) ? &timerM1 : &timerM2;
	esp_timer_create(&args, handle);
	_timer = *handle;
}

bool StepperAxis::confirmSpreadCycle() {
	_driver->en_spreadCycle(true);
	return _driver->en_spreadCycle();
}

uint8_t StepperAxis::testConnection() {
	return _driver->test_connection();
}

void StepperAxis::enable(bool en) {
	digitalWrite(_enPin, en ? LOW : HIGH); // TMC2209 EN is active-low
}

void StepperAxis::applySpeed(uint32_t sps) {
	esp_timer_handle_t timer = (this == &motorM1) ? timerM1 : timerM2;
	uint64_t periodUs = 1000000ULL / sps;
	esp_timer_stop(timer); // safe even if not running
	esp_timer_start_periodic(timer, periodUs);
}

void StepperAxis::jog(int8_t dir, uint8_t speedPct) {
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;

	if (dir == 0) {
		stop();
		return;
	}

	speedPct = constrain(speedPct, (uint8_t)0, (uint8_t)100);
	uint32_t targetSps = JOG_MIN_SPS + (uint32_t)((JOG_MAX_SPS - JOG_MIN_SPS) * (speedPct / 100.0f));

	if (ctx->dir != 0 && ctx->dir != dir) {
		stop(); // reversing direction — stop cleanly before ramping the other way
	}

	_targetSps = targetSps;
	_lastRampMs = millis();

	if (ctx->dir == 0) {
		// Starting from rest: kick off at the minimum viable speed and let
		// updateRamp() glide up to the target — jumping straight to a high
		// step rate would just stall the motor instead of spinning it.
		ctx->dir = dir;
		digitalWrite(_dirPin, dir > 0 ? HIGH : LOW);
		enable(true);
		_currentSps = JOG_MIN_SPS;
		applySpeed(_currentSps);
	}
	// else: already running the same direction — updateRamp() will glide
	// _currentSps toward the new target (up or down) on its own.
}

void StepperAxis::updateRamp() {
	if (_currentSps == 0 && _targetSps == 0) return; // fully stopped, nothing to do

	uint32_t now = millis();
	uint32_t dt = now - _lastRampMs;
	if (dt < 10) return; // throttle to ~100Hz so we're not restarting the timer every loop() spin
	_lastRampMs = now;

	if (_currentSps == _targetSps) return;

	uint32_t delta = (JOG_ACCEL_SPS2 * dt) / 1000;
	if (delta < 1) delta = 1;

	if (_currentSps < _targetSps) {
		_currentSps += delta;
		if (_currentSps > _targetSps) _currentSps = _targetSps;
	} else {
		_currentSps = (delta > _currentSps) ? 0 : _currentSps - delta;
		if (_currentSps < _targetSps) _currentSps = _targetSps;
	}

	applySpeed(_currentSps);
}

void StepperAxis::stop() {
	esp_timer_handle_t timer = (this == &motorM1) ? timerM1 : timerM2;
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;
	esp_timer_stop(timer);
	ctx->dir = 0;
	_currentSps = 0;
	_targetSps = 0;
	enable(false);
}

uint16_t StepperAxis::readStallGuard() {
	return _driver->SG_RESULT();
}

void StepperAxis::onTimerTick() {
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;
	_position += ctx->dir;
}

void motorsInit(Stream &tmcSerial) {
	motorM1.begin(tmcSerial, TMC_ADDR_M1, PIN_STEP_M1, PIN_DIR_M1, PIN_EN_M1, 0,
	              TMC_RUN_CURRENT_MA, TMC_HOLD_CURRENT_MA);
	motorM2.begin(tmcSerial, TMC_ADDR_M2, PIN_STEP_M2, PIN_DIR_M2, PIN_EN_M2, 1,
	              TMC_RUN_CURRENT_MA, TMC_HOLD_CURRENT_MA);
}

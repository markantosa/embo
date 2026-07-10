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

void StepperAxis::begin(HardwareSerial &serial, uint8_t uartAddr, int stepPin, int dirPin,
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

void StepperAxis::enable(bool en) {
	digitalWrite(_enPin, en ? LOW : HIGH); // TMC2209 EN is active-low
}

void StepperAxis::jog(int8_t dir, uint8_t speedPct) {
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;
	esp_timer_handle_t timer = (this == &motorM1) ? timerM1 : timerM2;

	if (dir == 0) {
		stop();
		return;
	}

	speedPct = constrain(speedPct, (uint8_t)0, (uint8_t)100);
	uint32_t sps = JOG_MIN_SPS + (uint32_t)((JOG_MAX_SPS - JOG_MIN_SPS) * (speedPct / 100.0f));
	uint64_t periodUs = 1000000ULL / sps;

	ctx->dir = dir;
	digitalWrite(_dirPin, dir > 0 ? HIGH : LOW);
	enable(true);

	esp_timer_stop(timer); // safe even if not running
	esp_timer_start_periodic(timer, periodUs);
}

void StepperAxis::stop() {
	esp_timer_handle_t timer = (this == &motorM1) ? timerM1 : timerM2;
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;
	esp_timer_stop(timer);
	ctx->dir = 0;
	enable(false);
}

uint16_t StepperAxis::readStallGuard() {
	return _driver->SG_RESULT();
}

void StepperAxis::onTimerTick() {
	AxisTimerCtx *ctx = (this == &motorM1) ? &ctxM1 : &ctxM2;
	_position += ctx->dir;
}

void motorsInit(HardwareSerial &tmcSerial) {
	motorM1.begin(tmcSerial, TMC_ADDR_M1, PIN_STEP_M1, PIN_DIR_M1, PIN_EN_M1, 0,
	              TMC_RUN_CURRENT_MA, TMC_HOLD_CURRENT_MA);
	motorM2.begin(tmcSerial, TMC_ADDR_M2, PIN_STEP_M2, PIN_DIR_M2, PIN_EN_M2, 1,
	              TMC_RUN_CURRENT_MA, TMC_HOLD_CURRENT_MA);
}

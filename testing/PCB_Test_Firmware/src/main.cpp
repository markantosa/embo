#include <Arduino.h>
#include "config.h"
#include "hal/motors.h"
#include "hal/homing.h"
#include "hal/uas_sensor.h"
#include "ble/ble_service.h"

// Shared single-wire UART bus for both TMC2209 modules (§6.3, GPIO4).
static HardwareSerial tmcSerial(1);

// If a motor is jogging and no fresh motor_cmd arrives within this window,
// stop it. Bench rig with real hardware — a dropped BLE link must not
// leave a stepper running unattended.
constexpr uint32_t JOG_WATCHDOG_MS = 300;
static uint32_t lastCmdMillisM1 = 0;
static uint32_t lastCmdMillisM2 = 0;
static bool jogActiveM1 = false;
static bool jogActiveM2 = false;

static uint32_t lastUasSample = 0;
static uint32_t lastSgSample = 0;
static uint32_t lastTelemetryPush = 0;
static TelemetryPacket telemetry{};

static void applyMotorCmd(const MotorCmd &cmd) {
	uint32_t now = millis();
	switch (cmd.motorId) {
	case MOTOR_1:
		motorM1.jog(cmd.dir, cmd.speedPct);
		jogActiveM1 = (cmd.dir != 0);
		lastCmdMillisM1 = now;
		break;
	case MOTOR_2:
		motorM2.jog(cmd.dir, cmd.speedPct);
		jogActiveM2 = (cmd.dir != 0);
		lastCmdMillisM2 = now;
		break;
	case MOTOR_BOTH:
		motorM1.jog(cmd.dir, cmd.speedPct);
		motorM2.jog(cmd.dir, cmd.speedPct);
		jogActiveM1 = jogActiveM2 = (cmd.dir != 0);
		lastCmdMillisM1 = lastCmdMillisM2 = now;
		break;
	}
}

static void applyHomeCmd(uint8_t motorId) {
	if (motorId == MOTOR_1 || motorId == MOTOR_BOTH) homingM1.start();
	if (motorId == MOTOR_2 || motorId == MOTOR_BOTH) homingM2.start();
}

static void jogWatchdog() {
	uint32_t now = millis();
	if (jogActiveM1 && (now - lastCmdMillisM1 > JOG_WATCHDOG_MS)) {
		motorM1.stop();
		jogActiveM1 = false;
	}
	if (jogActiveM2 && (now - lastCmdMillisM2 > JOG_WATCHDOG_MS)) {
		motorM2.stop();
		jogActiveM2 = false;
	}
}

void setup() {
	Serial.begin(115200);

	pinMode(PIN_STATUS_LED, OUTPUT);
	digitalWrite(PIN_STATUS_LED, LOW);

	tmcSerial.begin(115200, SERIAL_8N1, PIN_TMC_UART, PIN_TMC_UART);

	motorsInit(tmcSerial);
	// Firmware must enforce SpreadCycle at every boot and confirm it (brief §6.4/12.8).
	bool m1Ok = motorM1.confirmSpreadCycle();
	bool m2Ok = motorM2.confirmSpreadCycle();
	if (!m1Ok || !m2Ok) {
		Serial.println("WARNING: SpreadCycle readback failed on one or more TMC2209 drivers");
	}

	homingM1.begin(&motorM1, PIN_LIMIT_M1, -1);
	homingM2.begin(&motorM2, PIN_LIMIT_M2, -1);

	uasSensorInit();
	bleServiceInit();

	digitalWrite(PIN_STATUS_LED, HIGH);
	Serial.println("EMBO PCB test firmware ready, advertising as 'EMBO-PCB-Test'");
}

void loop() {
	MotorCmd cmd;
	if (blePopMotorCmd(cmd)) applyMotorCmd(cmd);

	uint8_t homeMotorId;
	if (blePopHomeCmd(homeMotorId)) applyHomeCmd(homeMotorId);

	homingM1.update();
	homingM2.update();
	jogWatchdog();

	uint32_t now = millis();

	if (now - lastUasSample >= UAS_SAMPLE_PERIOD_MS) {
		lastUasSample = now;
		telemetry.uasVolts = uasReadEnvelope();
	}

	if (now - lastSgSample >= SG_SAMPLE_PERIOD_MS) {
		lastSgSample = now;
		telemetry.sgResultM1 = motorM1.readStallGuard();
		telemetry.sgResultM2 = motorM2.readStallGuard();
	}

	telemetry.positionM1 = motorM1.position();
	telemetry.positionM2 = motorM2.position();
	telemetry.homingFlags =
		(homingM1.isHoming() ? 0x01 : 0) |
		(homingM2.isHoming() ? 0x02 : 0) |
		(homingM1.justCompleted() ? 0x04 : 0) |
		(homingM2.justCompleted() ? 0x08 : 0);

	if (now - lastTelemetryPush >= UAS_SAMPLE_PERIOD_MS) {
		lastTelemetryPush = now;
		bleNotifyTelemetry(telemetry);
	}
}

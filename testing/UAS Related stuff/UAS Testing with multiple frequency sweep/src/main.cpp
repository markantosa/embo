#include <Arduino.h>
#include "config.h"
#include "hal/motors.h"
#include "hal/homing.h"
#include "hal/uas_sensor.h"
#include "hal/force_sensor.h"
#include "hal/turbidity.h"
#include "hal/bus_probe.h"
#include "hal/tmc_uart_stream.h"
#include "ble/ble_service.h"
#include "ui.h"

// Shared single-wire UART bus for both TMC2209 modules (§7.3, GPIO4/44).
static TmcUartStream tmcSerial;

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
static uint32_t lastForceSample = 0;
static uint32_t lastTurbiditySample = 0;
static uint32_t lastTelemetryPush = 0;
static uint32_t lastDebugPrint = 0;
static uint32_t lastUartCheck = 0;
static TelemetryPacket telemetry{};

const TelemetryPacket &currentTelemetry() { return telemetry; }

// Status LED: blinks at BLINK_PERIOD_MS while waiting for a BLE dashboard to
// connect, holds solid once one does — an at-a-glance "is anyone listening"
// indicator for bench bring-up, no serial monitor needed.
constexpr uint32_t LED_BLINK_PERIOD_MS = 300;
static uint32_t lastLedToggleMs = 0;
static bool ledState = false;

static void updateStatusLed() {
	if (bleIsConnected()) {
		digitalWrite(PIN_STATUS_LED, HIGH);
		return;
	}
	uint32_t now = millis();
	if (now - lastLedToggleMs >= LED_BLINK_PERIOD_MS) {
		lastLedToggleMs = now;
		ledState = !ledState;
		digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
	}
}

static void applyMotorCmd(const MotorCmd &cmd) {
	uint32_t now = millis();
	bleLog("[BLE] motor_cmd motorId=%u dir=%d speedPct=%u", cmd.motorId, cmd.dir, cmd.speedPct);
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
	pinMode(PIN_LIMIT_M1, INPUT_PULLUP);
	pinMode(PIN_LIMIT_M2, INPUT_PULLUP);

	tmcSerial.begin(UART_NUM_1, PIN_TMC_UART_TX, PIN_TMC_UART_RX, 115200);

	// BLE up first so a client that connects quickly can catch the boot-time
	// TMC diagnostics below over the LOG characteristic, not just on Serial.
	bleServiceInit();

	motorsInit(tmcSerial);

	// UART link check — 0 means the driver responded correctly, non-zero means
	// the UART bus (PDN wiring, address straps, baud) isn't actually talking to it.
	uint8_t m1Conn = motorM1.testConnection();
	uint8_t m2Conn = motorM2.testConnection();
	bleLog("[TMC] M1 test_connection() = %u (0 = OK)", m1Conn);
	bleLog("[TMC] M2 test_connection() = %u (0 = OK)", m2Conn);

	// Firmware must enforce SpreadCycle at every boot and confirm it (brief §7.4).
	bool m1Ok = motorM1.confirmSpreadCycle();
	bool m2Ok = motorM2.confirmSpreadCycle();
	bleLog("[TMC] M1 SpreadCycle confirmed: %s", m1Ok ? "yes" : "NO");
	bleLog("[TMC] M2 SpreadCycle confirmed: %s", m2Ok ? "yes" : "NO");
	if (!m1Ok || !m2Ok) {
		bleLog("WARNING: SpreadCycle readback failed on one or more TMC2209 drivers");
	}

	homingM1.begin(&motorM1, PIN_LIMIT_M1, -1);
	homingM2.begin(&motorM2, PIN_LIMIT_M2, -1);

	uasSensorInit();
	forceSensorInit();
	turbidityInit();
	busProbeInit();
	ui_init();

	bleLog("EMBO UAS frequency-sweep test firmware ready, advertising as 'EMBO-UAS-Sweep'");
	// Status LED starts blinking here (see updateStatusLed() in loop()) and
	// goes solid the moment a dashboard connects.
}

void loop() {
	updateStatusLed();
	ui_update();

	MotorCmd cmd;
	if (blePopMotorCmd(cmd)) applyMotorCmd(cmd);

	uint8_t homeMotorId;
	if (blePopHomeCmd(homeMotorId)) applyHomeCmd(homeMotorId);

	float freqHz;
	if (blePopFreqCmd(freqHz)) uasSetFrequency(freqHz);

	homingM1.update();
	homingM2.update();
	motorM1.updateRamp();
	motorM2.updateRamp();
	jogWatchdog();

	uint32_t now = millis();

	if (now - lastUasSample >= UAS_SAMPLE_PERIOD_MS) {
		lastUasSample = now;
		telemetry.uasVolts = uasReadEnvelope();
		telemetry.uasFreqHz = uasGetFrequency();
	}

	if (now - lastSgSample >= SG_SAMPLE_PERIOD_MS) {
		lastSgSample = now;
		telemetry.sgResultM1 = motorM1.readStallGuard();
		telemetry.sgResultM2 = motorM2.readStallGuard();
	}

	if (now - lastForceSample >= FORCE_SAMPLE_PERIOD_MS) {
		lastForceSample = now;
		int32_t f1, f2;
		if (forceSensorRead(f1, f2)) {
			telemetry.forceRaw1 = f1 - FORCE1_TARE_OFFSET;
			telemetry.forceRaw2 = f2 - FORCE2_TARE_OFFSET;
		}
	}

	if (now - lastTurbiditySample >= TURBIDITY_SAMPLE_PERIOD_MS) {
		lastTurbiditySample = now;
		TurbidityReading t = turbidityRead();
		telemetry.alsClear = t.alsClear;
		telemetry.irRaw = t.irRaw;
		telemetry.redRaw = t.redRaw;
		telemetry.turbidityFlags = (t.apdsOk ? 0x01 : 0) | (t.maxOk ? 0x02 : 0);
	}

	telemetry.positionM1 = motorM1.position();
	telemetry.positionM2 = motorM2.position();
	telemetry.homingFlags =
		(homingM1.isHoming() ? 0x01 : 0) |
		(homingM2.isHoming() ? 0x02 : 0) |
		(homingM1.justCompleted() ? 0x04 : 0) |
		(homingM2.justCompleted() ? 0x08 : 0);
	telemetry.limitFlags =
		(digitalRead(PIN_LIMIT_M1) == LOW ? 0x01 : 0) |
		(digitalRead(PIN_LIMIT_M2) == LOW ? 0x02 : 0);

	if (now - lastTelemetryPush >= UAS_SAMPLE_PERIOD_MS) {
		lastTelemetryPush = now;
		bleNotifyTelemetry(telemetry);
	}

	// Raw sensor dump, streamed over BLE (and Serial) — for bring-up only.
	if (now - lastDebugPrint >= 1000) {
		lastDebugPrint = now;
		bleLog("[DEBUG] UAS=%.4fV SG1=%u SG2=%u F1=%ld F2=%ld ALS=%u IR=%lu RED=%lu pos1=%ld pos2=%ld",
		       telemetry.uasVolts, telemetry.sgResultM1, telemetry.sgResultM2,
		       (long)telemetry.forceRaw1, (long)telemetry.forceRaw2,
		       telemetry.alsClear, (unsigned long)telemetry.irRaw, (unsigned long)telemetry.redRaw,
		       (long)telemetry.positionM1, (long)telemetry.positionM2);
	}

	// Repeat the UART link check periodically (not just at boot) so it's
	// visible however long after power-up you actually connect the dashboard.
	if (now - lastUartCheck >= 4000) {
		lastUartCheck = now;
		uint8_t m1Conn = motorM1.testConnection();
		uint8_t m2Conn = motorM2.testConnection();
		bleLog("[TMC] link check: M1=%u M2=%u (0 = OK, non-zero = UART not responding)", m1Conn, m2Conn);
		busProbeCaptureAndLog();
	}
}

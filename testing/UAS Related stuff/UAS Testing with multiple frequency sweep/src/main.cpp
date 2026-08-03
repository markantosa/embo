#include <Arduino.h>
#include "config.h"
#include "hal/uas_sensor.h"
#include "hal/force_sensor.h"
#include "hal/turbidity.h"
#include "ble/ble_service.h"

// Motors/homing/UI/bus-probe are deliberately not included in this build —
// see platformio.ini's build_src_filter comment. This rig only drives the
// AD9833 + reads the UAS envelope over BLE; the other TelemetryPacket
// fields (motor/StallGuard/limit-switch) stay at their zero-initialized
// default since nothing populates them here.

static uint32_t lastUasSample = 0;
static uint32_t lastForceSample = 0;
static uint32_t lastTurbiditySample = 0;
static uint32_t lastTelemetryPush = 0;
static uint32_t lastDebugPrint = 0;
static TelemetryPacket telemetry{};

// Status LED: blinks while waiting for a BLE dashboard to connect, holds
// solid once one does — an at-a-glance "is anyone listening" indicator for
// bench bring-up, no serial monitor needed.
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

void setup() {
	Serial.begin(115200);

	pinMode(PIN_STATUS_LED, OUTPUT);
	digitalWrite(PIN_STATUS_LED, LOW);

	// BLE up first so a client that connects quickly can catch the boot-time
	// log line below over the LOG characteristic, not just on Serial.
	bleServiceInit();

	uasSensorInit();
	forceSensorInit();
	turbidityInit();

	bleLog("EMBO UAS frequency-sweep test firmware ready, advertising as 'EMBO-UAS-Sweep'");
	// Status LED starts blinking here (see updateStatusLed() in loop()) and
	// goes solid the moment a dashboard connects.
}

void loop() {
	updateStatusLed();

	float freqHz;
	if (blePopFreqCmd(freqHz)) uasSetFrequency(freqHz);

	uint32_t now = millis();

	if (now - lastUasSample >= UAS_SAMPLE_PERIOD_MS) {
		lastUasSample = now;
		telemetry.uasVolts = uasReadEnvelope();
		telemetry.uasFreqHz = uasGetFrequency();
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

	if (now - lastTelemetryPush >= UAS_SAMPLE_PERIOD_MS) {
		lastTelemetryPush = now;
		bleNotifyTelemetry(telemetry);
	}

	// Raw sensor dump, streamed over BLE (and Serial) — for bring-up only.
	if (now - lastDebugPrint >= 1000) {
		lastDebugPrint = now;
		bleLog("[DEBUG] UAS=%.4fV @ %.0fHz  F1=%ld F2=%ld ALS=%u IR=%lu RED=%lu",
		       telemetry.uasVolts, telemetry.uasFreqHz,
		       (long)telemetry.forceRaw1, (long)telemetry.forceRaw2,
		       telemetry.alsClear, (unsigned long)telemetry.irRaw, (unsigned long)telemetry.redRaw);
	}
}

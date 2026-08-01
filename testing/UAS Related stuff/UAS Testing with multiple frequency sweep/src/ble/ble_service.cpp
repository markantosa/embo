#include "ble_service.h"
#include <NimBLEDevice.h>
#include <cstdarg>

// Distinct UUID base from the v2.51 test firmware so both boards' dashboards
// can never accidentally cross-connect during side-by-side bring-up.
// Distinct advertised name + distinct UUID base from PCB_Test_Firmware_v3_4
// (this project's parent) so the two firmware images' dashboards can never
// accidentally cross-connect if both boards happen to be powered nearby.
static const char *SERVICE_UUID       = "8f6a2001-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *TELEMETRY_UUID     = "8f6a2002-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *MOTOR_CMD_UUID     = "8f6a2003-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *HOME_CMD_UUID      = "8f6a2004-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *LOG_UUID           = "8f6a2005-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *FREQ_CMD_UUID      = "8f6a2006-3b1a-4e3d-9f2e-6a2c6c9a9a10";

static NimBLEServer *server = nullptr;
static NimBLECharacteristic *telemetryChar = nullptr;
static NimBLECharacteristic *logChar = nullptr;
static volatile bool connected = false;

static MotorCmd pendingMotorCmd;
static volatile bool motorCmdPending = false;
static volatile uint8_t pendingHomeMotorId = 0xFF;
static volatile bool homeCmdPending = false;
static volatile float pendingFreqHz = 0.0f;
static volatile bool freqCmdPending = false;

class MotorCmdCallbacks : public NimBLECharacteristicCallbacks {
	void onWrite(NimBLECharacteristic *chr) override {
		std::string v = chr->getValue();
		if (v.size() != sizeof(MotorCmd)) return;
		memcpy((void *)&pendingMotorCmd, v.data(), sizeof(MotorCmd));
		motorCmdPending = true;
	}
};

class HomeCmdCallbacks : public NimBLECharacteristicCallbacks {
	void onWrite(NimBLECharacteristic *chr) override {
		std::string v = chr->getValue();
		if (v.size() != 1) return;
		pendingHomeMotorId = (uint8_t)v[0];
		homeCmdPending = true;
	}
};

class FreqCmdCallbacks : public NimBLECharacteristicCallbacks {
	void onWrite(NimBLECharacteristic *chr) override {
		std::string v = chr->getValue();
		if (v.size() != sizeof(float)) return;
		float hz;
		memcpy(&hz, v.data(), sizeof(float));
		pendingFreqHz = hz;
		freqCmdPending = true;
	}
};

static MotorCmdCallbacks motorCmdCallbacks;
static HomeCmdCallbacks homeCmdCallbacks;
static FreqCmdCallbacks freqCmdCallbacks;

class ServerCallbacks : public NimBLEServerCallbacks {
	void onConnect(NimBLEServer *srv) override {
		connected = true;
		NimBLEDevice::startAdvertising(); // allow a second/replacement central to find us
	}
	void onDisconnect(NimBLEServer *srv) override {
		connected = false;
		NimBLEDevice::startAdvertising();
	}
};
static ServerCallbacks serverCallbacks;

void bleServiceInit() {
	NimBLEDevice::init("EMBO-UAS-Sweep");
	NimBLEDevice::setMTU(247); // more headroom per log-line notification
	server = NimBLEDevice::createServer();
	server->setCallbacks(&serverCallbacks);

	NimBLEService *service = server->createService(SERVICE_UUID);

	telemetryChar = service->createCharacteristic(
		TELEMETRY_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

	NimBLECharacteristic *motorCmdChar = service->createCharacteristic(
		MOTOR_CMD_UUID, NIMBLE_PROPERTY::WRITE);
	motorCmdChar->setCallbacks(&motorCmdCallbacks);

	NimBLECharacteristic *homeCmdChar = service->createCharacteristic(
		HOME_CMD_UUID, NIMBLE_PROPERTY::WRITE);
	homeCmdChar->setCallbacks(&homeCmdCallbacks);

	NimBLECharacteristic *freqCmdChar = service->createCharacteristic(
		FREQ_CMD_UUID, NIMBLE_PROPERTY::WRITE);
	freqCmdChar->setCallbacks(&freqCmdCallbacks);

	logChar = service->createCharacteristic(
		LOG_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

	service->start();

	NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
	adv->addServiceUUID(SERVICE_UUID);
	adv->start();
}

bool bleIsConnected() {
	return connected;
}

void bleNotifyTelemetry(const TelemetryPacket &pkt) {
	if (!telemetryChar) return;
	telemetryChar->setValue((uint8_t *)&pkt, sizeof(pkt));
	telemetryChar->notify();
}

void bleLog(const char *fmt, ...) {
	char buf[200];
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (len < 0) return;
	if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;

	Serial.println(buf);

	if (logChar) {
		logChar->setValue((uint8_t *)buf, len);
		logChar->notify();
	}
}

bool blePopMotorCmd(MotorCmd &out) {
	if (!motorCmdPending) return false;
	noInterrupts();
	out = pendingMotorCmd;
	motorCmdPending = false;
	interrupts();
	return true;
}

bool blePopHomeCmd(uint8_t &motorIdOut) {
	if (!homeCmdPending) return false;
	noInterrupts();
	motorIdOut = pendingHomeMotorId;
	homeCmdPending = false;
	interrupts();
	return true;
}

bool blePopFreqCmd(float &hzOut) {
	if (!freqCmdPending) return false;
	noInterrupts();
	hzOut = pendingFreqHz;
	freqCmdPending = false;
	interrupts();
	return true;
}

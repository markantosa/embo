#include "ble_service.h"
#include <NimBLEDevice.h>

static const char *SERVICE_UUID       = "8f6a0001-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *TELEMETRY_UUID     = "8f6a0002-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *MOTOR_CMD_UUID     = "8f6a0003-3b1a-4e3d-9f2e-6a2c6c9a9a10";
static const char *HOME_CMD_UUID      = "8f6a0004-3b1a-4e3d-9f2e-6a2c6c9a9a10";

static NimBLEServer *server = nullptr;
static NimBLECharacteristic *telemetryChar = nullptr;

static MotorCmd pendingMotorCmd;
static volatile bool motorCmdPending = false;
static volatile uint8_t pendingHomeMotorId = 0xFF;
static volatile bool homeCmdPending = false;

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

static MotorCmdCallbacks motorCmdCallbacks;
static HomeCmdCallbacks homeCmdCallbacks;

class ServerCallbacks : public NimBLEServerCallbacks {
	void onConnect(NimBLEServer *srv) override {
		NimBLEDevice::startAdvertising(); // allow a second/replacement central to find us
	}
	void onDisconnect(NimBLEServer *srv) override {
		NimBLEDevice::startAdvertising();
	}
};
static ServerCallbacks serverCallbacks;

void bleServiceInit() {
	NimBLEDevice::init("EMBO-PCB-Test");
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

	service->start();

	NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
	adv->addServiceUUID(SERVICE_UUID);
	adv->start();
}

void bleNotifyTelemetry(const TelemetryPacket &pkt) {
	if (!telemetryChar) return;
	telemetryChar->setValue((uint8_t *)&pkt, sizeof(pkt));
	telemetryChar->notify();
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

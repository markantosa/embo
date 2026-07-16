// Throwaway minimal TMC2209 UART test.
//
// Purpose: isolate whether the persistent "test_connection() always fails"
// symptom in the full PCB_Test_Firmware is caused by something in that
// firmware's complexity (BLE stack, esp_timer step-pulse ISRs, homing state
// machine all running concurrently) or is a genuine board/wiring issue.
// This sketch does nothing else — no BLE, no timers, no interrupts — just
// the bare minimum to talk to both TMC2209 drivers over the shared UART bus.
//
// Values match include/config.h in the main firmware.

#include <Arduino.h>
#include <TMCStepper.h>

constexpr int PIN_TMC_UART = 4;
constexpr int PIN_EN_M1 = 7;
constexpr int PIN_EN_M2 = 10;
constexpr float R_SENSE = 0.11f;
constexpr uint8_t ADDR_M1 = 0;
constexpr uint8_t ADDR_M2 = 1;

HardwareSerial tmcSerial(1);
TMC2209Stepper driverM1(&tmcSerial, R_SENSE, ADDR_M1);
TMC2209Stepper driverM2(&tmcSerial, R_SENSE, ADDR_M2);

void setup() {
	Serial.begin(115200);
	delay(2000); // give you time to open the serial monitor before boot logs fly by

	pinMode(PIN_EN_M1, OUTPUT);
	pinMode(PIN_EN_M2, OUTPUT);
	digitalWrite(PIN_EN_M1, LOW); // active-low enable
	digitalWrite(PIN_EN_M2, LOW);

	tmcSerial.begin(115200, SERIAL_8N1, PIN_TMC_UART, PIN_TMC_UART);
	pinMode(PIN_TMC_UART, OUTPUT_OPEN_DRAIN);

	driverM1.begin();
	driverM2.begin();

	Serial.println("Minimal TMC2209 UART test — no BLE, no timers, nothing else running.");
}

void loop() {
	uint8_t c1 = driverM1.test_connection();
	uint8_t c2 = driverM2.test_connection();
	uint32_t v1 = driverM1.version();
	uint32_t v2 = driverM2.version();

	Serial.printf("M1: test_connection=%u version=0x%02X | M2: test_connection=%u version=0x%02X\n",
	              c1, v1, c2, v2);

	delay(1000);
}

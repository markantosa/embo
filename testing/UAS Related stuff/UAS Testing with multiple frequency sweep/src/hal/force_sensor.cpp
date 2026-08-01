#include "force_sensor.h"
#include "config.h"

void forceSensorInit() {
	pinMode(PIN_HX711_SCK, OUTPUT);
	digitalWrite(PIN_HX711_SCK, LOW);
	pinMode(PIN_HX711_1_DT, INPUT);
	pinMode(PIN_HX711_2_DT, INPUT);
}

static int32_t signExtend24(uint32_t v) {
	if (v & 0x800000) v |= 0xFF000000;
	return (int32_t)v;
}

bool forceSensorRead(int32_t &raw1, int32_t &raw2) {
	// DT reads HIGH while a conversion isn't ready yet (§10.1).
	if (digitalRead(PIN_HX711_1_DT) == HIGH || digitalRead(PIN_HX711_2_DT) == HIGH) {
		return false;
	}

	// Critical section: PD_SCK must never sit HIGH for more than ~60us or the
	// chip powers itself down mid-read (§10.1, §11.2).
	noInterrupts();
	uint32_t v1 = 0, v2 = 0;
	for (int i = 0; i < 24; i++) {
		digitalWrite(PIN_HX711_SCK, HIGH);
		delayMicroseconds(1);
		v1 = (v1 << 1) | digitalRead(PIN_HX711_1_DT);
		v2 = (v2 << 1) | digitalRead(PIN_HX711_2_DT);
		digitalWrite(PIN_HX711_SCK, LOW);
		delayMicroseconds(1);
	}
	// 25th pulse: selects channel A / gain 128 for the next conversion.
	digitalWrite(PIN_HX711_SCK, HIGH);
	delayMicroseconds(1);
	digitalWrite(PIN_HX711_SCK, LOW);
	delayMicroseconds(1);
	interrupts();

	raw1 = signExtend24(v1);
	raw2 = signExtend24(v2);
	return true;
}

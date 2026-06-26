/**
 * Testing required force to push the syringe (with embolising agent inside)
 * ESP32 Platform
 * Uses Loadcell + HX711 loadcell amp 
 * 
 * Wiring
 * HX711 VCC - 3.3V
 * HX711 GND - GND
 * HX711 DT - DATA_PIN
 * HX711 SCK - CLOCK_PIN
 */

#include <Arduino.h>
#include <HX711.h>

// -Pin config-
// ESP32 avoid GPIO 34-39 for SCK (input-only)
const int DATA_PIN = 4;
const int CLOCK_PPIN = 5;

// -Tare button-
const int TARE_PIN = 15; //pull to low to tare, set to -1 to disable, default 15

// -Calibration-
// Step 1: set CALIBRATION_MODE true, flash, open monitor
// Step 2: note raw offset (no weight), then raw with known weight
// Step 3: CALIBRATION_FACTOR = (raw_with_weight - offset) / grams
// Step 4: fill in values below, set CALIBRATION_MODE false, reflash
const bool CALIBRATION_MODE = false;
const long ZERO_OFFSET = 0;
const float CALIBRATION_FACTOR = 420.0; units in grams 

// -Averaging- 
const int SAMPLES = 10;

// -Read Interval
const unsigned long READ_INTERVAL_MS = 200; in ms 

HX711 scale;
unsigned long lastRead = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    if (TARE_PIN >= 0) {
    pinMode(TARE_PIN, INPUT_PULLUP);
  }
 
  scale.begin(DATA_PIN, CLOCK_PIN);
 
  // Wait up to 3 s for the chip to come up
  unsigned long t = millis();
  while (!scale.is_ready() && millis() - t < 3000) delay(10);
 
  if (!scale.is_ready()) {
    Serial.println("[ERROR] HX711 not found – check wiring!");
    while (true) delay(1000);
  }
  if (CALIBRATION_MODE) {
    Serial.println("=== CALIBRATION MODE ===");
    Serial.println("Remove all weight, then send any key to tare.");
    while (!Serial.available()) delay(10);
    Serial.read();
    scale.tare(20);
    Serial.print("Tare done. Raw offset: ");
    Serial.println(scale.get_offset());
    Serial.println("Place known weight, then send any key.");
    while (!Serial.available()) delay(10);
    Serial.read();
    long raw = scale.read_average(20);
    Serial.print("Raw with weight: ");
    Serial.println(raw);
    Serial.println("CALIBRATION_FACTOR = (raw - offset) / weight_grams");
  } else {
    scale.set_offset(ZERO_OFFSET);
    scale.set_scale(CALIBRATION_FACTOR);
    Serial.println("Load cell ready. Send 't' to tare.");
  }

}

void loop() {
    // ── Serial tare command ──────────────────────────────────────
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't' || c == 'T') {
      scale.tare(10);
      Serial.println("[INFO] Tared");
    }
  }
 
  // ── Hardware tare button ─────────────────────────────────────
  if (TARE_PIN >= 0 && digitalRead(TARE_PIN) == LOW) {
    scale.tare(10);
    Serial.println("[INFO] Tared (button)");
    delay(300);  // simple debounce
  }
 
  // ── Calibration raw print ────────────────────────────────────
  if (CALIBRATION_MODE) {
    if (millis() - lastRead >= READ_INTERVAL_MS && scale.is_ready()) {
      lastRead = millis();
      Serial.print("Raw: ");
      Serial.println(scale.read_average(5));
    }
    return;
  }
 
  // ── Normal weight reading ────────────────────────────────────
  if (millis() - lastRead >= READ_INTERVAL_MS && scale.is_ready()) {
    lastRead = millis();
    float grams = scale.get_units(SAMPLES);
    float kg    = grams / 1000.0;
    Serial.print("Weight: ");
    Serial.print(grams, 1);
    Serial.print(" g  (");
    Serial.print(kg, 3);
    Serial.println(" kg)");
  }
}
/**
 * Load Cell + HX711 + SH1106 1.3" OLED (U8g2) on ESP32-C6
 * PlatformIO project
 *
 * States:
 *   IDLE  – live weight display
 *   TEST  – scrolling graph + CSV stream over serial
 *   DONE  – peak weight display
 *
 * Controls:
 *   BOOT button (GPIO 9)  – start / stop / reset
 *   Serial 't'            – tare
 *   Serial 's'            – same as button
 *
 * CSV stream format:
 *   TEST_START
 *   ms,grams
 *   100,23.45
 *   ...
 *   TEST_END,peak,312.10
 *
 * All non-CSV lines are prefixed with '#' so the receiver can ignore them.
 */

#include <Arduino.h>
#include <Wire.h>
#include <HX711.h>
#include <U8g2lib.h>

// ── Pins ──────────────────────────────────────────────────────
#define HX711_DOUT  6
#define HX711_SCK   7
#define OLED_SDA    21
#define OLED_SCL    20
#define BOOT_BTN    9

// ── Calibration ───────────────────────────────────────────────
const float CALIBRATION_FACTOR = -7050;
const int   SAMPLES            = 5;

// ── Graph config ──────────────────────────────────────────────
// Auto-scaling: graph Y axis grows to fit peak, min 500g
const int   GRAPH_X         = 26;
const int   GRAPH_Y         = 12;
const int   GRAPH_W         = 100;
const int   GRAPH_H         = 42;
const float GRAPH_MIN_SCALE = 500.0;   // minimum full-scale even if peak is low

// ── Timing ────────────────────────────────────────────────────
const unsigned long SAMPLE_MS = 100;
const unsigned long DISP_MS   = 50;

// ─────────────────────────────────────────────────────────────
// 1.3" OLED is SH1106 128x64
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

HX711 scale;

enum State { IDLE, TEST, DONE };
State state = IDLE;

float graphBuf[100];
int   graphHead    = 0;
int   graphCount   = 0;
float peakGrams    = 0.0;
float currentGrams = 0.0;
bool  isTared      = false;

unsigned long testStartMs  = 0;
unsigned long lastSample   = 0;
unsigned long lastDisp     = 0;
bool          lastBtnState = HIGH;
unsigned long lastBtnTime  = 0;

// ─────────────────────────────────────────────────────────────
void resetTest() {
  memset(graphBuf, 0, sizeof(graphBuf));
  graphHead = 0; graphCount = 0; peakGrams = 0.0;
}

void pushSample(float g) {
  graphBuf[graphHead] = g;
  graphHead = (graphHead + 1) % GRAPH_W;
  if (graphCount < GRAPH_W) graphCount++;
  if (g > peakGrams) peakGrams = g;
}

float getSample(int i) {
  int start = (graphHead - graphCount + GRAPH_W) % GRAPH_W;
  return graphBuf[(start + i) % GRAPH_W];
}

// ─────────────────────────────────────────────────────────────
void drawIdle() {
  oled.clearBuffer();

  // Header
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 10, isTared ? "TARED" : "NO TARE");
  oled.drawStr(75, 10, "BTN=start");
  oled.drawHLine(0, 12, 128);

  // Large weight centred
  oled.setFont(u8g2_font_fub20_tr);
  char buf[16];
  sprintf(buf, "%.1f", currentGrams);
  int w = oled.getStrWidth(buf);
  oled.drawStr((128 - w) / 2, 42, buf);

  // Unit
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 55, "g");

  // Hint
  oled.drawHLine(0, 56, 128);
  oled.drawStr(0, 64, "t=tare  s=start");

  oled.sendBuffer();
}

void drawTest() {
  // Auto-scale Y axis to peak, minimum GRAPH_MIN_SCALE
  float scaleMax = max((float)peakGrams * 1.2f, GRAPH_MIN_SCALE);

  oled.clearBuffer();

  // Header
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 10, "TEST");
  char pkBuf[16];
  sprintf(pkBuf, "PK:%.0fg", peakGrams);
  oled.drawStr(128 - oled.getStrWidth(pkBuf), 10, pkBuf);

  // Graph border
  oled.drawFrame(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H);

  // Y axis labels (tiny font)
  oled.setFont(u8g2_font_04b_03_tr);
  char yTop[8], yMid[8];
  if (scaleMax >= 1000.0f)
    sprintf(yTop, "%.1fk", scaleMax / 1000.0f);
  else
    sprintf(yTop, "%.0f", scaleMax);
  if (scaleMax >= 2000.0f)
    sprintf(yMid, "%.1fk", scaleMax / 2000.0f);
  else
    sprintf(yMid, "%.0f", scaleMax / 2.0f);

  oled.drawStr(0, GRAPH_Y + 5,               yTop);
  oled.drawStr(0, GRAPH_Y + GRAPH_H / 2 + 2, yMid);
  oled.drawStr(0, GRAPH_Y + GRAPH_H,         "0");

  // Plot
  for (int i = 0; i < graphCount - 1; i++) {
    float v0 = constrain(getSample(i),     0, scaleMax);
    float v1 = constrain(getSample(i + 1), 0, scaleMax);
    int x0 = GRAPH_X + 1 + i;
    int x1 = GRAPH_X + 2 + i;
    int y0 = GRAPH_Y + GRAPH_H - 2 - (int)(v0 / scaleMax * (GRAPH_H - 2));
    int y1 = GRAPH_Y + GRAPH_H - 2 - (int)(v1 / scaleMax * (GRAPH_H - 2));
    oled.drawLine(x0, y0, x1, y1);
  }

  // Current value bottom-left
  oled.setFont(u8g2_font_ncenB08_tr);
  char cur[12];
  sprintf(cur, "%.0fg", currentGrams);
  oled.drawStr(0, 64, cur);

  // Stop hint bottom-right
  oled.drawStr(128 - oled.getStrWidth("BTN=stop"), 64, "BTN=stop");

  oled.sendBuffer();
}

void drawDone() {
  oled.clearBuffer();

  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 10, "TEST DONE");
  oled.drawHLine(0, 12, 128);

  // Peak weight large and centred
  oled.setFont(u8g2_font_fub20_tr);
  char buf[16];
  sprintf(buf, "%.1f", peakGrams);
  int w = oled.getStrWidth(buf);
  oled.drawStr((128 - w) / 2, 42, buf);

  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 53, "g  PEAK");
  oled.drawHLine(0, 56, 128);
  oled.drawStr(0, 64, "BTN=new test");

  oled.sendBuffer();
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("# Starting...");

  pinMode(BOOT_BTN, INPUT_PULLUP);
  Wire.setPins(OLED_SDA, OLED_SCL);
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 20, "Loadcell Start");
  oled.sendBuffer();

  scale.begin(HX711_DOUT, HX711_SCK);
  delay(500);

  if (scale.is_ready()) {
    Serial.println("# HX711 detected");
  } else {
    Serial.println("# HX711 NOT detected");
    oled.clearBuffer();
    oled.drawStr(0, 30, "HX711 ERROR");
    oled.sendBuffer();
    while (true) delay(1000);
  }

  scale.set_scale(CALIBRATION_FACTOR);
  Serial.println("# Taring...");
  scale.tare();
  isTared = true;
  Serial.println("# Ready. BTN or 's' to start, 't' to tare.");
}

// ─────────────────────────────────────────────────────────────
void loop() {
  if (scale.is_ready())
    currentGrams = scale.get_units(SAMPLES);

  // ── Boot button debounce ─────────────────────────────────────
  bool btnReading = digitalRead(BOOT_BTN);
  if (btnReading != lastBtnState) {
    lastBtnTime  = millis();
    lastBtnState = btnReading;
  }
  bool btnPressed = (millis() - lastBtnTime > 30) && (btnReading == LOW);

  // ── Input handling ───────────────────────────────────────────
  bool doAction = false;
  bool doTare   = false;

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't' || c == 'T') doTare   = true;
    if (c == 's' || c == 'S') doAction = true;
  }
  if (btnPressed) {
    doAction = true;
    while (digitalRead(BOOT_BTN) == LOW) delay(10);  // wait for release
  }

  if (doTare) {
    scale.tare(10);
    isTared = true;
    Serial.println("# Tared");
  }
  if (doAction) {
    if (state == IDLE) {
      resetTest();
      testStartMs = millis();
      Serial.println("TEST_START");
      Serial.println("ms,grams");
      state = TEST;
    } else if (state == TEST) {
      Serial.print("TEST_END,peak,");
      Serial.println(peakGrams, 2);
      state = DONE;
    } else if (state == DONE) {
      state = IDLE;
      Serial.println("# Reset");
    }
  }

  // ── State machine ────────────────────────────────────────────
  switch (state) {
    case IDLE:
      if (millis() - lastDisp >= DISP_MS) { lastDisp = millis(); drawIdle(); }
      break;

    case TEST:
      if (millis() - lastSample >= SAMPLE_MS) {
        lastSample = millis();
        pushSample(currentGrams);
        // Pure CSV — no prefix, receiver saves this directly
        Serial.print(millis() - testStartMs);
        Serial.print(",");
        Serial.println(currentGrams, 2);
      }
      if (millis() - lastDisp >= DISP_MS) { lastDisp = millis(); drawTest(); }
      break;

    case DONE:
      if (millis() - lastDisp >= 200) { lastDisp = millis(); drawDone(); }
      break;
  }
}
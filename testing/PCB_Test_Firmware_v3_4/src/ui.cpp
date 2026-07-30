#include "ui.h"
#include "config.h"
#include "hal/LGFX_Config.h"
#include "hal/buzzer_driver.h"
#include "hal/motors.h"
#include <Arduino.h>

static LGFX _tft;
static BuzzerDriver _buzzer(PIN_BUZ_PWM);

enum class UiState { SELECT, CONTROL };
static UiState _state = UiState::SELECT;
static uint8_t _selectedMotor = 0;  // 0 = Motor 1, 1 = Motor 2
static bool _needsRedraw = true;
static bool _jogging = false;
static int8_t _curDir = 0;       // last commanded direction, for status line
static uint8_t _curSpeedPct = 0; // last commanded speed, for status line

// Display-freeze bench feature (SELECT screen only, long-press to toggle):
// stops all TFT SPI traffic so it can't couple noise onto the AD9833's
// shared CLK/MOSI lines while probing the UAS analog chain with a scope.
static bool _displayFrozen = false;
static bool _suppressNextSelectPress = false;  // eat the release-edge after a freeze toggle

// ── EC11 rotary encoder — quadrature decode (Buxton table) ──────────────────

#define R_START      0x0
#define R_CW_FINAL   0x1
#define R_CW_BEGIN   0x2
#define R_CW_NEXT    0x3
#define R_CCW_BEGIN  0x4
#define R_CCW_FINAL  0x5
#define R_CCW_NEXT   0x6
#define DIR_NONE     0x0
#define DIR_CW       0x10
#define DIR_CCW      0x20

static const uint8_t _encTable[7][4] = {
	{R_START,     R_CW_BEGIN,  R_CCW_BEGIN, R_START},
	{R_CW_NEXT,   R_START,     R_CW_FINAL,  R_START | DIR_CW},
	{R_CW_NEXT,   R_CW_BEGIN,  R_START,     R_START},
	{R_CW_NEXT,   R_CW_BEGIN,  R_CW_FINAL,  R_START},
	{R_CCW_NEXT,  R_START,     R_CCW_BEGIN, R_START},
	{R_CCW_NEXT,  R_CCW_FINAL, R_START,     R_START | DIR_CCW},
	{R_CCW_NEXT,  R_CCW_FINAL, R_CCW_BEGIN, R_START},
};

static volatile uint8_t _encState = R_START;
static volatile int _encPendingStep = 0;

// Absolute detent counter — CW (turn right) counts up, CCW (turn left)
// counts down. Never reset by the ISR; CONTROL mode zeroes a separate
// reference against this on entry so "how far from where I started" can be
// read back at any time (see _encRefPos in ui_update()).
static volatile int32_t _encAbsPos = 0;

static void IRAM_ATTR _isrEncoder() {
	uint8_t a = digitalRead(PIN_EC11_A);
	uint8_t b = digitalRead(PIN_EC11_B);
	uint8_t pinState = (a << 1) | b;
	_encState = _encTable[_encState & 0xF][pinState];
	uint8_t result = _encState & 0x30;
	if (result == DIR_CW) { _encPendingStep -= 1; _encAbsPos += 1; }
	else if (result == DIR_CCW) { _encPendingStep += 1; _encAbsPos -= 1; }
}

static int _encReadStep() {
	noInterrupts();
	int step = _encPendingStep;
	_encPendingStep = 0;
	interrupts();
	return step;
}

static int32_t _encReadAbsPos() {
	noInterrupts();
	int32_t pos = _encAbsPos;
	interrupts();
	return pos;
}

// ── EC11 push-switch — short press only ──────────────────────────────────────

static bool _encSwLastState = HIGH;
static uint32_t _encSwLastChangeMs = 0;
constexpr uint32_t ENC_SW_DEBOUNCE_MS = 30;

static bool _encSwWasPressed() {
	bool reading = digitalRead(PIN_EC11_SW);
	bool pressedEdge = false;
	if (reading != _encSwLastState && (millis() - _encSwLastChangeMs) > ENC_SW_DEBOUNCE_MS) {
		_encSwLastChangeMs = millis();
		if (reading == LOW) pressedEdge = true;
		_encSwLastState = reading;
	}
	return pressedEdge;
}

// Independent long-press watcher, separate from the short-press edge logic
// above so it can't interfere with CONTROL mode's safety-critical "release
// = stop" behavior. Used only to toggle the display-freeze bench feature
// (see _displayFrozen) — every TFT SPI transaction shares physical
// CLK/MOSI wires with the AD9833 (see uas_sensor.cpp), so pausing screen
// updates removes that noise source while probing the analog side.
static uint32_t _encSwDownMs = 0;
static bool _longPressFired = false;
constexpr uint32_t ENC_SW_LONG_PRESS_MS = 700;

static bool _encSwWasLongPressed() {
	bool down = (digitalRead(PIN_EC11_SW) == LOW);
	if (!down) {
		_encSwDownMs = 0;
		_longPressFired = false;
		return false;
	}
	if (_encSwDownMs == 0) _encSwDownMs = millis();
	if (!_longPressFired && (millis() - _encSwDownMs) >= ENC_SW_LONG_PRESS_MS) {
		_longPressFired = true;
		return true;
	}
	return false;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

static void _drawCentered(const char *text, int16_t cx, int16_t cy, uint16_t color, uint8_t size) {
	_tft.setTextColor(color);
	_tft.setTextSize(size);
	int16_t w = _tft.textWidth(text);
	int16_t h = _tft.fontHeight();
	_tft.setCursor(cx - w / 2, cy - h / 2);
	_tft.print(text);
}

// ── Screen split: left = sensor telemetry panel, right = motor control ─────
// Divider sits at x=158..159 and is drawn once in ui_init(); every redraw
// below stays strictly on its own side of it so the two halves never wipe
// each other out.
constexpr int16_t SPLIT_X   = 158;
constexpr int16_t LEFT_X0   = 0;
constexpr int16_t LEFT_W    = SPLIT_X - 2;      // 0..155
constexpr int16_t RIGHT_X0  = SPLIT_X + 2;      // 160..319
constexpr int16_t RIGHT_W   = 320 - RIGHT_X0;
constexpr int16_t RIGHT_CX  = RIGHT_X0 + RIGHT_W / 2;

struct BoxRect { int16_t x, y, w, h; };
static const BoxRect _box[2] = {
	{RIGHT_X0 + (RIGHT_W - 120) / 2, 45, 120, 50},
	{RIGHT_X0 + (RIGHT_W - 120) / 2, 115, 120, 50},
};

static void _drawSelectScreen() {
	_tft.fillRect(RIGHT_X0, 0, RIGHT_W, 240, TFT_BLACK);
	_drawCentered("Select motor", RIGHT_CX, 15, TFT_DARKGREY, 1);

	for (uint8_t i = 0; i < 2; i++) {
		bool selected = (i == _selectedMotor);
		uint16_t border = selected ? TFT_CYAN : 0x4208;  // dim grey when not selected
		_tft.drawRect(_box[i].x, _box[i].y, _box[i].w, _box[i].h, border);
		_tft.drawRect(_box[i].x + 1, _box[i].y + 1, _box[i].w - 2, _box[i].h - 2, border);
		char label[10];
		snprintf(label, sizeof(label), "Motor %u", i + 1);
		_drawCentered(label, _box[i].x + _box[i].w / 2, _box[i].y + _box[i].h / 2,
		              selected ? TFT_WHITE : TFT_DARKGREY, 2);
	}

	_drawCentered("Rotate=pick", RIGHT_CX, 205, TFT_DARKGREY, 1);
	_drawCentered("Press=control", RIGHT_CX, 220, TFT_DARKGREY, 1);
}

static void _drawControlScreen(bool forceFull) {
	if (forceFull) {
		_tft.fillRect(RIGHT_X0, 0, RIGHT_W, 240, TFT_BLACK);
		char title[16];
		snprintf(title, sizeof(title), "Motor %u", _selectedMotor + 1);
		_drawCentered(title, RIGHT_CX, 20, TFT_WHITE, 2);
		_drawCentered("Right=fwd Left=rev", RIGHT_CX, 205, TFT_DARKGREY, 1);
		_drawCentered("Center=stop Press=back", RIGHT_CX, 220, TFT_DARKGREY, 1);
	}

	StepperAxis *m = (_selectedMotor == 0) ? &motorM1 : &motorM2;
	char posBuf[20];
	snprintf(posBuf, sizeof(posBuf), "Pos: %ld", (long)m->position());
	_tft.fillRect(RIGHT_X0, 90, RIGHT_W, 30, TFT_BLACK);
	_drawCentered(posBuf, RIGHT_CX, 100, TFT_WHITE, 2);

	char statusBuf[16];
	uint16_t statusColor;
	if (!_jogging) {
		snprintf(statusBuf, sizeof(statusBuf), "STOP");
		statusColor = TFT_DARKGREY;
	} else if (_curDir > 0) {
		snprintf(statusBuf, sizeof(statusBuf), "FWD %u%%", _curSpeedPct);
		statusColor = TFT_GREEN;
	} else {
		snprintf(statusBuf, sizeof(statusBuf), "REV %u%%", _curSpeedPct);
		statusColor = TFT_ORANGE;
	}
	_tft.fillRect(RIGHT_X0, 140, RIGHT_W, 30, TFT_BLACK);
	_drawCentered(statusBuf, RIGHT_CX, 150, statusColor, 2);
}

// ── Left panel — live sensor telemetry, all v3.4 sensors, text only ────────
// Same values as the BLE TELEMETRY characteristic (see currentTelemetry() /
// ble_service.h), just readable straight off the board without a dashboard.
constexpr uint32_t SENSOR_PANEL_REFRESH_MS = 1000;
static uint32_t _lastSensorRedrawMs = 0;

static void _drawSensorPanel() {
	const TelemetryPacket &t = currentTelemetry();
	_tft.fillRect(LEFT_X0, 0, LEFT_W, 240, TFT_BLACK);
	_tft.setTextSize(1);
	_tft.setTextColor(TFT_CYAN);
	_tft.setCursor(4, 4);
	_tft.print("SENSORS");

	char line[32];
	int16_t y = 20;
	const int16_t step = 16;

	_tft.setTextColor(TFT_YELLOW);
	snprintf(line, sizeof(line), "UAS  %.3f V", t.uasVolts);
	_tft.setCursor(4, y); _tft.print(line); y += step;

	_tft.setTextColor(TFT_WHITE);
	snprintf(line, sizeof(line), "SG1  %u", t.sgResultM1);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "SG2  %u", t.sgResultM2);
	_tft.setCursor(4, y); _tft.print(line); y += step;

	snprintf(line, sizeof(line), "F1   %ld", (long)t.forceRaw1);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "F2   %ld", (long)t.forceRaw2);
	_tft.setCursor(4, y); _tft.print(line); y += step;

	snprintf(line, sizeof(line), "ALS  %u", t.alsClear);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "IR   %lu", (unsigned long)t.irRaw);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "RED  %lu", (unsigned long)t.redRaw);
	_tft.setCursor(4, y); _tft.print(line); y += step;

	_tft.setTextColor(TFT_DARKGREY);
	snprintf(line, sizeof(line), "TBFlg 0x%02X", t.turbidityFlags);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "LIM  0x%02X", t.limitFlags);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "HOME 0x%02X", t.homingFlags);
	_tft.setCursor(4, y); _tft.print(line); y += step;

	snprintf(line, sizeof(line), "P1 %ld", (long)t.positionM1);
	_tft.setCursor(4, y); _tft.print(line); y += step;
	snprintf(line, sizeof(line), "P2 %ld", (long)t.positionM2);
	_tft.setCursor(4, y); _tft.print(line); y += step;
}

// One-off overlay drawn only at the moment freeze is toggled on — a single
// small SPI transaction, not a recurring one, so it doesn't defeat the
// purpose of freezing. Sits in the right half only; the left sensor panel
// just stops updating in place, which is the point.
static void _drawFrozenBanner() {
	_tft.fillRect(RIGHT_X0, 226, RIGHT_W, 14, TFT_BLACK);
	_drawCentered("FROZEN (hold=resume)", RIGHT_CX, 233, TFT_RED, 1);
}

// ── Public API ───────────────────────────────────────────────────────────────

void ui_init() {
	_tft.init();
	_tft.setRotation(3);
	_tft.fillScreen(TFT_BLACK);
	_tft.drawFastVLine(SPLIT_X, 0, 240, 0x4208);  // static divider, never redrawn over

	pinMode(PIN_EC11_SW, INPUT_PULLUP);
	pinMode(PIN_EC11_A, INPUT_PULLUP);
	pinMode(PIN_EC11_B, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(PIN_EC11_A), _isrEncoder, CHANGE);
	attachInterrupt(digitalPinToInterrupt(PIN_EC11_B), _isrEncoder, CHANGE);

	_buzzer.begin();

	_state = UiState::SELECT;
	_needsRedraw = true;
}

void ui_update() {
	_buzzer.update();

	// Long-press toggles the freeze — SELECT screen only, deliberately. In
	// CONTROL mode any button release must always stop the motor with zero
	// exceptions (safety-critical), so freeze-toggling is not offered there
	// rather than risk eating that release edge.
	if (_encSwWasLongPressed() && _state == UiState::SELECT) {
		_displayFrozen = !_displayFrozen;
		_suppressNextSelectPress = true;
		_buzzer.tone(2000, 80);
		if (_displayFrozen) {
			_drawFrozenBanner();  // one-off SPI transaction, not recurring
		} else {
			_needsRedraw = true;  // resume: force a full redraw next tick
		}
	}

	if (!_displayFrozen) {
		uint32_t nowSensors = millis();
		if (nowSensors - _lastSensorRedrawMs >= SENSOR_PANEL_REFRESH_MS) {
			_lastSensorRedrawMs = nowSensors;
			_drawSensorPanel();
		}
	}

	int step = _encReadStep();
	bool pressed = _encSwWasPressed();
	if (_suppressNextSelectPress && pressed) {
		_suppressNextSelectPress = false;
		pressed = false;
	}

	static int32_t _encRefPos = 0;  // absolute encoder position treated as "center" for CONTROL mode

	if (_state == UiState::SELECT) {
		if (step != 0) {
			_selectedMotor = 1 - _selectedMotor;  // only two items — any turn toggles
			_needsRedraw = true;
		}
		if (pressed) {
			_state = UiState::CONTROL;
			_encRefPos = _encReadAbsPos();  // re-center: this angle is now "stop"
			_jogging = false;
			_needsRedraw = true;
		}
		if (_needsRedraw && !_displayFrozen) { _drawSelectScreen(); _needsRedraw = false; }
		return;
	}

	// UiState::CONTROL — proportional joystick-style control: turning right
	// (CW) from the angle you entered CONTROL at drives forward, turning
	// left (CCW) drives reverse, and returning to that same angle stops.
	// Deviation magnitude (in encoder detents) sets speed. This replaces the
	// old per-detent pulse+watchdog jog scheme entirely — holding a turned
	// position now keeps the motor running continuously.
	StepperAxis *m = (_selectedMotor == 0) ? &motorM1 : &motorM2;

	if (pressed) {
		m->stop();
		_buzzer.stop();
		_jogging = false;
		_state = UiState::SELECT;
		_needsRedraw = true;
		if (!_displayFrozen) { _drawSelectScreen(); _needsRedraw = false; }
		return;
	}

	constexpr uint8_t SPEED_PCT_PER_DETENT = 12;  // ~8-9 detents from center reaches 100%
	int32_t delta = _encReadAbsPos() - _encRefPos;

	if (delta == 0) {
		if (_jogging) {
			m->stop();
			_buzzer.stop();
			_jogging = false;
			_needsRedraw = true;
		}
	} else {
		int8_t dir = (delta > 0) ? 1 : -1;  // right/CW (positive) = forward
		int32_t magnitude = (delta > 0) ? delta : -delta;
		uint8_t speedPct = (uint8_t)((magnitude * SPEED_PCT_PER_DETENT > 100) ? 100 : magnitude * SPEED_PCT_PER_DETENT);
		m->jog(dir, speedPct);
		if (!_jogging) {
			_buzzer.tone(1000, 0);  // rings continuously while moving, see ui.h
			_needsRedraw = true;    // one full redraw on the idle->moving transition
		}
		_jogging = true;
		_curDir = dir;
		_curSpeedPct = speedPct;
	}

	static uint32_t lastPosRedrawMs = 0;
	uint32_t now = millis();
	bool periodicRefresh = _jogging && (now - lastPosRedrawMs >= 150);  // live position while moving
	if ((_needsRedraw || periodicRefresh) && !_displayFrozen) {
		lastPosRedrawMs = now;
		_drawControlScreen(_needsRedraw);
		_needsRedraw = false;
	}
}

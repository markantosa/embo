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
static uint32_t _lastMoveMs = 0;

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

static void IRAM_ATTR _isrEncoder() {
	uint8_t a = digitalRead(PIN_EC11_A);
	uint8_t b = digitalRead(PIN_EC11_B);
	uint8_t pinState = (a << 1) | b;
	_encState = _encTable[_encState & 0xF][pinState];
	uint8_t result = _encState & 0x30;
	if (result == DIR_CW) _encPendingStep -= 1;
	else if (result == DIR_CCW) _encPendingStep += 1;
}

static int _encReadStep() {
	noInterrupts();
	int step = _encPendingStep;
	_encPendingStep = 0;
	interrupts();
	return step;
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

// ── Drawing ──────────────────────────────────────────────────────────────────

static void _drawCentered(const char *text, int16_t cx, int16_t cy, uint16_t color, uint8_t size) {
	_tft.setTextColor(color);
	_tft.setTextSize(size);
	int16_t w = _tft.textWidth(text);
	int16_t h = _tft.fontHeight();
	_tft.setCursor(cx - w / 2, cy - h / 2);
	_tft.print(text);
}

struct BoxRect { int16_t x, y, w, h; };
static const BoxRect _box[2] = {
	{20, 60, 130, 120},
	{170, 60, 130, 120},
};

static void _drawSelectScreen() {
	_tft.fillScreen(TFT_BLACK);
	_drawCentered("Select a motor", 160, 25, TFT_DARKGREY, 2);

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

	_drawCentered("Rotate to choose, press to control", 160, 210, TFT_DARKGREY, 1);
}

static void _drawControlScreen(bool forceFull) {
	if (forceFull) {
		_tft.fillScreen(TFT_BLACK);
		char title[16];
		snprintf(title, sizeof(title), "Motor %u", _selectedMotor + 1);
		_drawCentered(title, 160, 30, TFT_WHITE, 3);
		_drawCentered("Press knob to stop", 160, 210, TFT_DARKGREY, 1);
	}

	StepperAxis *m = (_selectedMotor == 0) ? &motorM1 : &motorM2;
	char posBuf[24];
	snprintf(posBuf, sizeof(posBuf), "Position: %ld", (long)m->position());
	_tft.fillRect(0, 90, 320, 30, TFT_BLACK);
	_drawCentered(posBuf, 160, 100, TFT_WHITE, 2);

	_tft.fillRect(0, 130, 320, 30, TFT_BLACK);
	_drawCentered(_jogging ? "JOGGING" : "idle", 160, 140, _jogging ? TFT_GREEN : TFT_DARKGREY, 2);
}

// ── Public API ───────────────────────────────────────────────────────────────

void ui_init() {
	_tft.init();
	_tft.setRotation(3);

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

	int step = _encReadStep();
	bool pressed = _encSwWasPressed();

	if (_state == UiState::SELECT) {
		if (step != 0) {
			_selectedMotor = 1 - _selectedMotor;  // only two items — any turn toggles
			_needsRedraw = true;
		}
		if (pressed) {
			_state = UiState::CONTROL;
			_jogging = false;
			_needsRedraw = true;
		}
		if (_needsRedraw) { _drawSelectScreen(); _needsRedraw = false; }
		return;
	}

	// UiState::CONTROL
	StepperAxis *m = (_selectedMotor == 0) ? &motorM1 : &motorM2;

	if (pressed) {
		m->stop();
		_buzzer.stop();
		_jogging = false;
		_state = UiState::SELECT;
		_needsRedraw = true;
		_drawSelectScreen();
		_needsRedraw = false;
		return;
	}

	if (step != 0) {
		int8_t dir = (step > 0) ? 1 : -1;
		m->jog(dir, ENCODER_JOG_SPEED_PCT);
		_lastMoveMs = millis();
		if (!_jogging) {
			_jogging = true;
			_buzzer.tone(1000, 0);  // rings continuously while jogging, see ui.h
			_needsRedraw = true;
		}
	} else if (_jogging && (millis() - _lastMoveMs > ENCODER_JOG_WATCHDOG_MS)) {
		// Encoder stopped turning — auto-stop, mirrors the BLE jog watchdog's
		// role but tracked independently (separate control path, see ui.h).
		m->stop();
		_buzzer.stop();
		_jogging = false;
		_needsRedraw = true;
	}

	static uint32_t lastPosRedrawMs = 0;
	uint32_t now = millis();
	bool periodicRefresh = _jogging && (now - lastPosRedrawMs >= 150);  // live position while moving
	if (_needsRedraw || periodicRefresh) {
		lastPosRedrawMs = now;
		_drawControlScreen(_needsRedraw);
		_needsRedraw = false;
	}
}

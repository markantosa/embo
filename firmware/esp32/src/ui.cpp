#include "ui.h"
#include "config.h"
#include "scheduler.h"
#include "motors.h"
#include "rpi_uart.h"
#include "calibration.h"
#include "ble_debug.h"
#include "LGFX_Config.h"
#include <Arduino.h>

static LGFX _tft;

enum class UiState { SET_TARGET, RUNNING, DONE, VERIFYING, ERROR_SCREEN };
static UiState _state = UiState::SET_TARGET;
static UiState _verifyReturnState = UiState::SET_TARGET;
static bool _needsRedraw = true;
static char _resultMsg[32] = "";
static char _verifyResultMsg[40] = "";
static char _verifySpecMsg[16] = "";
static bool _verifyInSpec = false;
static char _errorMsg[48] = "";

// ── EC11 rotary encoder — quadrature decode (Buxton table), ported from
// testing/display_ui_testing/src/hal/encoder_driver.cpp, validated there as
// the most responsive option on the bench. ──────────────────────────────────

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

// ── EC11 push-switch — short press = confirm/start/dismiss, long press =
// request an optional camera size verification. Same short/long pattern as
// BTN1 below. ─────────────────────────────────────────────────────────────

static bool _encSwDown = false;
static uint32_t _encSwDownAtMs = 0;
static bool _encSwLongFired = false;

// Returns 1 for a short-press event, 2 for a long-press event (fires once
// as soon as the hold threshold is crossed), 0 for no event this call.
static int _encSwPollEvent() {
    bool down = (digitalRead(PIN_EC11_SW) == LOW);

    if (down) {
        if (!_encSwDown) {
            _encSwDown = true;
            _encSwDownAtMs = millis();
            _encSwLongFired = false;
        } else if (!_encSwLongFired && (millis() - _encSwDownAtMs) >= EC11_SW_LONGPRESS_MS) {
            _encSwLongFired = true;
            return 2;
        }
    } else {
        if (_encSwDown && !_encSwLongFired && (millis() - _encSwDownAtMs) >= EC11_SW_DEBOUNCE_MS) {
            _encSwDown = false;
            return 1;
        }
        _encSwDown = false;
    }
    return 0;
}

// ── BTN1 — dedicated stop/e-stop, short vs. long press ───────────────────────
// One safety-critical button with one job: this build does NOT use BTN1 to
// start a run (see ui.h) — start/confirm lives on the encoder's own switch.

static bool _btn1Down = false;
static uint32_t _btn1DownAtMs = 0;
static bool _btn1LongFired = false;

// Returns 1 for a short-press event (graceful stop), 2 for a long-press
// event (emergency stop, fires once as soon as the hold threshold is
// crossed rather than waiting for release), 0 for no event this call.
static int _btn1PollEvent() {
    bool down = (digitalRead(PIN_BTN1) == LOW);

    if (down) {
        if (!_btn1Down) {
            _btn1Down = true;
            _btn1DownAtMs = millis();
            _btn1LongFired = false;
        } else if (!_btn1LongFired && (millis() - _btn1DownAtMs) >= BTN1_LONGPRESS_MS) {
            _btn1LongFired = true;
            return 2;
        }
    } else {
        if (_btn1Down && !_btn1LongFired && (millis() - _btn1DownAtMs) >= BTN1_DEBOUNCE_MS) {
            _btn1Down = false;
            return 1;
        }
        _btn1Down = false;
    }
    return 0;
}

// ── Drawing ──────────────────────────────────────────────────────────────────

static void _drawCentered(const char *text, int16_t y, uint16_t color, uint8_t size) {
    _tft.setTextColor(color);
    _tft.setTextSize(size);
    int16_t w = _tft.textWidth(text);
    _tft.setCursor((_tft.width() - w) / 2, y);
    _tft.print(text);
}

static void _drawSetTargetScreen() {
    _tft.fillScreen(TFT_BLACK);
    _drawCentered("EMBO", 30, TFT_WHITE, 3);
    _drawCentered("Set target size", 90, TFT_DARKGREY, 2);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u um", scheduler_get_target_um());
    _drawCentered(buf, 130, TFT_WHITE, 4);

    const char *ready = motors_is_homed() ? "Press knob to start" : "NOT HOMED";
    _drawCentered(ready, 195, motors_is_homed() ? TFT_DARKGREY : TFT_RED, 2);
    _drawCentered("Hold knob to verify with camera", 225, TFT_DARKGREY, 1);
}

// Animated spinner dots, shared by the running and verifying screens.
static uint8_t _spinnerFrame = 0;
static uint32_t _lastSpinnerMs = 0;

static void _drawSpinner(int16_t cy) {
    uint32_t now = millis();
    if (now - _lastSpinnerMs >= 150) {
        _lastSpinnerMs = now;
        _spinnerFrame = (_spinnerFrame + 1) % 4;
        int16_t cx = _tft.width() / 2;
        _tft.fillRect(cx - 40, cy - 10, 80, 20, TFT_BLACK);
        char dots[5] = "";
        for (uint8_t i = 0; i < _spinnerFrame; i++) dots[i] = '.';
        dots[_spinnerFrame] = '\0';
        _drawCentered(dots, cy - 8, TFT_WHITE, 2);
    }
}

// Deliberately NOT a live sensor readout. Raw sensor/diagnostic data belongs
// on the BLE dashboard (testing/PCB_Test_Firmware_v3_4/web), not this
// screen — see ui.h.
static void _drawRunningScreen(bool forceFull) {
    if (forceFull) {
        _tft.fillScreen(TFT_BLACK);
        _drawCentered("Mixing", 100, TFT_WHITE, 3);
        _drawCentered("Hold BTN1 for emergency stop", 220, TFT_DARKGREY, 1);
    }
    _drawSpinner(160);
}

static void _drawDoneScreen() {
    _tft.fillScreen(TFT_BLACK);
    _drawCentered(_resultMsg, 100, TFT_GREEN, 2);
    _drawCentered("Press knob for new run", 190, TFT_DARKGREY, 2);
    _drawCentered("Hold knob to verify with camera", 220, TFT_DARKGREY, 1);
}

// Optional, operator-triggered, single-shot CV check — NOT part of the
// mixing loop itself (see scheduler.h). Shows a spinner while the RPi
// captures+processes one frame, then the result (or a timeout notice).
static void _drawVerifyingScreen(bool waiting, bool forceFull) {
    if (forceFull) {
        _tft.fillScreen(TFT_BLACK);
        _drawCentered("Camera Verify", 60, TFT_WHITE, 3);
    }
    if (waiting) {
        if (forceFull) _drawCentered("Capturing...", 130, TFT_DARKGREY, 2);
        _drawSpinner(180);
    } else {
        _tft.fillRect(0, 110, _tft.width(), 90, TFT_BLACK);
        _drawCentered(_verifyResultMsg, 140, TFT_WHITE, 2);
        if (_verifySpecMsg[0] != '\0') {
            _drawCentered(_verifySpecMsg, 170, _verifyInSpec ? TFT_GREEN : TFT_RED, 2);
        }
        _drawCentered("Press knob to continue", 220, TFT_DARKGREY, 2);
    }
}

static void _drawErrorScreen() {
    _tft.fillScreen(TFT_BLACK);
    _drawCentered("HARDWARE FAULT", 100, TFT_RED, 2);
    _drawCentered(_errorMsg, 140, TFT_RED, 2);
    _drawCentered("Reboot required", 200, TFT_DARKGREY, 2);
}

// ── Verification flow ────────────────────────────────────────────────────────

static void _startVerification(UiState returnState) {
    _verifyReturnState = returnState;
    _verifyResultMsg[0] = '\0';
    _verifySpecMsg[0] = '\0';
    rpi_request_capture();
    _state = UiState::VERIFYING;
    _needsRedraw = true;
}

// ── Public API ───────────────────────────────────────────────────────────────

void ui_init() {
    _tft.init();
    _tft.setRotation(3);

    pinMode(PIN_BTN1, INPUT_PULLUP);
    pinMode(PIN_EC11_SW, INPUT_PULLUP);
    pinMode(PIN_EC11_A, INPUT_PULLUP);
    pinMode(PIN_EC11_B, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_EC11_A), _isrEncoder, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_EC11_B), _isrEncoder, CHANGE);

    _tft.fillScreen(TFT_BLACK);
    _drawCentered("EMBO", 110, TFT_WHITE, 4);
    _drawCentered("initializing...", 160, TFT_DARKGREY, 2);

    _state = UiState::SET_TARGET;
    _needsRedraw = true;
}

void ui_show_error(const char *msg) {
    strncpy(_errorMsg, msg, sizeof(_errorMsg) - 1);
    _errorMsg[sizeof(_errorMsg) - 1] = '\0';
    _state = UiState::ERROR_SCREEN;
    _needsRedraw = true;
    ble_log("UI: HARDWARE FAULT - %s", msg);
}

void ui_update() {
    if (_state == UiState::ERROR_SCREEN) {
        if (_needsRedraw) { _drawErrorScreen(); _needsRedraw = false; }
        return;  // no way out short of reboot
    }

    switch (_state) {
    case UiState::SET_TARGET: {
        int step = _encReadStep();
        if (step != 0) {
            int32_t newTarget = (int32_t)scheduler_get_target_um() + step * TARGET_SIZE_UM_STEP;
            if (newTarget < TARGET_SIZE_UM_MIN) newTarget = TARGET_SIZE_UM_MIN;
            if (newTarget > TARGET_SIZE_UM_MAX) newTarget = TARGET_SIZE_UM_MAX;
            scheduler_set_target_um((uint16_t)newTarget);
            _needsRedraw = true;
        }

        int swEvent = _encSwPollEvent();
        if (swEvent == 1 && motors_is_homed()) {
            scheduler_start();
            _state = UiState::RUNNING;
            _needsRedraw = true;
        } else if (swEvent == 2) {
            _startVerification(UiState::SET_TARGET);
        }

        // BTN1 has no function while idle (dedicated stop button, nothing running yet).
        _btn1PollEvent();

        if (_needsRedraw) { _drawSetTargetScreen(); _needsRedraw = false; }
        break;
    }

    case UiState::RUNNING: {
        int btnEvent = _btn1PollEvent();
        if (btnEvent == 2) {
            scheduler_emergency_stop();
            strncpy(_resultMsg, "STOPPED (e-stop)", sizeof(_resultMsg) - 1);
            _state = UiState::DONE;
            _needsRedraw = true;
            break;
        } else if (btnEvent == 1) {
            scheduler_stop();  // takes effect once the in-progress stroke finishes
        }

        if (scheduler_target_reached()) {
            if (scheduler_hit_safety_cap()) {
                strncpy(_resultMsg, "Stopped: safety cap", sizeof(_resultMsg) - 1);
            } else {
                strncpy(_resultMsg, "Target size reached", sizeof(_resultMsg) - 1);
            }
            _state = UiState::DONE;
            _needsRedraw = true;
            break;
        }

        if (!scheduler_is_running()) {
            // Graceful stop finished taking effect.
            strncpy(_resultMsg, "Stopped", sizeof(_resultMsg) - 1);
            _state = UiState::DONE;
            _needsRedraw = true;
            break;
        }

        _drawRunningScreen(_needsRedraw);
        _needsRedraw = false;
        break;
    }

    case UiState::DONE: {
        _btn1PollEvent();
        int swEvent = _encSwPollEvent();
        if (swEvent == 1) {
            _state = UiState::SET_TARGET;
            _needsRedraw = true;
        } else if (swEvent == 2) {
            _startVerification(UiState::DONE);
        }
        if (_needsRedraw) { _drawDoneScreen(); _needsRedraw = false; }
        break;
    }

    case UiState::VERIFYING: {
        _btn1PollEvent();  // stays live even here, though nothing is running to stop

        RpiCaptureStatus status = rpi_capture_status();
        if (status == RpiCaptureStatus::RESULT_READY) {
            int16_t median, iqr;
            rpi_pop_capture_result(median, iqr);
            // Feeds the (diagnostic-only) breakage-model fit for cross-
            // checking against the live sensor-fusion estimate in future
            // runs — does not alter the run already done, and does not
            // itself drive the stop condition (see scheduler.h).
            calib_breakage_add_point(motor_get_stroke_count(), (float)median);
            snprintf(_verifyResultMsg, sizeof(_verifyResultMsg), "Median %d um   IQR %d um", median, iqr);
            _verifyInSpec = (abs((int)median - (int)scheduler_get_target_um()) <= TARGET_TOLERANCE_UM);
            strncpy(_verifySpecMsg, _verifyInSpec ? "IN SPEC" : "OUT OF SPEC", sizeof(_verifySpecMsg) - 1);
            _needsRedraw = true;
        } else if (status == RpiCaptureStatus::TIMED_OUT) {
            int16_t dummyMedian, dummyIqr;
            rpi_pop_capture_result(dummyMedian, dummyIqr);  // consumes/clears the timeout flag
            strncpy(_verifyResultMsg, "No response from RPi", sizeof(_verifyResultMsg) - 1);
            _verifySpecMsg[0] = '\0';
            _needsRedraw = true;
        }

        bool waiting = (_verifyResultMsg[0] == '\0');
        if (_needsRedraw) { _drawVerifyingScreen(waiting, true); _needsRedraw = false; }
        else if (waiting) { _drawVerifyingScreen(waiting, false); }  // keep animating the spinner

        if (!waiting && _encSwPollEvent() == 1) {
            _state = _verifyReturnState;
            _needsRedraw = true;
        }
        break;
    }

    case UiState::ERROR_SCREEN:
        break;  // handled above
    }
}

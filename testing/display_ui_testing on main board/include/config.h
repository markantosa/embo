#pragma once

// Pin map corrected to match the actual EMBO v3.4 main board (was: generic
// bare-devkit test pins that don't correspond to anything wired on the real
// PCB — see docs/EMBO_PCB_Design_Brief_v3_4.txt / docs/EMBO_Pinout_Cheatsheet.txt).
// This project is a UI-library trial (LovyanGFX + encoder), now aimed at the
// real board so it can be flashed and actually drive the physical TFT/encoder.

// ---- Display (SPI) ----
// No MISO on this bus as of v3.3 — touch's data-out (T_DO) moved to GPIO46
// and this project doesn't use touch; ILI9341 doesn't need read-back either.
#define TFT_SCK   36   // PIN_SPI_CLK
#define TFT_MOSI  35   // PIN_SPI_MOSI
#define TFT_CS    39   // PIN_TFT_CS
#define TFT_DC    40   // PIN_TFT_DC
#define TFT_RST   41   // PIN_TFT_RST

// ---- Encoder (EC11, via IDC ribbon) ----
#define ENC_CLK   16   // PIN_EC11_A
#define ENC_DT    17   // PIN_EC11_B
#define ENC_SW    18   // PIN_EC11_SW

// ---- Buzzer ----
#define BUZZER_PIN   13   // PIN_BUZ_PWM

// ---- Buttons ----
// The real v3.4 board has exactly ONE physical button (BTN1, GPIO11) — BTN2
// was removed in v3.3 (one-button UI decision, see design brief §8.1). Both
// logical roles below share it until/unless this test app is redesigned
// around a single button; btnNext isn't currently read by the app state
// machine anyway (see app_state_machine.h), only btnSelect (reset) is.
#define BTN_NEXT_PIN   11  // PIN_BTN1 — currently unused by app logic
#define BTN_SELECT_PIN 11  // PIN_BTN1 — global reset

#define BUTTON_DEBOUNCE_MS 40
#define PERCENT_MIN 0
#define PERCENT_MAX 100
#define PERCENT_STEP 1 //step change per detent

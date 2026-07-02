#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// ---- Display pin configuration ----
#define TFT_CS   18
#define TFT_DC   19
#define TFT_RST  20
#define TFT_SCK  6
#define TFT_MOSI 7
#define TFT_MISO 21  // now used — shared bus, needed for touch

// ---- Touch pin configuration ----
#define TOUCH_CS 22  // touch's own CS, shares SCK/MOSI/MISO with display

Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS);  // no IRQ pin — we'll poll

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  touch.begin();

  tft.fillScreen(ILI9341_BLACK);
  Serial.println("Display + touch initialized.");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    Serial.print("Touch raw X: ");
    Serial.print(p.x);
    Serial.print("  Y: ");
    Serial.println(p.y);
  }
  delay(50);
}
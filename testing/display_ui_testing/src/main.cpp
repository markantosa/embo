#include <config.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>


Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS);  // no IRQ pin — we'll poll
volatile int encoderPos = 0;
volatile bool lastCLK = HIGH;

void IRAM_ATTR handleEncoder() {
  bool clkState = digitalRead(ENC_CLK);
  if (clkState != lastCLK) {
    if (digitalRead(ENC_DT) != clkState) {
      encoderPos++;
    } else {
      encoderPos--;
    }
  }
  lastCLK = clkState;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  touch.begin();

  tft.fillScreen(ILI9341_BLACK);
  Serial.println("Display + touch initialized.");
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_CLK), handleEncoder, CHANGE);
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

  static int lastPos = 0;
  if (encoderPos != lastPos) {
    Serial.print("Encoder position: ");
    Serial.println(encoderPos);
    lastPos = encoderPos;
  }

  if (digitalRead(ENC_SW) == LOW) {
    Serial.println("Encoder pressed");
    delay(200);
  }

  if (digitalRead(BTN1) == LOW) {
    Serial.println("Button 1 pressed");
    delay(200);
  }

  if (digitalRead(BTN2) == LOW) {
    Serial.println("Button 2 pressed");
    delay(200);
  }
}
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ---- Pin configuration ----
#define TFT_CS   18
#define TFT_DC   19
#define TFT_RST  20
#define TFT_SCK  6
#define TFT_MOSI 7
#define TFT_MISO -1  // unused, display-only

Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();

  Serial.println("Display initialized, starting color cycle...");
}

void loop() {
  Serial.println("RED");
  tft.fillScreen(ILI9341_RED);
  delay(1000);

  Serial.println("GREEN");
  tft.fillScreen(ILI9341_GREEN);
  delay(1000);

  Serial.println("BLUE");
  tft.fillScreen(ILI9341_BLUE);
  delay(1000);

  Serial.println("WHITE");
  tft.fillScreen(ILI9341_WHITE);
  delay(1000);

  Serial.println("BLACK");
  tft.fillScreen(ILI9341_BLACK);
  delay(1000);
}
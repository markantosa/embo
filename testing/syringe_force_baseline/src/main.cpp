#include <Arduino.h>
#include <Wire.h>

#include <HX711.h>
#include <U8g2lib.h>


// =======================
// Pins
// =======================

#define HX711_DOUT 6
#define HX711_SCK  7


#define OLED_SDA 21
#define OLED_SCL 20




// =======================
// Objects
// =======================

HX711 scale;


// 1.3 inch OLED (SH1106)
// If your OLED is SSD1306 change this later
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0,
    U8X8_PIN_NONE
);



// =======================
// Calibration
// =======================

float calibration_factor = -7050;
// change this after calibration



void setup() {


    Serial.begin(115200);

    delay(1000);

    Serial.println("Starting...");



    // I2C

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );



    // OLED

    oled.begin();

    oled.clearBuffer();

    oled.setFont(
        u8g2_font_ncenB08_tr
    );

    oled.drawStr(
        0,
        20,
        "Loadcell Start"
    );

    oled.sendBuffer();



    // HX711

    scale.begin(
        HX711_DOUT,
        HX711_SCK
    );


    delay(500);


    if(scale.is_ready()) {

        Serial.println("HX711 detected");

    }
    else {

        Serial.println("HX711 NOT detected");

    }



    scale.set_scale(
        calibration_factor
    );


    Serial.println("Taring...");

    scale.tare();


    Serial.println("Ready");

}



void loop() {


    float weight = 0;


    if(scale.is_ready()) {


        weight =
            scale.get_units(5);



        Serial.print("Weight: ");

        Serial.print(weight);

        Serial.println(" g");



        oled.clearBuffer();



        oled.setFont(
            u8g2_font_ncenB08_tr
        );


        oled.drawStr(
            0,
            15,
            "Weight"
        );



        oled.setFont(
            u8g2_font_fub20_tr
        );


        char buffer[20];

        sprintf(
            buffer,
            "%.2f g",
            weight
        );


        oled.drawStr(
            0,
            45,
            buffer
        );


        oled.sendBuffer();



    }

    else {


        Serial.println(
            "HX711 waiting"
        );


        oled.clearBuffer();

        oled.setFont(
            u8g2_font_ncenB08_tr
        );

        oled.drawStr(
            0,
            30,
            "HX711 ERROR"
        );

        oled.sendBuffer();


    }


    delay(500);

}
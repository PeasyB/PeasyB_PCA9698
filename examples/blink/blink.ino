// Blink one PCA9698 output.
//
// OE is active-low after power-up. Tie OE to GND (or call
// gpio.setOeActiveHigh(true) and tie OE high) or the pin will stay
// high-impedance and the LED will not blink.
//
// The default address 0x20 is AD2, AD1, and AD0 all tied to GND.
// Pass another address to the constructor if your straps differ:
//   PeasyB_PCA9698 gpio(0x21);

#include <Wire.h>
#include <PeasyB_PCA9698.h>

// IO0_0. Any name in PeasyB_PCA9698::Pin, or a number from 0 to 39, works here.
const uint8_t LED = PeasyB_PCA9698::IO0_0;

PeasyB_PCA9698 gpio;

void setup() {
    Serial.begin(115200);

    // Select this board's SDA and SCL pins before begin(), if the core
    // does not already default to the bus the PCA9698 is wired to.
    Wire.begin();
    Wire.setClock(400000);

    if (!gpio.begin()) {
        Serial.println("PCA9698 did not ACK at 0x20");
        while (true) {
            delay(1000);
        }
    }

    gpio.pinMode(LED, OUTPUT);
}

void loop() {
    gpio.digitalWrite(LED, HIGH);
    delay(500);
    gpio.digitalWrite(LED, LOW);
    delay(500);
}

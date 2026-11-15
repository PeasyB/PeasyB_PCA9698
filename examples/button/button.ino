// Copy a button onto an LED through the PCA9698.
//
// The expander has no internal pull-ups. Wire the button between the input
// pin and GND, and fit a resistor of about 10 kΩ from that pin up to VDD.
// The LED goes from the output pin to GND through its own series resistor.
//
// OE must be asserted or the LED pin will not drive. With the power-up
// polarity, that means OE tied to GND.

#include <Wire.h>
#include <PeasyB_PCA9698.h>

const uint8_t BUTTON = PeasyB_PCA9698::IO0_0;
const uint8_t LED = PeasyB_PCA9698::IO0_1;

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

    if (!gpio.pinMode(BUTTON, INPUT) || !gpio.pinMode(LED, OUTPUT)) {
        Serial.println("Pin setup failed");
        while (true) {
            delay(1000);
        }
    }
}

void loop() {
    const int pressed = gpio.digitalRead(BUTTON);
    if (pressed < 0) {
        Serial.println("Read failed");
        delay(1000);
        return;
    }

    // The button shorts to GND, so a press reads LOW. Light the LED while it is down.
    gpio.digitalWrite(LED, pressed == LOW ? HIGH : LOW);
}

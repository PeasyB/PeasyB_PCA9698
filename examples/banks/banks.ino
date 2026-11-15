// Write a whole bank at once, and read all five banks back.
//
// Useful when several pins on the same bank should change together.
// For every bank in the chip to change on the same I²C STOP, call
// gpio.setOutputUpdate(PeasyB_PCA9698::OutputUpdate::ON_STOP) and then
// gpio.writeAll().

#include <Wire.h>
#include <PeasyB_PCA9698.h>

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

    // Bank 0, every pin an output. In the direction byte, 0 means output.
    gpio.writeDirection(0, 0x00);
}

void loop() {
    // Bit 0 of the value is IO0_0. 0x01, then 0x02, then 0x04, and so on.
    static uint8_t pattern = 0x01;
    gpio.writeBank(0, pattern);
    pattern = (pattern == 0x80) ? 0x01 : static_cast<uint8_t>(pattern << 1);

    uint8_t pins[PeasyB_PCA9698::BANK_COUNT];
    if (gpio.readAll(pins)) {
        Serial.print("Inputs:");
        for (uint8_t bank = 0; bank < PeasyB_PCA9698::BANK_COUNT; ++bank) {
            Serial.print(" ");
            Serial.print(pins[bank], HEX);
        }
        Serial.println();
    }

    delay(200);
}

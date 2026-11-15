// RESET, OE, and INT from GPIOs on the host.
//
// Set the three constants to the pin numbers on your board. Leave one at
// -1 to skip it. Nothing in here is tied to a particular microcontroller.
//
// INT and RESET need pull-ups to VDD. With the power-up polarity, OE needs
// a pull-down to GND so the outputs stay enabled until this sketch drives
// the pin. About 10 kΩ is enough: the GPIO overrides that pull.

#include <Wire.h>
#include <PeasyB_PCA9698.h>

const int8_t PIN_RESET = -1;
const int8_t PIN_OE = -1;
const int8_t PIN_INT = -1;

const uint8_t LED = PeasyB_PCA9698::IO0_0;
const uint8_t SENSE = PeasyB_PCA9698::IO0_1;

PeasyB_PCA9698 gpio;
volatile bool intFlag = false;

void onIntFalling() {
    intFlag = true;
}

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

    if (PIN_RESET >= 0) {
        gpio.useResetPin(static_cast<uint8_t>(PIN_RESET));
        if (!gpio.hardReset()) {
            Serial.println("Reset pulse failed");
        }
    }
    if (PIN_OE >= 0) {
        gpio.useOePin(static_cast<uint8_t>(PIN_OE));
    }
    if (PIN_INT >= 0) {
        gpio.useInterruptPin(static_cast<uint8_t>(PIN_INT));
        if (!gpio.onInterrupt(onIntFalling)) {
            Serial.println("This GPIO cannot interrupt");
        }
    }

    gpio.pinMode(LED, OUTPUT);
    gpio.pinMode(SENSE, INPUT);
    // A change on SENSE pulls INT low. Reading that bank lets it go high again.
    gpio.setInterrupt(SENSE, true);
}

void loop() {
    gpio.digitalWrite(LED, HIGH);
    delay(400);

    if (PIN_OE >= 0) {
        gpio.enableOutputs(false);
        delay(200);
        gpio.enableOutputs(true);
    }

    gpio.digitalWrite(LED, LOW);
    delay(400);

    if (intFlag) {
        intFlag = false;
        uint8_t banks[PeasyB_PCA9698::BANK_COUNT];
        if (gpio.readAll(banks)) {
            Serial.print("INT, bank 0 = ");
            Serial.println(banks[0], HEX);
        }
    }
}

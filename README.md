# PeasyB PCA9698

Arduino library for the [NXP PCA9698](https://www.nxp.com/docs/en/data-sheet/PCA9698.pdf), a 40-bit Fast-mode Plus I²C GPIO expander. The API is modelled on the Adafruit MCP23X17 library, so `pinMode`, `digitalRead`, and `digitalWrite` behave the way they do there. On top of that it covers the PCA9698 itself: open-drain outputs, per-pin interrupt masks, polarity inversion, and banks that change together on I²C STOP.

```cpp
#include <Wire.h>
#include <PeasyB_PCA9698.h>

PeasyB_PCA9698 gpio;

void setup() {
    Wire.begin();
    gpio.begin();                 // 0x20, which is AD2 = AD1 = AD0 = GND
    gpio.pinMode(PeasyB_PCA9698::IO0_0, OUTPUT);
}

void loop() {
    gpio.digitalToggle(PeasyB_PCA9698::IO0_0);
    delay(500);
}
```

## Install

The published copy of this library is [PeasyB_PCA9698_Library](https://github.com/PeasyB/PeasyB_PCA9698_Library). Download that repository as a ZIP and choose **Sketch → Include Library → Add .ZIP Library**, or clone it into the Arduino libraries directory:

```bash
git clone https://github.com/PeasyB/PeasyB_PCA9698_Library.git ~/Arduino/libraries/PeasyB_PCA9698_Library
```

The same sources are also in the [breakout repository](https://github.com/PeasyB/PCA9698-Breakout), in the `PeasyB_PCA9698_Library` folder. Copy that folder into `libraries` if you already have the board files checked out. The library does not require that board.

## What you need to know before the first write

- **OE gates every output.** After power-up, OE is active-low. Hold OE at GND, or call `setOeActiveHigh(true)` and hold OE high. A floating OE pin leaves every output high-impedance, and `digitalWrite()` will look like it did nothing.
- **There are no internal pull-ups.** `pinMode(pin, INPUT_PULLUP)` returns false. Fit the resistor on the board.
- **Addresses in `Wire` are 7-bit.** The datasheet prints the 8-bit write address (0x40 when all AD pins are GND). This library, like the rest of Arduino, uses 0x20 for that strap. `addressFor()` converts any AD2/AD1/AD0 combination.
- **INT is open-drain and active-low**, and it is masked off until you call `setInterrupt()`. Reading an input bank clears the interrupt for that whole bank.
- **Output drive is shared.** Bank 0 is programmed in pairs (IO0_0 with IO0_1, and so on). Banks 1–4 have one drive setting each. The power-up drive is totem-pole.

## Pin numbers

`IO0_0` is pin 0 and `IO4_7` is pin 39. `pinNumber(bank, bit)` builds the same number, and the `Pin` enumerators spell the datasheet names:

```cpp
gpio.pinMode(PeasyB_PCA9698::IO2_3, OUTPUT);   // pin 19
gpio.digitalWrite(gpio.pinNumber(2, 3), HIGH);
```

| Bank | Pins | Library numbers |
| --- | --- | --- |
| IO0 | IO0_0 … IO0_7 | 0–7 |
| IO1 | IO1_0 … IO1_7 | 8–15 |
| IO2 | IO2_0 … IO2_7 | 16–23 |
| IO3 | IO3_0 … IO3_7 | 24–31 |
| IO4 | IO4_0 … IO4_7 | 32–39 |

`readBank()` returns the **pin levels** (the Input Port register). `writeBank()` writes the **output latch**. Those only match when the pin is an output, OE is asserted, and nothing else is loading the net. Polarity inversion affects reads, not the level an output drives.

## Address

| AD2 | AD1 | AD0 | 7-bit address |
| --- | --- | --- | --- |
| GND | GND | GND | 0x20 |
| GND | GND | VDD | 0x21 |
| GND | VDD | GND | 0x22 |
| GND | VDD | VDD | 0x23 |
| VDD | GND | GND | 0x24 |
| VDD | GND | VDD | 0x25 |
| VDD | VDD | GND | 0x26 |
| VDD | VDD | VDD | 0x27 |

Tying an AD pin to SCL or SDA selects the other straps, 64 in all. `PeasyB_PCA9698::addressFor()` covers that table, including the block at 0x70–0x77.

```cpp
const uint8_t addr = PeasyB_PCA9698::addressFor(
    PeasyB_PCA9698::AddrLevel::VDD,
    PeasyB_PCA9698::AddrLevel::GND,
    PeasyB_PCA9698::AddrLevel::GND);
PeasyB_PCA9698 gpio(addr);
```

## Using it

Start the I²C peripheral yourself, on the pins your board uses for the bus, and choose the clock. `begin()` does not call `Wire.begin()`, so pin selection stays in the sketch.

```cpp
Wire.begin();
Wire.setClock(400000);   // the chip will also run at 1 MHz

if (!gpio.begin()) {
    // lastError() is I2C_ERROR when the address did not ACK
}
```

`begin()` reads the configuration back, so the cache matches a chip that was already set up. `digitalWrite()` then updates the cached latch and writes a single Output Port byte.

To change a whole bank in one transaction, use `writeBank()`. To change all five banks on the same STOP:

```cpp
gpio.setOutputUpdate(PeasyB_PCA9698::OutputUpdate::ON_STOP);
uint8_t banks[5] = {0x0F, 0x00, 0xFF, 0x00, 0x01};
gpio.writeAll(banks);
```

Open-drain on bank 3 (the setting covers IO3_0 through IO3_7):

```cpp
gpio.setBankOutputDrive(3, PeasyB_PCA9698::OutputDrive::OPEN_DRAIN);
```

Interrupt on one input. INT goes low when that pin changes, and stays low until the bank is read:

```cpp
gpio.pinMode(PeasyB_PCA9698::IO1_0, INPUT);
gpio.setInterrupt(PeasyB_PCA9698::IO1_0, true);
```

Direction and mask bytes use the datasheet polarity. A 1 in a direction byte is an **input**. A 1 in a mask byte **hides** the interrupt. `pinMode()` and `setInterrupt()` are the easier calls; `writeDirection()` and `writeInterruptMask()` are there when you want the raw byte.

`forceBanks(0x1F, false)` drives every output low without rewriting the five latch registers. The next `digitalWrite()` or `writeBank()` on a forced bank puts that bank back under latch control.

`readDeviceId()` reads the 24-bit ID through the reserved Device ID address. The PCA9698 field in the datasheet is all zeros (manufacturer 0, part 0, revision 0), so a successful read looks empty. That is the value NXP published, and the call still checks that the device answered. `enableAllCall(true)` lets the chip answer the write-only GPIO All Call at 0x6E, which is not the I²C general-call address. `allCallWrite()` sends that broadcast and does not update the cache; call `refresh()` afterwards on every chip that took part.

`lastError()` returns the most recent `Error` and then clears it.

## RESET, OE, and INT

Pass the host GPIO numbers wired to those pins. The numbers belong to the sketch, not the library.

```cpp
gpio.useResetPin(RESET_PIN);       // held high; hardReset() pulses it low
gpio.useOePin(OE_PIN);             // outputs enabled, using the current polarity
gpio.useInterruptPin(INT_PIN);     // input; add true to turn on an internal pull-up

gpio.enableOutputs(false);         // tri-state every output
gpio.hardReset();                  // pulse RESET, then re-read the registers

if (gpio.interruptAsserted() == 1) {
    uint8_t banks[5];
    gpio.readAll(banks);           // reading the changed banks releases INT
}
```

`onInterrupt()` runs a function on the falling edge. That function is interrupt context: set a flag there, and do the I²C read from `loop()`. A 10 kΩ pull to the idle level (RESET and INT high, OE low for the power-up polarity) holds the pin while the GPIO is still high-Z, and the GPIO can override that pull afterwards.

## Wiring INT, RESET, and OE

Connect each pin **directly** to a GPIO. Put the 10 kΩ resistor from the pin to a rail, not in series with the wire.

| Pin | Direction | Resistor | Why |
| --- | --- | --- | --- |
| INT | Open-drain output, active low | 10 kΩ to 3.3 V | The chip can only pull low. A host GPIO pull-up works too, if that pin has one. |
| RESET | Input, active low | 10 kΩ to 3.3 V | Stays out of reset while the GPIO is high-Z during boot. Drive the GPIO low to reset. |
| OE | Input, active low by default | 10 kΩ to GND | Outputs stay enabled if the GPIO is high-Z. Drive the GPIO high to tri-state them. Use a pull-up instead if the outputs should stay off until software enables them. |

A series 10 kΩ only slows the edge. It does not replace the pull resistor on INT. A pull of about 10 kΩ to the idle rail is weak enough that the host GPIO can override it, so the switch or resistor can stay in the idle position while software drives the pin.

## Examples

| Sketch | What it shows |
| --- | --- |
| `examples/blink` | One output, toggled twice a second. |
| `examples/button` | An input with an external pull-up copied onto an output. |
| `examples/banks` | A walking bit written with `writeBank()`, and all five banks read back. |
| `examples/control_pins` | Host GPIOs for RESET, OE, and INT. Set the three pin numbers first. |

## API

Doxygen comments on every public method are in `src/PeasyB_PCA9698.h`. From this folder:

```bash
doxygen Doxyfile
```

The HTML lands in `docs/doxygen`. That directory is generated output.

## Files

| Path | Purpose |
| --- | --- |
| `src/PeasyB_PCA9698.h` | Class, pin names, and the datasheet notes. |
| `src/PeasyB_PCA9698.cpp` | I²C and the register cache. |
| `library.properties` | Arduino Library Manager metadata. |
| `keywords.txt` | IDE highlighting. |
| `examples/` | Blink, button, banks, and the RESET / OE / INT sketch. |

## Hardware check

Version 1.0.0 was run on a PCA9698DGG at 7-bit address 0x20 and 100 kHz. That pass covered pin reads and writes, polarity, open-drain, OE in both polarities, RESET, interrupt mask and clear, the SMBus alert response, GPIO All Call, and releasing an ALLBNK force. Output-change-on-STOP follows the datasheet; the ACK-versus-STOP edge itself is not visible from these calls, because each one ends the I²C transaction before it returns.

## License

MIT. Copyright 2026 PeasyB. See `LICENSE`.

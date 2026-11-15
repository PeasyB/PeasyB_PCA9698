/**
 * @file PeasyB_PCA9698.h
 * @brief Arduino library for the NXP PCA9698 40-bit I²C GPIO expander.
 *
 * The PCA9698 is a Fast-mode Plus (up to 1 MHz) device with 40 I/Os arranged
 * as five banks of eight. Each pin can be an input or an output. Outputs are
 * totem-pole after power-up and can be switched to open-drain in pairs on
 * bank 0, or per bank on banks 1–4. There are no internal pull resistors.
 *
 * Pin numbers in this library match the datasheet names: IO0_0 is 0,
 * IO0_7 is 7, IO1_0 is 8, and IO4_7 is 39. @c PeasyB_PCA9698::pinNumber()
 * builds that number from a bank and a bit, and the @c Pin enumerators
 * spell the names out.
 *
 * The public surface is modelled on the Adafruit MCP23X17 library
 * (`begin`, `pinMode`, `digitalRead`, `digitalWrite`) and then exposes the
 * PCA9698 registers: output structure, interrupt masks, polarity inversion,
 * output-change-on-STOP, GPIO All Call, SMBus Alert, and the device ID.
 *
 * Register map and bit meanings follow NXP document PCA9698, Rev. 3
 * (3 August 2010).
 *
 * @author PeasyB
 * @license MIT
 * @see https://www.nxp.com/docs/en/data-sheet/PCA9698.pdf
 */

#ifndef PEASYB_PCA9698_H
#define PEASYB_PCA9698_H

#include <Arduino.h>
#include <Wire.h>

/**
 * @class PeasyB_PCA9698
 * @brief Driver for one PCA9698 on an Arduino `TwoWire` bus.
 *
 * Construct one object per chip. Call `Wire.begin()` before `begin()`,
 * after selecting SDA and SCL if this board does not use the core's default
 * I²C pins. The default 7-bit address is 0x20, which is what the chip
 * enumerates when AD2, AD1, and AD0 are all tied to GND.
 *
 * Output-latch, direction, polarity, interrupt-mask, OUTCONF, and MODE
 * values are cached after `begin()`. `digitalWrite()` updates the cache and
 * writes one Output Port register, so a single LED toggle is one I²C
 * transaction rather than a read-modify-write of the pins. Call `refresh()`
 * if something else writes the same chip.
 */
class PeasyB_PCA9698 {
public:
    /// Number of I/O banks. Bank 0 is IO0_x, bank 4 is IO4_x.
    static constexpr uint8_t BANK_COUNT = 5;

    /// Number of GPIO pins. Legal pin numbers are 0 through 39.
    static constexpr uint8_t PIN_COUNT = 40;

    /**
     * 7-bit address when AD2 = AD1 = AD0 = GND (VSS).
     *
     * The datasheet "Address" column is the 8-bit write address (0x40 for
     * this strap). Arduino `Wire` wants the 7-bit form, which is 0x20.
     */
    static constexpr uint8_t ADDRESS_DEFAULT = 0x20;

    /// How an AD pin is strapped. GND and VSS are the same electrical level.
    enum class AddrLevel : uint8_t {
        VSS = 0, ///< Tied to ground.
        GND = 0, ///< Alias of VSS.
        VDD = 1, ///< Tied to the chip supply.
        SCL = 2, ///< Tied to the I²C clock.
        SDA = 3  ///< Tied to the I²C data line.
    };

    /// What a digital output driver does when it is asked to drive high.
    enum class OutputDrive : uint8_t {
        OPEN_DRAIN = 0, ///< High is high-Z. An external pull-up makes the high.
        TOTEM_POLE = 1  ///< Push-pull. This is the power-up default.
    };

    /// When a write to an Output Port register shows up on the pins.
    enum class OutputUpdate : uint8_t {
        /**
         * Pins change during the ACK of each Output Port byte.
         * Power-up default. Banks update one after another.
         */
        ON_ACK = 1,
        /**
         * Pins hold their previous levels until I²C STOP, then the banks
         * written in that transaction change together. After a write with
         * this mode selected, the chip ignores its own address until STOP.
         * `writeBank()` and `writeAll()` always end with STOP, so the update
         * has happened by the time those calls return.
         */
        ON_STOP = 0
    };

    /// Result of the most recent call. `lastError()` reads and clears it.
    enum Error : uint8_t {
        OK = 0,          ///< The last call succeeded.
        NOT_BEGUN,       ///< `begin()` has not succeeded yet.
        PIN_ERROR,       ///< Pin number was outside 0–39.
        BANK_ERROR,      ///< Bank number was outside 0–4.
        I2C_ERROR,       ///< The chip NACKed or the bus read came up short.
        MODE_ERROR,      ///< Pin mode is not supported. There are no internal pulls.
        ARG_ERROR,       ///< A pointer was null, a length was zero, or the GPIO cannot interrupt.
        NO_PIN           ///< RESET, OE, or INT has not been given a host GPIO.
    };

    /**
     * Datasheet signal names as pin numbers.
     *
     * `pinMode(PeasyB_PCA9698::IO2_3, OUTPUT)` is the same call as
     * `pinMode(19, OUTPUT)`.
     */
    enum Pin : uint8_t {
        IO0_0 = 0, IO0_1, IO0_2, IO0_3, IO0_4, IO0_5, IO0_6, IO0_7,
        IO1_0,     IO1_1, IO1_2, IO1_3, IO1_4, IO1_5, IO1_6, IO1_7,
        IO2_0,     IO2_1, IO2_2, IO2_3, IO2_4, IO2_5, IO2_6, IO2_7,
        IO3_0,     IO3_1, IO3_2, IO3_3, IO3_4, IO3_5, IO3_6, IO3_7,
        IO4_0,     IO4_1, IO4_2, IO4_3, IO4_4, IO4_5, IO4_6, IO4_7
    };

    /**
     * Three-byte Device ID returned by the reserved Device ID address.
     *
     * Layout from datasheet section 7.5:
     * - 12 manufacturer bits: all of byte 0, then the top 4 bits of byte 1
     * - 9 part bits: the low 4 bits of byte 1, then the top 5 bits of byte 2
     * - 3 revision bits: the low 3 bits of byte 2
     */
    struct DeviceId {
        uint8_t raw[3]; ///< Bytes in the order the chip sends them.

        /** @brief 12-bit manufacturer field. NXP's ID in this scheme is 0. */
        uint16_t manufacturer() const {
            return (static_cast<uint16_t>(raw[0]) << 4) | (raw[1] >> 4);
        }

        /** @brief 9-bit part identification. */
        uint16_t part() const {
            return (static_cast<uint16_t>(raw[1] & 0x0F) << 5) | (raw[2] >> 3);
        }

        /** @brief 3-bit die revision. */
        uint8_t revision() const {
            return raw[2] & 0x07;
        }
    };

    /**
     * @brief Store the address and bus. Does not talk to the chip.
     * @param i2cAddress 7-bit address. Defaults to @c ADDRESS_DEFAULT (0x20).
     * @param wire Bus to use. Defaults to the global `Wire`.
     */
    explicit PeasyB_PCA9698(uint8_t i2cAddress = ADDRESS_DEFAULT, TwoWire *wire = &Wire);

    /**
     * @brief Probe the chip and load the register cache.
     *
     * Start the I²C peripheral before calling this, on the pins your board
     * routes to the bus. A failed probe leaves the object unusable until a
     * later `begin()` succeeds.
     *
     * @return true when the address ACKs and the configuration registers read back.
     */
    bool begin();

    /**
     * @brief True when the configured address ACKs a zero-length write.
     *
     * Safe to call before `begin()`.
     */
    bool isConnected();

    /** @brief 7-bit address this object is using. */
    uint8_t address() const { return _address; }

    /**
     * @brief 7-bit address for an AD2/AD1/AD0 strap.
     *
     * Covers all 64 straps in datasheet Table 12, including pins tied to SCL
     * or SDA. The group where AD0 is on the bus, AD1 is on a rail, and AD2 is
     * on the bus lands at 0x70–0x77 (not at 0x68).
     */
    static constexpr uint8_t addressFor(AddrLevel ad2, AddrLevel ad1, AddrLevel ad0) {
        const uint8_t a2 = static_cast<uint8_t>(ad2);
        const uint8_t a1 = static_cast<uint8_t>(ad1);
        const uint8_t a0 = static_cast<uint8_t>(ad0);
        const bool bus2 = a2 >= 2;
        const bool bus1 = a1 >= 2;
        const bool bus0 = a0 >= 2;
        uint8_t base = 0x20;
        if (bus1 && !bus0) {
            base = 0x10;
        } else if (bus1 && bus0) {
            base = 0x18;
        } else if (!bus1 && bus0) {
            base = 0x28;
        }
        uint8_t result = static_cast<uint8_t>(base | ((a2 & 1) << 2) | ((a1 & 1) << 1) | (a0 & 1));
        if (bus2) {
            result = static_cast<uint8_t>(result + (base == 0x28 ? 0x48 : 0x40));
        }
        return result;
    }

    /**
     * @brief Library pin number for a bank and bit.
     * @param bank 0–4, matching IO0–IO4.
     * @param bit 0–7, matching the bit inside that bank.
     * @return Pin number 0–39. The caller still has to pass a legal pin to the other methods.
     */
    static constexpr uint8_t pinNumber(uint8_t bank, uint8_t bit) {
        return static_cast<uint8_t>((bank << 3) | (bit & 0x07));
    }

    /**
     * @name Arduino-style pin access
     * @{
     */

    /**
     * @brief Set one pin to INPUT or OUTPUT.
     *
     * INPUT sets the IOC bit (1 = input, the power-up state). OUTPUT clears
     * it. The output latch is not changed, so an output comes up at whatever
     * `digitalWrite()` last stored, or LOW if nothing has written it.
     *
     * To avoid a glitch, write the latch while the pin is still an input,
     * then switch it to OUTPUT.
     *
     * INPUT_PULLUP and INPUT_PULLDOWN return false. This chip has no internal
     * pull resistors; fit one on the board.
     *
     * @return false if the pin number is illegal, the mode is unsupported, or I²C fails.
     */
    bool pinMode(uint8_t pin, uint8_t mode);

    /**
     * @brief Read one pin.
     *
     * Reads the Input Port register, which shows the level at the pin for
     * both inputs and outputs. A 1 in the Polarity Inversion register flips
     * the bit this call returns.
     *
     * Reading the register clears a pending interrupt for every unmasked
     * input in that bank whose level has been captured. One `digitalRead()`
     * therefore clears the whole bank, not just the pin you named.
     *
     * @return HIGH, LOW, or -1 if the pin is illegal or the read fails.
     */
    int digitalRead(uint8_t pin);

    /**
     * @brief Set the output latch for one pin.
     *
     * The latch is updated even when the pin is currently an input. The pin
     * itself only drives once IOC says it is an output and OE is asserted.
     * Writing the same level that is already latched does not send the output
     * register again. If `forceBanks()` is still holding the bank, the call
     * does write ALLBNK so the pins follow the latch.
     *
     * @param pin Pin 0–39.
     * @param level HIGH or LOW. Any non-zero value is HIGH.
     * @return false if the pin is illegal or I²C fails.
     */
    bool digitalWrite(uint8_t pin, uint8_t level);

    /**
     * @brief Invert the output latch of one pin and write it back.
     * @return false if the pin is illegal or I²C fails.
     */
    bool digitalToggle(uint8_t pin);

    /** @} */

    /**
     * @name Whole banks
     *
     * `readBank()` returns pin levels. `writeBank()` writes the output latch.
     * Those are different registers: a value you just wrote is not what
     * `readBank()` returns unless the pin is an output, OE is on, and nothing
     * is loading the pin.
     * @{
     */

    /**
     * @brief Read one Input Port register.
     * @param bank 0–4.
     * @return The eight pin levels as a bit mask (bit 0 is IOx_0), or -1 on failure.
     *         Polarity inversion applies. This read clears interrupts for this bank.
     */
    int readBank(uint8_t bank);

    /**
     * @brief Replace one Output Port register.
     * @param bank 0–4.
     * @param value Bit 0 is IOx_0. Bits for pins that are inputs are stored and take effect if that pin later becomes an output.
     */
    bool writeBank(uint8_t bank, uint8_t value);

    /**
     * @brief Read all five Input Port registers in one transaction.
     * @param values Receives bank 0 in values[0] through bank 4 in values[4].
     */
    bool readAll(uint8_t values[BANK_COUNT]);

    /**
     * @brief Write all five Output Port registers in one transaction.
     *
     * Auto-increment is set, so the five bytes land on OP0–OP4 and the
     * reserved slots at 0x0D–0x0F are not written. With output update set to
     * ON_STOP, every bank changes on the STOP that ends this call.
     */
    bool writeAll(const uint8_t values[BANK_COUNT]);

    /**
     * @brief Output latch currently cached for one bank.
     *
     * This is the last value written to the Output Port register, not a live
     * sample of the pin. `refresh()` reloads it from the chip.
     * @return The cached byte, or 0 if @p bank is out of range.
     */
    uint8_t outputLatch(uint8_t bank) const;

    /** @} */

    /**
     * @name Direction, polarity, interrupts
     *
     * In the direction and interrupt-mask bytes, a 1 means the datasheet's
     * "set" state: 1 = input, and 1 = interrupt masked. That matches the
     * IOC and MSK registers, and it is the opposite of "1 means output".
     * @{
     */

    /**
     * @brief Write one I/O Configuration register.
     * @param mask Bit = 1 input, bit = 0 output.
     */
    bool writeDirection(uint8_t bank, uint8_t mask);

    /**
     * @brief Read one I/O Configuration register back from the chip.
     * @param mask Receives the register. Bit = 1 input, bit = 0 output.
     */
    bool readDirection(uint8_t bank, uint8_t &mask);

    /**
     * @brief Invert, or stop inverting, the Input Port bit for one pin.
     *
     * Inversion changes what `digitalRead()` returns. It does not change the
     * level the pin drives as an output.
     */
    bool setInverted(uint8_t pin, bool inverted);

    /** @brief Write one Polarity Inversion register. Bit = 1 inverts that input. */
    bool writePolarity(uint8_t bank, uint8_t mask);

    /** @brief Read one Polarity Inversion register from the chip. */
    bool readPolarity(uint8_t bank, uint8_t &mask);

    /**
     * @brief Unmask or mask the interrupt for one input pin.
     *
     * A level change on an unmasked input asserts INT (open-drain, active
     * low) until the Input Port register for that pin is read, or the pin
     * returns to its previous level. Masked is the power-up state, so
     * interrupts stay quiet until you enable them.
     *
     * The datasheet recommends programming the mask and the direction during
     * initialisation. Changing either later can produce a spurious INT.
     */
    bool setInterrupt(uint8_t pin, bool enabled);

    /**
     * @brief Write one Mask Interrupt register.
     * @param mask Bit = 1 masked (no interrupt), bit = 0 enabled.
     */
    bool writeInterruptMask(uint8_t bank, uint8_t mask);

    /** @brief Read one Mask Interrupt register from the chip. */
    bool readInterruptMask(uint8_t bank, uint8_t &mask);

    /** @} */

    /**
     * @name Output structure
     *
     * OUTCONF is not one bit per pin. On bank 0 the bits cover pairs
     * (IO0_0 with IO0_1, IO0_2 with IO0_3, and so on). On banks 1–4 one bit
     * covers the whole bank. `setOutputDrive()` on any pin in a pair or bank
     * changes the neighbours that share that bit. Totem-pole is the power-up
     * default (OUTCONF = 0xFF).
     * @{
     */

    /**
     * @brief Set the driver used by the pair or bank that contains @p pin.
     * @return false on an illegal pin or an I²C failure.
     */
    bool setOutputDrive(uint8_t pin, OutputDrive drive);

    /** @brief Drive currently configured for the pair or bank containing @p pin. */
    OutputDrive outputDrive(uint8_t pin) const;

    /** @brief Apply one drive to every pin of a bank. Bank 0 sets all four pair bits. */
    bool setBankOutputDrive(uint8_t bank, OutputDrive drive);

    /** @brief Cached OUTCONF byte. Bit = 1 totem-pole, bit = 0 open-drain. */
    uint8_t readOutputConfig() const { return _outconf; }

    /** @brief Write OUTCONF directly. See the datasheet for the bit positions. */
    bool writeOutputConfig(uint8_t value);

    /** @} */

    /**
     * @name MODE register
     *
     * Reserved MODE bits (2, 5, 6, and 7) are written as 0. The power-up
     * value is 0x02: outputs change on ACK, OE is active low, All Call is
     * off, and SMBus Alert is off.
     * @{
     */

    /** @brief Choose whether outputs change on ACK or on STOP. */
    bool setOutputUpdate(OutputUpdate when);

    /** @brief Current output-update selection from the cache. */
    OutputUpdate outputUpdate() const;

    /**
     * @brief Set the active level of the OE pin.
     *
     * false (the default) means OE low enables every output and OE high
     * tri-states them. true swaps that. Program this before relying on the
     * outputs. With the default polarity, a floating or high OE pin makes
     * `digitalWrite()` look like it does nothing.
     */
    bool setOeActiveHigh(bool activeHigh);

    /** @brief True when the cached OEPOL bit makes OE active high. */
    bool oeActiveHigh() const { return (_mode & MODE_OEPOL) != 0; }

    /**
     * @brief Let this chip answer the GPIO All Call address (0x6E).
     *
     * All Call is a write-only broadcast. It is not the I²C general call
     * address. Devices with this bit clear ignore it.
     */
    bool enableAllCall(bool enabled);

    /** @brief True when this chip will answer GPIO All Call. */
    bool allCallEnabled() const { return (_mode & MODE_IOAC) != 0; }

    /**
     * @brief Answer the SMBus Alert Response Address when INT is low.
     *
     * INT is open-drain either way. With this enabled, a master can read
     * address 0x0C and the asserting chip replies with its own address.
     */
    bool enableSmbusAlert(bool enabled);

    /** @brief True when SMBus Alert response is enabled. */
    bool smbusAlertEnabled() const { return (_mode & MODE_SMBA) != 0; }

    /** @brief Cached MODE byte, with reserved bits already cleared. */
    uint8_t readMode() const { return _mode; }

    /**
     * @brief Write MODE. Reserved bits in @p value are forced to 0.
     * @return false on I²C failure.
     */
    bool writeMode(uint8_t value);

    /** @} */

    /**
     * @name All-bank force, All Call, alert, device ID
     * @{
     */

    /**
     * @brief Force every output in the selected banks to the same level.
     *
     * @param bankMask Bit 0 selects bank 0, bit 4 selects bank 4.
     *        Banks whose bit is clear keep the level in their Output Port register.
     * @param level true forces the selected banks high, false forces them low.
     *
     * This writes the ALLBNK register. The Output Port registers themselves
     * do not change, so `outputLatch()` still reports the old latch, while
     * the pins show the forced level. A later `digitalWrite()`, `writeBank()`,
     * or `writeAll()` on a forced bank programs ALLBNK so that bank follows
     * its latch again. Rewriting the output register alone does not.
     */
    bool forceBanks(uint8_t bankMask, bool level);

    /**
     * @brief Write the ALLBNK register as a raw datasheet byte.
     *
     * Prefer `forceBanks()` unless you specifically want the BSEL filter.
     * A later latch write releases any bank this byte is still forcing.
     */
    bool writeAllBank(uint8_t value);

    /** @brief Read ALLBNK from the chip. */
    bool readAllBank(uint8_t &value);

    /**
     * @brief Write-only broadcast to every PCA9698 that has All Call enabled.
     *
     * Sent to 7-bit address 0x6E. A length greater than 1 sets auto-increment
     * and is only meaningful for a 5-bank register group (Output Port,
     * Polarity, IOC, or Mask) starting at that group's base address.
     * OUTCONF, ALLBNK, and MODE are single-byte registers; pass a length of 1.
     *
     * This object's cache is not updated, because other chips on the bus
     * change too and this chip might not even be participating. Call
     * `refresh()` on each object that had All Call enabled.
     */
    bool allCallWrite(uint8_t reg, const uint8_t *data, uint8_t length);

    /**
     * @brief Read the SMBus Alert Response.
     *
     * The chip only ACKs 7-bit address 0x0C when SMBus Alert is enabled and
     * its INT/SMBALERT pin is low. On success, @p sevenBitAddress is the
     * winning device. If several devices are asserting, the lowest address
     * wins and the others stay asserted, so call this again.
     */
    bool alertResponse(uint8_t &sevenBitAddress);

    /**
     * @brief Read the 24-bit Device ID for this chip.
     *
     * Uses the reserved Device ID address (7-bit 0x7C). The transaction is a
     * write of this chip's address followed by a repeated START and three
     * read bytes. A STOP in between cancels the read, so this call never
     * inserts one.
     */
    bool readDeviceId(DeviceId &id);

    /** @} */

    /**
     * @name RESET, OE, and INT on the host
     *
     * These methods drive or read a GPIO on the microcontroller. The pin
     * number is whatever that board uses. The library does not assume a
     * particular MCU. INT needs a pull-up to VDD. RESET needs a pull-up so
     * the chip stays out of reset while the GPIO is still high-Z. OE, with
     * the power-up polarity, needs a pull-down so outputs stay enabled.
     * A 10 kΩ pull is enough: the GPIO can override it.
     * @{
     */

    /// Sentinel for "this host GPIO is not in use".
    static constexpr int16_t NO_HOST_PIN = -1;

    /**
     * @brief Drive the host GPIO wired to RESET, and hold it high.
     *
     * The level is written before the pin becomes an output, so the pin does
     * not blip low on the way out of high-Z. The external pull-up covers the
     * time before this call.
     */
    void useResetPin(uint8_t hostPin);

    /** @brief Host GPIO passed to `useResetPin()`, or @c NO_HOST_PIN. */
    int16_t resetPin() const { return _resetPin; }

    /**
     * @brief Pulse RESET low, then reload the register cache.
     *
     * The datasheet minimum low time is 4 ns. The pulse here is much longer,
     * so a slow GPIO still meets it. Registers return to their power-up
     * values: every I/O is an input, interrupts are masked, outputs are
     * totem-pole, and OE is active-low. Start the I²C bus before calling.
     *
     * @return false if no reset pin was given, or the chip does not answer afterwards.
     */
    bool hardReset();

    /**
     * @brief Drive the host GPIO wired to OE.
     *
     * Outputs are enabled immediately, using the current OE polarity
     * (active-low until `setOeActiveHigh(true)`). Changing that polarity
     * later re-drives this pin so the outputs stay in the same enabled or
     * tri-stated state.
     */
    void useOePin(uint8_t hostPin);

    /** @brief Host GPIO passed to `useOePin()`, or @c NO_HOST_PIN. */
    int16_t oePin() const { return _oePin; }

    /**
     * @brief Enable or tri-state every output, through the OE pin.
     *
     * @param enabled true lets the output latches drive. false tri-states them.
     * @return false if `useOePin()` has not been called.
     */
    bool enableOutputs(bool enabled);

    /**
     * @brief Last state requested with `enableOutputs()`, or true if OE is still untouched.
     *
     * This is the intended state, not a read-back of the pin.
     */
    bool outputsEnabled() const { return _outputsEnabled; }

    /**
     * @brief Read the host GPIO wired to INT.
     *
     * The pin is an input. @p pullUp enables the GPIO's internal pull-up for
     * boards that do not already have one on INT. An external pull-up is the
     * usual choice, and it can sit in parallel with an internal one.
     */
    void useInterruptPin(uint8_t hostPin, bool pullUp = false);

    /** @brief Host GPIO passed to `useInterruptPin()`, or @c NO_HOST_PIN. */
    int16_t interruptPin() const { return _intPin; }

    /**
     * @brief Read INT.
     *
     * INT is open-drain and active-low, so a low pin means an unmasked input
     * changed and its bank has not been read yet.
     *
     * @return 1 when INT is low, 0 when it is high, -1 if no pin was given.
     */
    int interruptAsserted();

    /**
     * @brief Call @p isr on the falling edge of INT.
     *
     * @p isr runs in interrupt context. Keep it short, and do not call into
     * this library or into Wire from there. A volatile flag is the usual
     * pattern. Returns false if the pin cannot wake an interrupt on this core,
     * or if no INT pin was given.
     */
    bool onInterrupt(void (*isr)());

    /** @brief Remove the handler installed by `onInterrupt()`. */
    bool detachInterruptHandler();

    /** @} */

    /**
     * @brief Re-read direction, latches, polarity, masks, OUTCONF, ALLBNK, and MODE.
     *
     * Call this after a GPIO All Call, or after another master writes the
     * same chip. Does not by itself push latches back out to pins that
     * `forceBanks()` moved.
     */
    bool refresh();

    /**
     * @brief Most recent error code.
     *
     * Reading it resets the stored code to OK, so check it once after a
     * failed call.
     */
    Error lastError();

private:
    /// Command-byte bit 7. Set it to walk a 5-bank register group.
    static constexpr uint8_t AUTO_INCREMENT = 0x80;

    /// Input Port registers, banks 0–4. Read only.
    static constexpr uint8_t REG_IP = 0x00;
    /// Output Port registers, banks 0–4.
    static constexpr uint8_t REG_OP = 0x08;
    /// Polarity Inversion registers, banks 0–4.
    static constexpr uint8_t REG_PI = 0x10;
    /// I/O Configuration registers, banks 0–4. 1 = input.
    static constexpr uint8_t REG_IOC = 0x18;
    /// Interrupt mask registers, banks 0–4. 1 = masked.
    static constexpr uint8_t REG_MSK = 0x20;
    /// Output structure. Not one bit per pin; see `setOutputDrive()`.
    static constexpr uint8_t REG_OUTCONF = 0x28;
    /// All-bank output force register.
    static constexpr uint8_t REG_ALLBNK = 0x29;
    /// OE polarity, output-update timing, All Call, SMBus Alert.
    static constexpr uint8_t REG_MODE = 0x2A;

    static constexpr uint8_t MODE_OEPOL = 1 << 0;
    static constexpr uint8_t MODE_OCH = 1 << 1;
    static constexpr uint8_t MODE_IOAC = 1 << 3;
    static constexpr uint8_t MODE_SMBA = 1 << 4;
    /// Bits the datasheet says must be written. Everything else stays 0.
    static constexpr uint8_t MODE_USED = MODE_OEPOL | MODE_OCH | MODE_IOAC | MODE_SMBA;

    /// 7-bit address of the GPIO All Call write.
    static constexpr uint8_t ADDRESS_ALL_CALL = 0x6E;
    /// 7-bit address of the SMBus Alert Response read.
    static constexpr uint8_t ADDRESS_ALERT = 0x0C;
    /// 7-bit form of the reserved Device ID address (datasheet 1111 100).
    static constexpr uint8_t ADDRESS_DEVICE_ID = 0x7C;

    uint8_t _address;
    TwoWire *_wire;
    bool _begun;
    Error _error;

    uint8_t _output[BANK_COUNT];
    uint8_t _direction[BANK_COUNT];
    uint8_t _polarity[BANK_COUNT];
    uint8_t _mask[BANK_COUNT];
    uint8_t _outconf;
    uint8_t _mode;
    /// Last ALLBNK value. Power-up is 0x80: BSEL set, every bank on its latch.
    uint8_t _allbnk;

    /// Host GPIOs. @c NO_HOST_PIN until the matching use*Pin() call.
    int16_t _resetPin;
    int16_t _oePin;
    int16_t _intPin;
    /// Logical OE state. true means outputs are allowed to drive.
    bool _outputsEnabled;

    bool ready();
    void applyOe();
    bool validPin(uint8_t pin);
    bool validBank(uint8_t bank);

    bool writeBytes(uint8_t i2cAddress, uint8_t reg, const uint8_t *data, uint8_t length, bool autoIncrement);
    bool readBytes(uint8_t i2cAddress, uint8_t reg, uint8_t *data, uint8_t length, bool autoIncrement);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readRegister(uint8_t reg, uint8_t &value);
    bool readGroup(uint8_t base, uint8_t values[BANK_COUNT]);
    bool writeGroup(uint8_t base, const uint8_t values[BANK_COUNT]);

    bool updateCachedBit(uint8_t *cache, uint8_t baseReg, uint8_t pin, bool setBit);
    static bool bankFollowsLatch(uint8_t allbnk, uint8_t bank);
    bool releaseBanks(uint8_t bankMask);
    static uint8_t driveBit(uint8_t pin);
};

static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::GND,
                                         PeasyB_PCA9698::AddrLevel::GND,
                                         PeasyB_PCA9698::AddrLevel::GND) == 0x20,
              "AD pins at GND must be 7-bit 0x20");
static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::VDD,
                                         PeasyB_PCA9698::AddrLevel::VDD,
                                         PeasyB_PCA9698::AddrLevel::VDD) == 0x27,
              "AD pins at VDD must be 7-bit 0x27");
static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::VSS,
                                         PeasyB_PCA9698::AddrLevel::SCL,
                                         PeasyB_PCA9698::AddrLevel::VSS) == 0x10,
              "AD2=VSS AD1=SCL AD0=VSS must be 0x10");
static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::SCL,
                                         PeasyB_PCA9698::AddrLevel::SCL,
                                         PeasyB_PCA9698::AddrLevel::VDD) == 0x51,
              "AD2=SCL AD1=SCL AD0=VDD must be 0x51");
static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::SCL,
                                         PeasyB_PCA9698::AddrLevel::VSS,
                                         PeasyB_PCA9698::AddrLevel::VSS) == 0x60,
              "AD2=SCL AD1=VSS AD0=VSS must be 0x60");
static_assert(PeasyB_PCA9698::addressFor(PeasyB_PCA9698::AddrLevel::SDA,
                                         PeasyB_PCA9698::AddrLevel::VDD,
                                         PeasyB_PCA9698::AddrLevel::SDA) == 0x77,
              "AD2=SDA AD1=VDD AD0=SDA must be 0x77");
static_assert(PeasyB_PCA9698::pinNumber(2, 3) == PeasyB_PCA9698::IO2_3,
              "pinNumber(bank, bit) must match the Pin enumerators");
static_assert(PeasyB_PCA9698::IO4_7 == 39, "the last pin is IO4_7");

#endif

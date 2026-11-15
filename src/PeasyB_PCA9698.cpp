/**
 * @file PeasyB_PCA9698.cpp
 * @brief Implementation of the PCA9698 Arduino driver.
 *
 * I²C transactions always send an explicit command byte. Bit 7 of that byte
 * is the auto-increment flag. For a 5-bank group (Input, Output, Polarity,
 * IOC, Mask) the flag walks the low 3 bits of the register address and skips
 * the three reserved addresses that follow each group. OUTCONF, ALLBNK, and
 * MODE are single registers: a second data byte would overwrite the same
 * register, so they are only ever accessed one byte at a time.
 *
 * The output latch is cached on purpose. The Input Port register shows pin
 * levels, not the latch, so a read-modify-write through the input register
 * would clobber the other outputs in the bank.
 *
 * @author PeasyB
 * @license MIT
 */

#include "PeasyB_PCA9698.h"

PeasyB_PCA9698::PeasyB_PCA9698(uint8_t i2cAddress, TwoWire *wire)
    : _address(i2cAddress),
      _wire(wire),
      _begun(false),
      _error(OK),
      _outconf(0xFF),
      _mode(MODE_OCH),
      _allbnk(0x80),
      _resetPin(NO_HOST_PIN),
      _oePin(NO_HOST_PIN),
      _intPin(NO_HOST_PIN),
      _outputsEnabled(true) {
    // Power-up image, replaced by refresh() inside begin().
    for (uint8_t bank = 0; bank < BANK_COUNT; ++bank) {
        _output[bank] = 0x00;
        _direction[bank] = 0xFF;
        _polarity[bank] = 0x00;
        _mask[bank] = 0xFF;
    }
}

bool PeasyB_PCA9698::begin() {
    _begun = false;
    if (_wire == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    if (!isConnected()) {
        return false;
    }
    if (!refresh()) {
        return false;
    }
    _begun = true;
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::isConnected() {
    if (_wire == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    _wire->beginTransmission(_address);
    if (_wire->endTransmission() != 0) {
        _error = I2C_ERROR;
        return false;
    }
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::pinMode(uint8_t pin, uint8_t mode) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    if (mode == INPUT_PULLUP) {
        _error = MODE_ERROR;
        return false;
    }
#ifdef INPUT_PULLDOWN
    if (mode == INPUT_PULLDOWN) {
        _error = MODE_ERROR;
        return false;
    }
#endif
    if (mode != INPUT && mode != OUTPUT) {
        _error = MODE_ERROR;
        return false;
    }
    // IOC bit = 1 is an input. OUTPUT is the only mode that clears the bit.
    return updateCachedBit(_direction, REG_IOC, pin, mode != OUTPUT);
}

int PeasyB_PCA9698::digitalRead(uint8_t pin) {
    if (!ready() || !validPin(pin)) {
        return -1;
    }
    int bankValue = readBank(pin >> 3);
    if (bankValue < 0) {
        return -1;
    }
    return (bankValue & (1 << (pin & 0x07))) ? HIGH : LOW;
}

bool PeasyB_PCA9698::digitalWrite(uint8_t pin, uint8_t level) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    const uint8_t bank = pin >> 3;
    const uint8_t bit = static_cast<uint8_t>(1u << (pin & 0x07));
    uint8_t updated = _output[bank];
    if (level == LOW) {
        updated = static_cast<uint8_t>(updated & ~bit);
    } else {
        updated = static_cast<uint8_t>(updated | bit);
    }
    if (updated != _output[bank]) {
        if (!writeRegister(static_cast<uint8_t>(REG_OP + bank), updated)) {
            return false;
        }
        _output[bank] = updated;
    }
    // ALLBNK keeps owning the pins until it is told to follow the latch.
    // A repeated write of the same output byte does not do that.
    return releaseBanks(static_cast<uint8_t>(1u << bank));
}

bool PeasyB_PCA9698::digitalToggle(uint8_t pin) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    const uint8_t bank = pin >> 3;
    const uint8_t current = (_output[bank] >> (pin & 0x07)) & 0x01;
    return digitalWrite(pin, current ? LOW : HIGH);
}

int PeasyB_PCA9698::readBank(uint8_t bank) {
    if (!ready() || !validBank(bank)) {
        return -1;
    }
    uint8_t value = 0;
    if (!readRegister(static_cast<uint8_t>(REG_IP + bank), value)) {
        return -1;
    }
    return value;
}

bool PeasyB_PCA9698::writeBank(uint8_t bank, uint8_t value) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!writeRegister(static_cast<uint8_t>(REG_OP + bank), value)) {
        return false;
    }
    _output[bank] = value;
    return releaseBanks(static_cast<uint8_t>(1u << bank));
}

bool PeasyB_PCA9698::readAll(uint8_t values[BANK_COUNT]) {
    if (!ready()) {
        return false;
    }
    if (values == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    return readGroup(REG_IP, values);
}

bool PeasyB_PCA9698::writeAll(const uint8_t values[BANK_COUNT]) {
    if (!ready()) {
        return false;
    }
    if (values == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    if (!writeGroup(REG_OP, values)) {
        return false;
    }
    for (uint8_t bank = 0; bank < BANK_COUNT; ++bank) {
        _output[bank] = values[bank];
    }
    return releaseBanks(0x1F);
}

uint8_t PeasyB_PCA9698::outputLatch(uint8_t bank) const {
    if (bank >= BANK_COUNT) {
        return 0;
    }
    return _output[bank];
}

bool PeasyB_PCA9698::writeDirection(uint8_t bank, uint8_t mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!writeRegister(static_cast<uint8_t>(REG_IOC + bank), mask)) {
        return false;
    }
    _direction[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::readDirection(uint8_t bank, uint8_t &mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!readRegister(static_cast<uint8_t>(REG_IOC + bank), mask)) {
        return false;
    }
    _direction[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::setInverted(uint8_t pin, bool inverted) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    return updateCachedBit(_polarity, REG_PI, pin, inverted);
}

bool PeasyB_PCA9698::writePolarity(uint8_t bank, uint8_t mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!writeRegister(static_cast<uint8_t>(REG_PI + bank), mask)) {
        return false;
    }
    _polarity[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::readPolarity(uint8_t bank, uint8_t &mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!readRegister(static_cast<uint8_t>(REG_PI + bank), mask)) {
        return false;
    }
    _polarity[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::setInterrupt(uint8_t pin, bool enabled) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    // MSK bit 0 enables the interrupt. The cache stores the register image,
    // so "enabled" clears the bit.
    return updateCachedBit(_mask, REG_MSK, pin, !enabled);
}

bool PeasyB_PCA9698::writeInterruptMask(uint8_t bank, uint8_t mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!writeRegister(static_cast<uint8_t>(REG_MSK + bank), mask)) {
        return false;
    }
    _mask[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::readInterruptMask(uint8_t bank, uint8_t &mask) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    if (!readRegister(static_cast<uint8_t>(REG_MSK + bank), mask)) {
        return false;
    }
    _mask[bank] = mask;
    return true;
}

bool PeasyB_PCA9698::setOutputDrive(uint8_t pin, OutputDrive drive) {
    if (!ready() || !validPin(pin)) {
        return false;
    }
    const uint8_t bit = driveBit(pin);
    uint8_t updated = _outconf;
    if (drive == OutputDrive::TOTEM_POLE) {
        updated = static_cast<uint8_t>(updated | bit);
    } else {
        updated = static_cast<uint8_t>(updated & ~bit);
    }
    return writeOutputConfig(updated);
}

PeasyB_PCA9698::OutputDrive PeasyB_PCA9698::outputDrive(uint8_t pin) const {
    if (pin >= PIN_COUNT) {
        return OutputDrive::TOTEM_POLE;
    }
    return (_outconf & driveBit(pin)) ? OutputDrive::TOTEM_POLE : OutputDrive::OPEN_DRAIN;
}

bool PeasyB_PCA9698::setBankOutputDrive(uint8_t bank, OutputDrive drive) {
    if (!ready() || !validBank(bank)) {
        return false;
    }
    // Bank 0 owns OUTCONF bits 0–3 (four pairs). Each later bank owns one bit.
    const uint8_t bits = (bank == 0) ? 0x0F : static_cast<uint8_t>(1u << (bank + 3));
    uint8_t updated = _outconf;
    if (drive == OutputDrive::TOTEM_POLE) {
        updated = static_cast<uint8_t>(updated | bits);
    } else {
        updated = static_cast<uint8_t>(updated & ~bits);
    }
    return writeOutputConfig(updated);
}

bool PeasyB_PCA9698::writeOutputConfig(uint8_t value) {
    if (!ready()) {
        return false;
    }
    if (!writeRegister(REG_OUTCONF, value)) {
        return false;
    }
    _outconf = value;
    return true;
}

bool PeasyB_PCA9698::setOutputUpdate(OutputUpdate when) {
    if (!ready()) {
        return false;
    }
    uint8_t mode = static_cast<uint8_t>(_mode & MODE_USED);
    if (when == OutputUpdate::ON_ACK) {
        mode = static_cast<uint8_t>(mode | MODE_OCH);
    } else {
        mode = static_cast<uint8_t>(mode & ~MODE_OCH);
    }
    return writeMode(mode);
}

PeasyB_PCA9698::OutputUpdate PeasyB_PCA9698::outputUpdate() const {
    return (_mode & MODE_OCH) ? OutputUpdate::ON_ACK : OutputUpdate::ON_STOP;
}

bool PeasyB_PCA9698::setOeActiveHigh(bool activeHigh) {
    if (!ready()) {
        return false;
    }
    uint8_t mode = static_cast<uint8_t>(_mode & MODE_USED);
    if (activeHigh) {
        mode = static_cast<uint8_t>(mode | MODE_OEPOL);
    } else {
        mode = static_cast<uint8_t>(mode & ~MODE_OEPOL);
    }
    return writeMode(mode);
}

bool PeasyB_PCA9698::enableAllCall(bool enabled) {
    if (!ready()) {
        return false;
    }
    uint8_t mode = static_cast<uint8_t>(_mode & MODE_USED);
    if (enabled) {
        mode = static_cast<uint8_t>(mode | MODE_IOAC);
    } else {
        mode = static_cast<uint8_t>(mode & ~MODE_IOAC);
    }
    return writeMode(mode);
}

bool PeasyB_PCA9698::enableSmbusAlert(bool enabled) {
    if (!ready()) {
        return false;
    }
    uint8_t mode = static_cast<uint8_t>(_mode & MODE_USED);
    if (enabled) {
        mode = static_cast<uint8_t>(mode | MODE_SMBA);
    } else {
        mode = static_cast<uint8_t>(mode & ~MODE_SMBA);
    }
    return writeMode(mode);
}

bool PeasyB_PCA9698::writeMode(uint8_t value) {
    if (!ready()) {
        return false;
    }
    // Bits 2, 5, 6, and 7 are reserved and must be written as 0.
    value = static_cast<uint8_t>(value & MODE_USED);
    if (!writeRegister(REG_MODE, value)) {
        return false;
    }
    _mode = value;
    // A polarity change would otherwise invert the pin under our feet.
    applyOe();
    return true;
}

void PeasyB_PCA9698::useResetPin(uint8_t hostPin) {
    _resetPin = hostPin;
    // Write the idle level first. pinMode(OUTPUT) on many cores drives the
    // pin low for a moment if the latch is still 0, and that would reset the chip.
    // The leading :: selects the host GPIO. The bare names are this class's
    // expander methods, and those take a PCA9698 pin number, not a host pin.
    ::digitalWrite(hostPin, HIGH);
    ::pinMode(hostPin, OUTPUT);
}

bool PeasyB_PCA9698::hardReset() {
    if (_wire == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    if (_resetPin < 0) {
        _error = NO_PIN;
        return false;
    }
    // tw(rst) is 4 ns minimum. Hold it long enough that a slow GPIO still
    // qualifies, then give the outputs their trst (100 ns minimum) before
    // talking on the bus again. trec(rst) itself is 0.
    ::digitalWrite(static_cast<uint8_t>(_resetPin), LOW);
    delayMicroseconds(10);
    ::digitalWrite(static_cast<uint8_t>(_resetPin), HIGH);
    delayMicroseconds(10);
    if (!refresh()) {
        return false;
    }
    _begun = true;
    _outputsEnabled = true;
    applyOe();
    _error = OK;
    return true;
}

void PeasyB_PCA9698::useOePin(uint8_t hostPin) {
    _oePin = hostPin;
    _outputsEnabled = true;
    // Latch the idle level before the pin starts driving, same reason as RESET.
    const bool activeHigh = (_mode & MODE_OEPOL) != 0;
    ::digitalWrite(hostPin, activeHigh ? HIGH : LOW);
    ::pinMode(hostPin, OUTPUT);
}

bool PeasyB_PCA9698::enableOutputs(bool enabled) {
    if (_oePin < 0) {
        _error = NO_PIN;
        return false;
    }
    _outputsEnabled = enabled;
    applyOe();
    _error = OK;
    return true;
}

void PeasyB_PCA9698::applyOe() {
    if (_oePin < 0) {
        return;
    }
    const bool activeHigh = (_mode & MODE_OEPOL) != 0;
    const bool level = _outputsEnabled ? activeHigh : !activeHigh;
    ::digitalWrite(static_cast<uint8_t>(_oePin), level ? HIGH : LOW);
}

void PeasyB_PCA9698::useInterruptPin(uint8_t hostPin, bool pullUp) {
    _intPin = hostPin;
    ::pinMode(hostPin, pullUp ? INPUT_PULLUP : INPUT);
}

int PeasyB_PCA9698::interruptAsserted() {
    if (_intPin < 0) {
        _error = NO_PIN;
        return -1;
    }
    _error = OK;
    return ::digitalRead(static_cast<uint8_t>(_intPin)) == LOW ? 1 : 0;
}

bool PeasyB_PCA9698::onInterrupt(void (*isr)()) {
    if (_intPin < 0) {
        _error = NO_PIN;
        return false;
    }
    if (isr == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    const int irq = digitalPinToInterrupt(static_cast<uint8_t>(_intPin));
    if (irq == NOT_AN_INTERRUPT) {
        _error = ARG_ERROR;
        return false;
    }
    ::attachInterrupt(irq, isr, FALLING);
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::detachInterruptHandler() {
    if (_intPin < 0) {
        _error = NO_PIN;
        return false;
    }
    const int irq = digitalPinToInterrupt(static_cast<uint8_t>(_intPin));
    if (irq == NOT_AN_INTERRUPT) {
        _error = ARG_ERROR;
        return false;
    }
    ::detachInterrupt(irq);
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::forceBanks(uint8_t bankMask, bool level) {
    if (!ready()) {
        return false;
    }
    if ((bankMask & ~0x1F) != 0) {
        _error = BANK_ERROR;
        return false;
    }
    // BSEL = 0, Bx = 0 forces a bank low and Bx = 1 leaves it on the latch.
    // BSEL = 1, Bx = 1 forces a bank high and Bx = 0 leaves it on the latch.
    const uint8_t value = level
        ? static_cast<uint8_t>(0x80 | bankMask)
        : static_cast<uint8_t>((~bankMask) & 0x1F);
    if (!writeRegister(REG_ALLBNK, value)) {
        return false;
    }
    _allbnk = value;
    return true;
}

bool PeasyB_PCA9698::writeAllBank(uint8_t value) {
    if (!ready()) {
        return false;
    }
    if (!writeRegister(REG_ALLBNK, value)) {
        return false;
    }
    _allbnk = value;
    return true;
}

bool PeasyB_PCA9698::readAllBank(uint8_t &value) {
    if (!ready()) {
        return false;
    }
    return readRegister(REG_ALLBNK, value);
}

bool PeasyB_PCA9698::allCallWrite(uint8_t reg, const uint8_t *data, uint8_t length) {
    if (!ready()) {
        return false;
    }
    // Auto-increment only does something useful for a multi-byte 5-bank group.
    // Single-byte registers ignore the flag and would overwrite themselves.
    const bool autoIncrement = length > 1;
    return writeBytes(ADDRESS_ALL_CALL, reg, data, length, autoIncrement);
}

bool PeasyB_PCA9698::alertResponse(uint8_t &sevenBitAddress) {
    if (!ready()) {
        return false;
    }
    // The alert response is a read from a fixed address, with no command byte.
    const uint8_t got = _wire->requestFrom(static_cast<uint8_t>(ADDRESS_ALERT), static_cast<uint8_t>(1));
    if (got != 1) {
        _error = I2C_ERROR;
        return false;
    }
    const uint8_t reported = static_cast<uint8_t>(_wire->read());
    // The chip sends its slave address with the R/W bit cleared.
    sevenBitAddress = reported >> 1;
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::readDeviceId(DeviceId &id) {
    if (!ready()) {
        return false;
    }
    // Datasheet section 7.5. The byte after the Device ID address is this
    // chip's slave address in the usual I²C position (7 bits, then a
    // don't-care R/W bit). A STOP before the read cancels the transaction,
    // so endTransmission(false) holds the bus for the repeated START.
    _wire->beginTransmission(ADDRESS_DEVICE_ID);
    _wire->write(static_cast<uint8_t>(_address << 1));
    if (_wire->endTransmission(false) != 0) {
        _error = I2C_ERROR;
        return false;
    }
    const uint8_t got = _wire->requestFrom(static_cast<uint8_t>(ADDRESS_DEVICE_ID), static_cast<uint8_t>(3));
    if (got != 3) {
        _error = I2C_ERROR;
        return false;
    }
    id.raw[0] = static_cast<uint8_t>(_wire->read());
    id.raw[1] = static_cast<uint8_t>(_wire->read());
    id.raw[2] = static_cast<uint8_t>(_wire->read());
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::refresh() {
    if (_wire == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    if (!readGroup(REG_OP, _output) ||
        !readGroup(REG_PI, _polarity) ||
        !readGroup(REG_IOC, _direction) ||
        !readGroup(REG_MSK, _mask) ||
        !readRegister(REG_OUTCONF, _outconf) ||
        !readRegister(REG_ALLBNK, _allbnk) ||
        !readRegister(REG_MODE, _mode)) {
        return false;
    }
    _mode = static_cast<uint8_t>(_mode & MODE_USED);
    _error = OK;
    return true;
}

PeasyB_PCA9698::Error PeasyB_PCA9698::lastError() {
    const Error error = _error;
    _error = OK;
    return error;
}

bool PeasyB_PCA9698::ready() {
    if (_wire == nullptr) {
        _error = ARG_ERROR;
        return false;
    }
    if (!_begun) {
        _error = NOT_BEGUN;
        return false;
    }
    return true;
}

bool PeasyB_PCA9698::validPin(uint8_t pin) {
    if (pin >= PIN_COUNT) {
        _error = PIN_ERROR;
        return false;
    }
    return true;
}

bool PeasyB_PCA9698::validBank(uint8_t bank) {
    if (bank >= BANK_COUNT) {
        _error = BANK_ERROR;
        return false;
    }
    return true;
}

bool PeasyB_PCA9698::writeBytes(uint8_t i2cAddress, uint8_t reg, const uint8_t *data,
                               uint8_t length, bool autoIncrement) {
    if (_wire == nullptr || data == nullptr || length == 0) {
        _error = ARG_ERROR;
        return false;
    }
    uint8_t command = static_cast<uint8_t>(reg & 0x7F);
    if (autoIncrement) {
        command = static_cast<uint8_t>(command | AUTO_INCREMENT);
    }
    _wire->beginTransmission(i2cAddress);
    _wire->write(command);
    for (uint8_t i = 0; i < length; ++i) {
        _wire->write(data[i]);
    }
    // STOP matters. With OCH = 0 the outputs update on STOP, and the chip
    // will not answer its address again until that STOP arrives.
    if (_wire->endTransmission() != 0) {
        _error = I2C_ERROR;
        return false;
    }
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::readBytes(uint8_t i2cAddress, uint8_t reg, uint8_t *data,
                              uint8_t length, bool autoIncrement) {
    if (_wire == nullptr || data == nullptr || length == 0) {
        _error = ARG_ERROR;
        return false;
    }
    uint8_t command = static_cast<uint8_t>(reg & 0x7F);
    if (autoIncrement) {
        command = static_cast<uint8_t>(command | AUTO_INCREMENT);
    }
    _wire->beginTransmission(i2cAddress);
    _wire->write(command);
    if (_wire->endTransmission(false) != 0) {
        _error = I2C_ERROR;
        return false;
    }
    const uint8_t got = _wire->requestFrom(i2cAddress, length);
    if (got != length) {
        _error = I2C_ERROR;
        return false;
    }
    for (uint8_t i = 0; i < length; ++i) {
        data[i] = static_cast<uint8_t>(_wire->read());
    }
    _error = OK;
    return true;
}

bool PeasyB_PCA9698::writeRegister(uint8_t reg, uint8_t value) {
    return writeBytes(_address, reg, &value, 1, false);
}

bool PeasyB_PCA9698::readRegister(uint8_t reg, uint8_t &value) {
    return readBytes(_address, reg, &value, 1, false);
}

bool PeasyB_PCA9698::readGroup(uint8_t base, uint8_t values[BANK_COUNT]) {
    return readBytes(_address, base, values, BANK_COUNT, true);
}

bool PeasyB_PCA9698::writeGroup(uint8_t base, const uint8_t values[BANK_COUNT]) {
    return writeBytes(_address, base, values, BANK_COUNT, true);
}

bool PeasyB_PCA9698::updateCachedBit(uint8_t *cache, uint8_t baseReg, uint8_t pin, bool setBit) {
    const uint8_t bank = pin >> 3;
    const uint8_t bit = static_cast<uint8_t>(1u << (pin & 0x07));
    uint8_t updated = cache[bank];
    if (setBit) {
        updated = static_cast<uint8_t>(updated | bit);
    } else {
        updated = static_cast<uint8_t>(updated & ~bit);
    }
    if (!writeRegister(static_cast<uint8_t>(baseReg + bank), updated)) {
        return false;
    }
    cache[bank] = updated;
    return true;
}

bool PeasyB_PCA9698::bankFollowsLatch(uint8_t allbnk, uint8_t bank) {
    const uint8_t bit = static_cast<uint8_t>(1u << bank);
    // BSEL = 0 forces a bank only when its bit is 0.
    // BSEL = 1 forces a bank only when its bit is 1.
    if ((allbnk & 0x80) != 0) {
        return (allbnk & bit) == 0;
    }
    return (allbnk & bit) != 0;
}

bool PeasyB_PCA9698::releaseBanks(uint8_t bankMask) {
    uint8_t updated = _allbnk;
    for (uint8_t bank = 0; bank < BANK_COUNT; ++bank) {
        if ((bankMask & (1u << bank)) == 0 || bankFollowsLatch(updated, bank)) {
            continue;
        }
        const uint8_t bit = static_cast<uint8_t>(1u << bank);
        if ((updated & 0x80) != 0) {
            updated = static_cast<uint8_t>(updated & ~bit);
        } else {
            updated = static_cast<uint8_t>(updated | bit);
        }
    }
    if (updated == _allbnk) {
        _error = OK;
        return true;
    }
    if (!writeRegister(REG_ALLBNK, updated)) {
        return false;
    }
    _allbnk = updated;
    return true;
}

uint8_t PeasyB_PCA9698::driveBit(uint8_t pin) {
    const uint8_t bank = pin >> 3;
    const uint8_t bit = pin & 0x07;
    if (bank == 0) {
        // OUT001, OUT023, OUT045, OUT067 sit in bits 0–3.
        return static_cast<uint8_t>(1u << (bit >> 1));
    }
    // OUT1..OUT4 sit in bits 4–7, one bank each.
    return static_cast<uint8_t>(1u << (bank + 3));
}

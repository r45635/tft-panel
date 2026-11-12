#include "pca9557.h"

#if defined(BOARD_CROWPANEL7)

bool Pca9557::present() {
    wire_.beginTransmission(addr_);
    return wire_.endTransmission() == 0;
}

bool Pca9557::writeReg(uint8_t reg, uint8_t value) {
    wire_.beginTransmission(addr_);
    wire_.write(reg);
    wire_.write(value);
    return wire_.endTransmission() == 0;
}

bool Pca9557::readReg(uint8_t reg, uint8_t &value) {
    wire_.beginTransmission(addr_);
    wire_.write(reg);
    if (wire_.endTransmission(false) != 0) return false;
    if (wire_.requestFrom(addr_, (uint8_t)1) != 1) return false;
    value = wire_.read();
    return true;
}

bool Pca9557::setOutputs(uint8_t latch) {
    out_ = latch;
    return writeReg(REG_OUTPUT, out_);
}

bool Pca9557::setDirections(uint8_t inputMask) {
    cfg_ = inputMask;
    return writeReg(REG_CONFIG, cfg_);
}

bool Pca9557::setPin(uint8_t pin, bool high) {
    out_ = high ? (out_ | (1u << pin)) : (out_ & ~(1u << pin));
    return writeReg(REG_OUTPUT, out_);
}

bool Pca9557::setPinInput(uint8_t pin, bool input) {
    cfg_ = input ? (cfg_ | (1u << pin)) : (cfg_ & ~(1u << pin));
    return writeReg(REG_CONFIG, cfg_);
}

#endif  // BOARD_CROWPANEL7

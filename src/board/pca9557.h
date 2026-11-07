// Minimal driver for the PCA9557 I2C GPIO expander of the CrowPanel 7" V3.0.
#pragma once

#include <Wire.h>

class Pca9557 {
public:
    explicit Pca9557(TwoWire &wire, uint8_t addr) : wire_(wire), addr_(addr) {}

    bool present();
    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t &value);

    // Output latch / direction kept in shadow registers (bit = 1 → high / input).
    bool setOutputs(uint8_t latch);
    bool setDirections(uint8_t inputMask);
    bool setPin(uint8_t pin, bool high);
    bool setPinInput(uint8_t pin, bool input);

    static constexpr uint8_t REG_INPUT = 0x00;
    static constexpr uint8_t REG_OUTPUT = 0x01;
    static constexpr uint8_t REG_POLARITY = 0x02;
    static constexpr uint8_t REG_CONFIG = 0x03;

private:
    TwoWire &wire_;
    uint8_t addr_;
    uint8_t out_ = 0xFF;
    uint8_t cfg_ = 0xFF;
};

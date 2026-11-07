// Minimal polling driver for the GT911 capacitive touch controller.
// Reset/INT are not on MCU pins on this board: they go through the PCA9557
// (see Board::begin), so this driver only talks I2C.
#pragma once

#include <Wire.h>

struct TouchPoint {
    uint16_t x;
    uint16_t y;
};

class Gt911 {
public:
    explicit Gt911(TwoWire &wire) : wire_(wire) {}

    // Probes 0x5D then 0x14; returns false if neither answers.
    bool begin(uint8_t preferredAddr, uint8_t altAddr);
    uint8_t address() const { return addr_; }

    // Product id string ("911"), firmware version and configured resolution.
    bool readInfo(char productId[5], uint16_t &fw, uint16_t &xRes, uint16_t &yRes);

    // Returns the number of touches (0..5) and fills the first point.
    // Returns -1 on an I2C error.
    int read(TouchPoint &first);

private:
    bool readRegs(uint16_t reg, uint8_t *buf, size_t len);
    bool writeReg(uint16_t reg, uint8_t value);

    TwoWire &wire_;
    uint8_t addr_ = 0;
    TouchPoint last_ = {0, 0};
    int lastCount_ = 0;
};

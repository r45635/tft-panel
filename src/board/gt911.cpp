#include "gt911.h"

namespace {
constexpr uint16_t REG_PRODUCT_ID = 0x8140;   // 4 ASCII bytes + fw (2) + x res (2) + y res (2)
constexpr uint16_t REG_STATUS = 0x814E;       // bit7 = buffer ready, low nibble = touch count
constexpr uint16_t REG_POINT1 = 0x8150;       // x lo, x hi, y lo, y hi, size lo, size hi
}  // namespace

bool Gt911::begin(uint8_t preferredAddr, uint8_t altAddr) {
    for (uint8_t a : {preferredAddr, altAddr}) {
        wire_.beginTransmission(a);
        if (wire_.endTransmission() == 0) {
            addr_ = a;
            return true;
        }
    }
    return false;
}

bool Gt911::readRegs(uint16_t reg, uint8_t *buf, size_t len) {
    wire_.beginTransmission(addr_);
    wire_.write(reg >> 8);
    wire_.write(reg & 0xFF);
    if (wire_.endTransmission(false) != 0) return false;
    if (wire_.requestFrom(addr_, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = wire_.read();
    return true;
}

bool Gt911::writeReg(uint16_t reg, uint8_t value) {
    wire_.beginTransmission(addr_);
    wire_.write(reg >> 8);
    wire_.write(reg & 0xFF);
    wire_.write(value);
    return wire_.endTransmission() == 0;
}

bool Gt911::readInfo(char productId[5], uint16_t &fw, uint16_t &xRes, uint16_t &yRes) {
    uint8_t b[10];
    if (!readRegs(REG_PRODUCT_ID, b, sizeof b)) return false;
    for (int i = 0; i < 4; i++) productId[i] = (char)b[i];
    productId[4] = '\0';
    fw = b[4] | (b[5] << 8);
    xRes = b[6] | (b[7] << 8);
    yRes = b[8] | (b[9] << 8);
    return true;
}

int Gt911::read(TouchPoint &first) {
    uint8_t status;
    if (!readRegs(REG_STATUS, &status, 1)) return -1;
    if (status & 0x80) {
        // Fresh frame. Without one, keep the last state: polling faster than the
        // GT911 scan rate must not turn a held finger into a release.
        int count = status & 0x0F;
        if (count > 5) count = 0;
        if (count > 0) {
            uint8_t p[4];
            if (!readRegs(REG_POINT1, p, sizeof p)) return -1;
            last_.x = p[0] | (p[1] << 8);
            last_.y = p[2] | (p[3] << 8);
        }
        lastCount_ = count;
        writeReg(REG_STATUS, 0);   // release the buffer for the next frame
    }
    first = last_;
    return lastCount_;
}

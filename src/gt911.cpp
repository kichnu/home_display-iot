#include "gt911.h"

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "board_pins.h"
#include "ch422g.h"

namespace gt911 {

namespace {
constexpr uint16_t REG_CONFIG = 0x8047;
constexpr uint16_t REG_PRODUCT_ID = 0x8140;
constexpr uint16_t REG_STATUS = 0x814E;
constexpr uint16_t REG_POINT1 = 0x814F;  // track id, x lo/hi, y lo/hi, size lo/hi, reserved
constexpr int POINT_STRIDE = 8;

uint8_t s_addr = 0;

bool readReg(uint16_t reg, uint8_t* buf, size_t len) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(s_addr, (uint8_t)len) != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

bool writeReg8(uint16_t reg, uint8_t value) {
    Wire.beginTransmission(s_addr);
    Wire.write(reg >> 8);
    Wire.write(reg & 0xFF);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// INT held LOW while RST rises selects address 0x5D.
void hardwareReset() {
    pinMode(PIN_TP_INT, OUTPUT);
    digitalWrite(PIN_TP_INT, LOW);
    ch422g::setPin(EXIO_TP_RST, false);
    delay(20);
    ch422g::setPin(EXIO_TP_RST, true);
    delay(10);
    pinMode(PIN_TP_INT, INPUT);
    delay(100);  // firmware boot
}
}  // namespace

bool begin(Info* info) {
    hardwareReset();

    if (probe(GT911_ADDR_PRIMARY)) {
        s_addr = GT911_ADDR_PRIMARY;
    } else if (probe(GT911_ADDR_ALT)) {
        s_addr = GT911_ADDR_ALT;
    } else {
        return false;
    }

    uint8_t id[6] = {0};
    if (!readReg(REG_PRODUCT_ID, id, sizeof(id))) return false;

    uint8_t cfg[5] = {0};
    if (!readReg(REG_CONFIG, cfg, sizeof(cfg))) return false;

    if (info) {
        info->addr = s_addr;
        memcpy(info->productId, id, 4);
        info->productId[4] = '\0';
        info->firmware = id[4] | (id[5] << 8);
        info->cfgVersion = cfg[0];
        info->cfgX = cfg[1] | (cfg[2] << 8);
        info->cfgY = cfg[3] | (cfg[4] << 8);
    }

    writeReg8(REG_STATUS, 0);
    return true;
}

int read(Point* pts, bool* ready) {
    *ready = false;
    uint8_t status;
    if (!readReg(REG_STATUS, &status, 1)) return -1;
    if (!(status & 0x80)) return 0;  // buffer not ready
    *ready = true;

    int n = status & 0x0F;
    if (n > MAX_POINTS) n = 0;  // invalid frame, drop it

    if (n > 0) {
        uint8_t raw[MAX_POINTS * POINT_STRIDE];
        if (!readReg(REG_POINT1, raw, n * POINT_STRIDE)) return -1;
        for (int i = 0; i < n; i++) {
            const uint8_t* p = raw + i * POINT_STRIDE;
            pts[i].id = p[0];
            pts[i].x = p[1] | (p[2] << 8);
            pts[i].y = p[3] | (p[4] << 8);
            pts[i].size = p[5] | (p[6] << 8);
        }
    }

    writeReg8(REG_STATUS, 0);
    return n;
}

}  // namespace gt911

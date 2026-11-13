#include "ch422g.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

namespace ch422g {

namespace {
constexpr uint8_t CMD_SYSTEM = 0x24;
constexpr uint8_t CMD_IO_OUT = 0x38;
constexpr uint8_t CMD_IO_IN = 0x26;
constexpr uint8_t SYS_IO_OE = 0x01;

// Default: TP_RST low (touch held in reset), backlight OFF until the first frame is drawn (the
// CH422G keeps its outputs across an ESP32 reset, and an un-driven RGB panel shows garbage),
// LCD out of reset, SD_CS high.
// DI0/DI1 (EXIO0/EXIO5) are driven LOW: they are optocoupler collectors with a 4.7k pull-up, so
// LOW only sinks the pull-up current, while HIGH could fight an active optocoupler.
uint8_t s_state = (1 << EXIO_LCD_RST) | (1 << EXIO_SD_CS);

bool sendByte(uint8_t cmd, uint8_t value) {
    Wire.beginTransmission(cmd);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}
}  // namespace

bool begin() {
    if (!sendByte(CMD_SYSTEM, SYS_IO_OE)) return false;
    return sendByte(CMD_IO_OUT, s_state);
}

bool writeAll(uint8_t value) {
    s_state = value;
    return sendByte(CMD_IO_OUT, s_state);
}

bool setPin(uint8_t bit, bool high) {
    uint8_t v = high ? (s_state | (1 << bit)) : (s_state & ~(1 << bit));
    return writeAll(v);
}

uint8_t state() { return s_state; }

bool readInputs(uint8_t* value, uint32_t* inputUs) {
    uint32_t t0 = micros();
    bool ok = sendByte(CMD_SYSTEM, 0x00);  // IO0..7 as inputs
    if (ok) ok = Wire.requestFrom(CMD_IO_IN, (uint8_t)1) == 1;
    if (ok) *value = Wire.read();
    // Always restore outputs, even after a failed read.
    bool restored = sendByte(CMD_SYSTEM, SYS_IO_OE) && sendByte(CMD_IO_OUT, s_state);
    if (inputUs) *inputUs = micros() - t0;
    return ok && restored;
}

}  // namespace ch422g

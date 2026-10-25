#pragma once
// Minimal CH422G driver. The chip has no regular register map: each "I2C address"
// is a command (0x24 = system config, 0x38 = IO0..7 output, 0x26 = IO input, 0x23 = OC outputs).

#include <stdint.h>

namespace ch422g {

bool begin();                        // enable IO0..7 as push-pull outputs, write current state
bool setPin(uint8_t bit, bool high);  // update one EXIO line
bool writeAll(uint8_t value);
uint8_t state();

// Reads IO0..7. The IO direction is shared by all eight lines, so this briefly switches every
// IO to input (DISP pulled high by the board -> backlight on, resets floating), reads, and
// restores outputs. Returns the time spent with IO as inputs in *inputUs.
bool readInputs(uint8_t* value, uint32_t* inputUs);

}  // namespace ch422g

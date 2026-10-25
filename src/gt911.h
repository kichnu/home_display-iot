#pragma once
// Minimal GT911 capacitive touch driver (polling, up to 5 points).

#include <stdint.h>

namespace gt911 {

constexpr int MAX_POINTS = 5;

struct Point {
    uint8_t id;
    uint16_t x;
    uint16_t y;
    uint16_t size;
};

struct Info {
    uint8_t addr;
    char productId[5];
    uint16_t firmware;
    uint16_t cfgX;  // resolution stored in the controller config
    uint16_t cfgY;
    uint8_t cfgVersion;
};

// Resets the controller (via CH422G TP_RST + INT strap) and probes 0x5D / 0x14.
bool begin(Info* info);

// Returns number of points (0..5), or -1 on I2C error. *ready is false when the controller
// has no new frame yet (pts untouched); a ready frame with 0 points means release.
int read(Point* pts, bool* ready);

}  // namespace gt911

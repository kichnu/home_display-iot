#pragma once
// Colours and sizes of the panel GUI (dark theme, 800x480, wet-finger touch targets).

#include <lvgl.h>
#include <stdint.h>

namespace gui {

constexpr int32_t SCREEN_W = 800;
constexpr int32_t SCREEN_H = 480;
constexpr int32_t TOP_H = 56;      // top bar
constexpr int32_t STRIP_H = 44;    // alarm strip (only while alarms are active)
constexpr int32_t PAD = 10;
constexpr int32_t TOUCH_H = 80;    // minimum touch target (decisions doc)
constexpr int32_t ROW_H = 76;      // list rows

enum ColorId : uint32_t {
    C_BG = 0x101418,
    C_SURFACE = 0x1C232B,
    C_SURFACE2 = 0x26303A,
    C_TEXT = 0xE8EDF2,
    C_MUTED = 0x8A97A6,
    C_ACCENT = 0x3D8BFD,
    C_OK = 0x2EBD6B,
    C_P3 = 0xE5C02A,
    C_P2 = 0xF08A24,
    C_P1 = 0xE5484D,
    C_OFFLINE = 0x6B7480,
};

inline lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }

}  // namespace gui

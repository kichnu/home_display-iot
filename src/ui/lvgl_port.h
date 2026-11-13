#pragma once
// LVGL 9 port: display (DIRECT mode on the two panel framebuffers), GT911 touch input,
// tick, log and backlight. Fonts and theme: gui/fonts.h (shared with the PC simulator).
// All lv_* calls must come from the task that calls update().

#include <lvgl.h>

namespace ui {


bool begin();     // after lcd::begin(), ch422g::begin(), gt911::begin()
void update();    // from loop(): runs lv_timer_handler()

// Backlight on/off (CH422G DISP). While off, the first touch only wakes the screen and is not
// passed to LVGL (no "blind" button presses).
void setBacklight(bool on);
bool backlight();

// Backlight off, then esp_restart(). The CH422G keeps the backlight on across an ESP32 reset
// while the RGB signals stop, which shows ~1 s of garbage.
[[noreturn]] void restart();

}  // namespace ui

#pragma once
// LVGL 9 port: display (DIRECT mode on the two panel framebuffers), GT911 touch input,
// tick, log, fonts and backlight. All lv_* calls must come from the task that calls update().

#include <lvgl.h>

namespace ui {

struct Fonts {
    const lv_font_t* body;    // 22 px regular
    const lv_font_t* small;   // 18 px regular
    const lv_font_t* title;   // 28 px bold
    const lv_font_t* value;   // 44 px bold, process values
};

bool begin();     // after lcd::begin(), ch422g::begin(), gt911::begin()
void update();    // from loop(): runs lv_timer_handler()

const Fonts& fonts();

// Backlight on/off (CH422G DISP). While off, the first touch only wakes the screen and is not
// passed to LVGL (no "blind" button presses).
void setBacklight(bool on);
bool backlight();

}  // namespace ui

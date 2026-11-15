#pragma once
// Fonts and theme shared by the panel and the PC simulator (only LVGL, no Arduino).

#include <lvgl.h>

namespace gui {

struct Fonts {
    const lv_font_t* body;   // 22 px regular
    const lv_font_t* small;  // 18 px regular
    const lv_font_t* title;  // 28 px bold
    const lv_font_t* value;  // 44 px bold, process values / clock
    const lv_font_t* huge;   // 96 px bold, the main value of a node screen (thermostat temperature)
};

// Noto Sans TTF (embedded with .incbin) rendered by TinyTTF; LV_SYMBOL_* icons from the built-in
// Montserrat fonts as fallback. Call after lv_init().
void initFonts();
const Fonts& fonts();

// Dark default theme with the TTF body font, also on the top/system layers (modals, toasts).
void applyTheme(lv_display_t* disp);

}  // namespace gui

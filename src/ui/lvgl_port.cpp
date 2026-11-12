#include "lvgl_port.h"

#include <Arduino.h>

#include "board_pins.h"
#include "ch422g.h"
#include "gt911.h"
#include "lcd.h"

extern "C" {
extern const uint8_t font_noto_regular_ttf[];
extern const uint8_t font_noto_regular_ttf_end[];
extern const uint8_t font_noto_bold_ttf[];
extern const uint8_t font_noto_bold_ttf_end[];
}

namespace ui {

namespace {
// No new GT911 frame for this long while pressed = finger lifted (fallback; the controller
// normally sends an explicit release frame).
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 150;
constexpr size_t TTF_GLYPH_CACHE = 256;

lv_display_t* s_disp = nullptr;
lv_indev_t* s_indev = nullptr;
Fonts s_fonts = {};

bool s_backlight = true;
bool s_swallowTouch = false;  // touch that woke the screen, ignored until release
bool s_pressed = false;
int16_t s_x = 0, s_y = 0;
uint32_t s_lastFrameMs = 0;
uint32_t s_touchErrors = 0;

uint32_t tickCb() { return millis(); }

void logCb(lv_log_level_t, const char* buf) { Serial.print(buf); }

void flushCb(lv_display_t* disp, const lv_area_t*, uint8_t* pxMap) {
    // DIRECT mode: pxMap is the whole framebuffer LVGL has just finished; show it on the last area.
    if (lv_display_flush_is_last(disp)) lcd::swap(reinterpret_cast<const uint16_t*>(pxMap));
    lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t*, lv_indev_data_t* data) {
    gt911::Point pts[gt911::MAX_POINTS];
    bool ready = false;
    int n = gt911::read(pts, &ready);
    uint32_t now = millis();
    if (n < 0) {
        s_touchErrors++;
    } else if (ready) {
        s_lastFrameMs = now;
        s_pressed = n > 0;
        if (s_pressed) {
            s_x = pts[0].x < LCD_W ? pts[0].x : LCD_W - 1;
            s_y = pts[0].y < LCD_H ? pts[0].y : LCD_H - 1;
        }
    } else if (s_pressed && now - s_lastFrameMs > TOUCH_RELEASE_TIMEOUT_MS) {
        s_pressed = false;
    }

    if (!s_backlight && s_pressed) {
        setBacklight(true);
        s_swallowTouch = true;
    }
    if (s_swallowTouch) {
        if (!s_pressed) s_swallowTouch = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->point.x = s_x;
    data->point.y = s_y;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// TinyTTF logs "cache not allocated" (LV_LOG_ERROR) for every glyph missing in the TTF. LV_SYMBOL_*
// icons (Private Use Area) are never in Noto, so they are skipped here and resolved silently by
// the Montserrat fallback.
using GlyphDscCb = bool (*)(const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t);
GlyphDscCb s_ttfGlyphDsc = nullptr;

bool ttfGlyphDscNoSymbols(const lv_font_t* font, lv_font_glyph_dsc_t* out, uint32_t letter, uint32_t next) {
    if (letter >= 0xE000 && letter <= 0xF8FF) return false;
    return s_ttfGlyphDsc(font, out, letter, next);
}

const lv_font_t* makeFont(const uint8_t* start, const uint8_t* end, int px, const lv_font_t* fallback) {
    lv_font_t* f = lv_tiny_ttf_create_data_ex(start, end - start, px, LV_FONT_KERNING_NORMAL, TTF_GLYPH_CACHE);
    if (!f) {
        Serial.printf("TinyTTF: font %d px failed, using built-in\n", px);
        return fallback;
    }
    s_ttfGlyphDsc = f->get_glyph_dsc;  // same TinyTTF callback for every font
    f->get_glyph_dsc = ttfGlyphDscNoSymbols;
    f->fallback = fallback;  // LV_SYMBOL_* icons live only in the built-in Montserrat fonts
    return f;
}
}  // namespace

bool begin() {
    lv_init();
    lv_tick_set_cb(tickCb);
    lv_log_register_print_cb(logCb);

    s_disp = lv_display_create(LCD_W, LCD_H);
    if (!s_disp) return false;
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, lcd::framebuffer(0), lcd::framebuffer(1),
                           LCD_W * LCD_H * sizeof(uint16_t), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_disp, flushCb);
    lv_sysmon_hide_performance(s_disp);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touchReadCb);
    lv_indev_set_display(s_indev, s_disp);

    s_fonts.body = makeFont(font_noto_regular_ttf, font_noto_regular_ttf_end, 22, &lv_font_montserrat_20);
    s_fonts.small = makeFont(font_noto_regular_ttf, font_noto_regular_ttf_end, 18, &lv_font_montserrat_14);
    s_fonts.title = makeFont(font_noto_bold_ttf, font_noto_bold_ttf_end, 28, &lv_font_montserrat_28);
    s_fonts.value = makeFont(font_noto_bold_ttf, font_noto_bold_ttf_end, 44, &lv_font_montserrat_40);

    lv_theme_t* th = lv_theme_default_init(s_disp, lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_TEAL), true, s_fonts.body);
    lv_display_set_theme(s_disp, th);
    // Top/system layers exist before the theme is set, so their children (modals created with a
    // nullptr parent) would inherit LV_FONT_DEFAULT (Montserrat, no Polish glyphs).
    lv_obj_set_style_text_font(lv_layer_top(), s_fonts.body, 0);
    lv_obj_set_style_text_font(lv_layer_sys(), s_fonts.body, 0);
    return true;
}

void update() { lv_timer_handler(); }

const Fonts& fonts() { return s_fonts; }

void setBacklight(bool on) {
    s_backlight = on;
    ch422g::setPin(EXIO_LCD_BL, on);
}

bool backlight() { return s_backlight; }

}  // namespace ui

#include "lvgl_port.h"

#include <Arduino.h>

#include "board_pins.h"
#include "ch422g.h"
#include "gt911.h"
#include "lcd.h"
#include "../gui/fonts.h"

namespace ui {

namespace {
// No new GT911 frame for this long while pressed = finger lifted (fallback; the controller
// normally sends an explicit release frame).
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 150;

lv_display_t* s_disp = nullptr;
lv_indev_t* s_indev = nullptr;

bool s_backlight = false;  // switched on by main after the first frame
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

    gui::initFonts();
    gui::applyTheme(s_disp);
    return true;
}

void update() { lv_timer_handler(); }

void setBacklight(bool on) {
    s_backlight = on;
    ch422g::setPin(EXIO_LCD_BL, on);
}

bool backlight() { return s_backlight; }

void restart() {
    setBacklight(false);
    delay(20);  // let the current frame finish with the backlight already off
    esp_restart();
}

}  // namespace ui

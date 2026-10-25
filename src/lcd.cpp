#include "lcd.h"

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <stdlib.h>
#include <string.h>

#include "board_pins.h"
#include "font5x7.h"

namespace lcd {

namespace {
esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb = nullptr;

// Current drawing target (framebuffer or an off-screen buffer).
uint16_t* s_dst = nullptr;
int s_dstW = LCD_W;
int s_dstH = LCD_H;

// Bounce buffers in internal SRAM: the driver copies PSRAM -> SRAM with the CPU,
// so plain cached writes to the framebuffer are visible without manual cache flushes.
constexpr int BOUNCE_LINES = 10;
}  // namespace

bool begin() {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src = LCD_CLK_SRC_DEFAULT;
    cfg.timings.pclk_hz = LCD_PCLK_HZ;
    cfg.timings.h_res = LCD_W;
    cfg.timings.v_res = LCD_H;
    cfg.timings.hsync_pulse_width = LCD_HSYNC_PW;
    cfg.timings.hsync_back_porch = LCD_HSYNC_BP;
    cfg.timings.hsync_front_porch = LCD_HSYNC_FP;
    cfg.timings.vsync_pulse_width = LCD_VSYNC_PW;
    cfg.timings.vsync_back_porch = LCD_VSYNC_BP;
    cfg.timings.vsync_front_porch = LCD_VSYNC_FP;
    cfg.timings.flags.pclk_active_neg = LCD_PCLK_ACTIVE_NEG;
    cfg.data_width = 16;
    cfg.bits_per_pixel = 16;
    cfg.num_fbs = 1;
    cfg.bounce_buffer_size_px = LCD_W * BOUNCE_LINES;
    cfg.hsync_gpio_num = PIN_LCD_HSYNC;
    cfg.vsync_gpio_num = PIN_LCD_VSYNC;
    cfg.de_gpio_num = PIN_LCD_DE;
    cfg.pclk_gpio_num = PIN_LCD_PCLK;
    cfg.disp_gpio_num = -1;
    for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = PIN_LCD_DATA[i];
    cfg.flags.fb_in_psram = 1;

    esp_err_t err = esp_lcd_new_rgb_panel(&cfg, &s_panel);
    if (err != ESP_OK) {
        log_e("esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    void* fb = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, &fb);
    if (err != ESP_OK || !fb) {
        log_e("get_frame_buffer failed: %s", esp_err_to_name(err));
        return false;
    }
    s_fb = static_cast<uint16_t*>(fb);
    s_dst = s_fb;
    fill(BLACK);
    return true;
}

uint16_t* framebuffer() { return s_fb; }

void setTarget(uint16_t* buf, int w, int h) {
    if (buf) {
        s_dst = buf;
        s_dstW = w;
        s_dstH = h;
    } else {
        s_dst = s_fb;
        s_dstW = LCD_W;
        s_dstH = LCD_H;
    }
}

void blit(int x, int y, int w, int h, const uint16_t* buf) {
    for (int row = 0; row < h; row++) {
        int ty = y + row;
        if (ty < 0 || ty >= LCD_H) continue;
        memcpy(s_fb + ty * LCD_W + x, buf + row * w, w * sizeof(uint16_t));
    }
}

void fill(uint16_t color) { fillRect(0, 0, s_dstW, s_dstH, color); }

void fillRect(int x, int y, int w, int h, uint16_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_dstW) w = s_dstW - x;
    if (y + h > s_dstH) h = s_dstH - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; row++) {
        uint16_t* p = s_dst + row * s_dstW + x;
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

void pixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= s_dstW || y >= s_dstH) return;
    s_dst[y * s_dstW + x] = color;
}

void hline(int x, int y, int w, uint16_t color) { fillRect(x, y, w, 1, color); }
void vline(int x, int y, int h, uint16_t color) { fillRect(x, y, 1, h, color); }

void rect(int x, int y, int w, int h, uint16_t color) {
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

void line(int x0, int y0, int x1, int y1, int thickness, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = thickness / 2;
    while (true) {
        fillRect(x0 - half, y0 - half, thickness, thickness, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void circle(int cx, int cy, int r, uint16_t color) {
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        hline(cx - dx, cy + dy, 2 * dx + 1, color);
    }
}

void text(int x, int y, const char* s, uint16_t fg, uint16_t bg, int scale) {
    for (; *s; s++, x += 6 * scale) {
        char c = *s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t* glyph = FONT5X7[c - 0x20];
        if (bg != fg) fillRect(x, y, 6 * scale, 8 * scale, bg);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) fillRect(x + col * scale, y + row * scale, scale, scale, fg);
            }
        }
    }
}

int textWidth(const char* s, int scale) { return strlen(s) * 6 * scale; }

}  // namespace lcd

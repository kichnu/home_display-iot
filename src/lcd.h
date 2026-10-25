#pragma once
// RGB panel driver (esp_lcd, single PSRAM framebuffer + bounce buffers) and basic drawing.

#include <stdint.h>

namespace lcd {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t BLUE = 0x001F;
constexpr uint16_t YELLOW = 0xFFE0;
constexpr uint16_t CYAN = 0x07FF;
constexpr uint16_t MAGENTA = 0xF81F;
constexpr uint16_t GRAY = 0x8410;
constexpr uint16_t DARKGRAY = 0x2104;

bool begin();
uint16_t* framebuffer();

// Redirect all drawing to an off-screen buffer (w x h); nullptr restores the framebuffer.
void setTarget(uint16_t* buf, int w, int h);
// Copy an off-screen buffer row by row into the framebuffer at (x, y).
void blit(int x, int y, int w, int h, const uint16_t* buf);

void fill(uint16_t color);
void fillRect(int x, int y, int w, int h, uint16_t color);
void pixel(int x, int y, uint16_t color);
void hline(int x, int y, int w, uint16_t color);
void vline(int x, int y, int h, uint16_t color);
void rect(int x, int y, int w, int h, uint16_t color);
void line(int x0, int y0, int x1, int y1, int thickness, uint16_t color);
void circle(int cx, int cy, int r, uint16_t color);  // filled

// 5x7 font, cell 6x8 px times scale. bg == fg means transparent background.
void text(int x, int y, const char* s, uint16_t fg, uint16_t bg, int scale = 2);
int textWidth(const char* s, int scale = 2);

}  // namespace lcd

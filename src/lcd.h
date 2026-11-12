#pragma once
// RGB panel driver: esp_lcd with two PSRAM framebuffers and bounce buffers in SRAM.
// The UI renders into the hidden framebuffer, then swap() shows it from the next frame.

#include <stdint.h>

namespace lcd {

bool begin();

// The two framebuffers (LCD_W * LCD_H RGB565 each). Valid after begin().
uint16_t* framebuffer(int index);

// Show `fb` (one of the two framebuffers) and block until the panel no longer reads the
// previous one (end of the frame in progress). After return the previous buffer may be drawn into.
void swap(const uint16_t* fb);

}  // namespace lcd

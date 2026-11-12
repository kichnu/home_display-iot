#include "lcd.h"

#include <Arduino.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/semphr.h>
#include <string.h>

#include "board_pins.h"

namespace lcd {

namespace {
esp_lcd_panel_handle_t s_panel = nullptr;
uint16_t* s_fb[2] = {nullptr, nullptr};
SemaphoreHandle_t s_frameDone = nullptr;

// Bounce buffers in internal SRAM: the driver copies PSRAM -> SRAM with the CPU, so cached writes
// to the framebuffers are visible without manual cache flushes, and a flash write (cache off)
// does not starve the LCD DMA.
constexpr int BOUNCE_LINES = 10;

// With bounce buffers the driver picks the framebuffer at the start of each frame, so the end of
// the frame in progress is the moment the old buffer is released.
bool IRAM_ATTR onFrameDone(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void*) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_frameDone, &woken);
    return woken == pdTRUE;
}
}  // namespace

bool begin() {
    s_frameDone = xSemaphoreCreateBinary();

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
    cfg.num_fbs = 2;
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

    esp_lcd_rgb_panel_event_callbacks_t cbs = {};
    cbs.on_frame_buf_complete = onFrameDone;
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &cbs, nullptr);

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    void* fb0 = nullptr;
    void* fb1 = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1);
    if (err != ESP_OK || !fb0 || !fb1) {
        log_e("get_frame_buffer failed: %s", esp_err_to_name(err));
        return false;
    }
    s_fb[0] = static_cast<uint16_t*>(fb0);
    s_fb[1] = static_cast<uint16_t*>(fb1);
    memset(s_fb[0], 0, LCD_W * LCD_H * sizeof(uint16_t));
    memset(s_fb[1], 0, LCD_W * LCD_H * sizeof(uint16_t));
    return true;
}

uint16_t* framebuffer(int index) { return s_fb[index & 1]; }

void swap(const uint16_t* fb) {
    // Passing one of the driver's framebuffers selects it; no copy is made.
    xSemaphoreTake(s_frameDone, 0);
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_W, LCD_H, fb);
    xSemaphoreTake(s_frameDone, pdMS_TO_TICKS(100));
}

}  // namespace lcd

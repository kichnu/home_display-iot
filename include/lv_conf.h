// LVGL 9.2 configuration for the touch panel (ESP32-S3, 800x480 RGB565).
// Only the options that differ from LVGL defaults are listed; the rest comes from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Heap from the C library: large blocks go to PSRAM (Arduino malloc), no fixed LVGL pool.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

// The panel refreshes at ~39 Hz (16 MHz PCLK, 820 x 500 total), flush waits for the frame end.
#define LV_DEF_REFR_PERIOD 20

// Single UI task: every lv_* call comes from loop().
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 0

// Built-in fonts: only as a fallback for LV_SYMBOL_* icons (TinyTTF Noto has no symbol glyphs).
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

// Runtime TTF rendering: Polish glyphs (incl. U+00D3/U+00F3 "Ó ó") without a font converter.
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256

#define LV_USE_OBSERVER 1

// FPS / CPU overlay, toggled from the UI.
#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1

#endif  // LV_CONF_H

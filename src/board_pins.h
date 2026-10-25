#pragma once
// Pin map and panel parameters for Waveshare ESP32-S3-Touch-LCD-4.3B.
// Source: Waveshare demo package (ESP32-S3-Touch-LCD-4.3B-BOX-Demo), 800x480 ST7262 variant.

#include <stdint.h>

// ---- LCD (RGB565, 16-bit parallel) ----
constexpr int LCD_W = 800;
constexpr int LCD_H = 480;

constexpr uint32_t LCD_PCLK_HZ = 16 * 1000 * 1000;
constexpr int LCD_HSYNC_PW = 4;
constexpr int LCD_HSYNC_BP = 8;
constexpr int LCD_HSYNC_FP = 8;
constexpr int LCD_VSYNC_PW = 4;
constexpr int LCD_VSYNC_BP = 8;
constexpr int LCD_VSYNC_FP = 8;
constexpr bool LCD_PCLK_ACTIVE_NEG = true;

constexpr int PIN_LCD_VSYNC = 3;
constexpr int PIN_LCD_HSYNC = 46;
constexpr int PIN_LCD_DE = 5;
constexpr int PIN_LCD_PCLK = 7;

// Data bus D0..D15 = B3..B7, G2..G7, R3..R7 (RGB565 order: B is the low bits).
constexpr int PIN_LCD_DATA[16] = {
    14, 38, 18, 17, 10,     // B3 B4 B5 B6 B7
    39, 0, 45, 48, 47, 21,  // G2 G3 G4 G5 G6 G7
    1, 2, 42, 41, 40,       // R3 R4 R5 R6 R7
};

// ---- I2C bus (GT911, CH422G, PCF85063 RTC) ----
constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 9;
constexpr uint32_t I2C_FREQ_HZ = 400000;

// ---- Touch GT911 ----
constexpr int PIN_TP_INT = 4;  // also selects the I2C address during reset (LOW -> 0x5D)
constexpr uint8_t GT911_ADDR_PRIMARY = 0x5D;
constexpr uint8_t GT911_ADDR_ALT = 0x14;

// ---- CH422G IO expander (EXIO bit numbers) ----
constexpr uint8_t EXIO_TP_RST = 1;
constexpr uint8_t EXIO_LCD_BL = 2;
constexpr uint8_t EXIO_LCD_RST = 3;
constexpr uint8_t EXIO_SD_CS = 4;
constexpr uint8_t EXIO_DI1 = 5;  // DI1 input on the 4.3B (USB_SEL only on the 4.3 non-B); EXIO0 = DI0

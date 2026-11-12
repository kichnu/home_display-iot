# home_display-iot — aquarium touch panel

Central touch panel for the aquarium nodes, on the
[Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B)
(800x480 RGB565 ST7262, GT911 touch, CH422G IO expander). Design notes (Polish):
`Panel dotykowy ESP-NOW — ustalenia i stack.md`, rollout plan: `docs/PLAN_WDROZENIA.md`.

Current state: stage 1 — LVGL foundation with a demo screen, no radio yet.

## Build / flash

```sh
pio run -t upload
pio device monitor
```

Requires the pioarduino platform (Arduino core 3.x / ESP-IDF 5.x) — set in `platformio.ini`.
Serial goes over native USB (USB-CDC on boot). If the port does not show up, hold BOOT and press RESET.

## Stack

- Display: ESP-IDF `esp_lcd` RGB panel, two framebuffers in PSRAM + bounce buffers in SRAM.
  LVGL renders in `DIRECT` mode into the hidden framebuffer; the flush swaps buffers and waits
  for the end of the frame in progress (`src/lcd.cpp`, `src/ui/lvgl_port.cpp`).
- GUI: LVGL 9.2.2 (`lib_deps`), configuration in `include/lv_conf.h`.
- Touch: own GT911 driver (`src/gt911.cpp`) as an LVGL pointer input.
- Fonts: Noto Sans Regular/Bold TTF embedded in flash (`src/ui/fonts.S`, `assets/fonts/`, OFL)
  and rendered at runtime by LVGL TinyTTF — full Polish character set without a font converter.
  LVGL symbol icons come from the built-in Montserrat fonts set as fallback.
- Backlight is on/off only (CH422G EXIO2). With the backlight off the first touch only wakes the
  screen and is not passed to the UI.

## Demo screen (stage 1)

- "Lampy (demo)" card: Auto / Noc / Test mode buttons and "Program…" with a confirmation modal
  (mock data, logged on Serial).
- "Test fontów" card: Polish glyphs at three sizes, spinner animation (tearing / smoothness check).
- Bottom bar: FPS / CPU overlay switch, "Wygaś ekran" (backlight off, touch to wake).

Serial commands: `i` memory info, `b` toggle backlight.

The original bring-up test (patterns, touch paint, DI read test) is in git history (`27efbea`) and
in `~/Dokumenty/My_apps/TOOLS/test_ESP32-S3-Touch-LCD-4.3B/`.

## Hardware notes

| Function | Pins |
|---|---|
| RGB data D0..D15 (B3-B7, G2-G7, R3-R7) | 14 38 18 17 10, 39 0 45 48 47 21, 1 2 42 41 40 |
| HSYNC / VSYNC / DE / PCLK | 46 / 3 / 5 / 7 |
| I2C SDA / SCL | 8 / 9 |
| Touch INT | 4 (LOW during reset selects GT911 address 0x5D) |
| CH422G EXIO0 / 1 / 2 / 3 / 4 / 5 | DI0 / TP_RST / backlight (DISP) / LCD_RST / SD_CS / DI1 |

Panel timing (800x480): PCLK 16 MHz, HSYNC pw/bp/fp 4/8/8, VSYNC 4/8/8, PCLK active on falling edge.

CH422G is not register-based: I2C address 0x24 = system config (0x01 enables IO outputs),
0x38 = IO0..7 output byte. Because of that an I2C scan reports several "devices" in the
0x20-0x3F range.
# home_display-iot

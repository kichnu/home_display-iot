# ESP32-S3-Touch-LCD-4.3B — display & touch bring-up test

PlatformIO firmware for a first hardware check of the
[Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B)
(800x480 RGB565 ST7262 panel, GT911 capacitive touch, CH422G IO expander).

No external libraries: the panel is driven with ESP-IDF `esp_lcd` (RGB, PSRAM framebuffer +
bounce buffers); GT911 and CH422G are accessed directly over `Wire`.

## Build / flash

```sh
pio run -t upload
pio device monitor
```

Requires the pioarduino platform (Arduino core 3.x / ESP-IDF 5.x) — set in `platformio.ini`.
Serial goes over native USB (USB-CDC on boot). If the port does not show up, hold BOOT and press RESET.

## What it does

1. Serial: chip/PSRAM info, I2C scan, CH422G and GT911 detection (address, product ID, firmware,
   configured resolution).
2. Test patterns (tap, Serial `n`, or 5 s timeout to advance):
   solid red/green/blue/white/black, color bars, R/G/B/gray gradients, data-line bit walk
   (one RGB565 bit per stripe — a black stripe means a dead line), geometry (1 px border,
   50 px grid, circles, 1 px checkerboard), info screen.
3. Touch test: multi-touch paint (up to 5 points, colour per track ID), live coordinates in the
   header, corner/centre targets to verify axis mapping, `CLEAR` and `PATTERNS` buttons.

Serial commands: `n` next pattern, `p` patterns, `t` touch screen, `b` toggle backlight, `s` I2C scan,
`d` / `f` DI0/DI1 read test every 200 / 20 ms with the backlight off (any key or a touch ends it).
CH422G has one IO direction for all lines, so each DI read briefly releases DISP (pulled up on the
board) - the test shows whether that flashes the backlight.

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

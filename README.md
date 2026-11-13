# home_display-iot — aquarium touch panel

Central touch panel for the aquarium nodes, on the
[Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B)
(800x480 RGB565 ST7262, GT911 touch, CH422G IO expander). Design notes (Polish):
`Panel dotykowy ESP-NOW — ustalenia i stack.md`, rollout plan: `docs/PLAN_WDROZENIA.md`.

Current state: stage 2 — LVGL demo screen + Wi-Fi STA, NTP time and ArduinoOTA. No ESP-NOW yet.

## Build / flash

```sh
pio run -t upload                      # USB
pio run -e esp32s3-lcd43b_ota -t upload  # Wi-Fi (ArduinoOTA, 192.168.10.50)
pio device monitor
```

Secrets come only from `~/.secrets/iot.env` (sourced from `~/.zshrc`), never from the repo.
Pre-scripts write them to generated headers in `.pio/build/<env>/generated/`:

| Variable | Script | Use |
|---|---|---|
| `IOT_WIFI_FALLBACK_SSID`, `IOT_WIFI_FALLBACK_PASSWORD` | `scripts/wifi_secret.py` | Wi-Fi STA (same network and variables as the lamps) |
| `OTA_PASSWORD_192_168_10_50` | `scripts/ota_secret.py` | ArduinoOTA password, `--auth` for espota |

A plain build without them only warns; an upload without them is aborted. The panel IP is a DHCP
reservation on the router (MAC printed on Serial at boot). OTA from the PC needs the router rule
for the espota return connection (`--host_port=8266`). The first firmware with OTA goes over USB.

Requires the pioarduino platform (Arduino core 3.x / ESP-IDF 5.x) — set in `platformio.ini`.
`custom_sdkconfig` (octal PSRAM, code/rodata executed from PSRAM) triggers a hybrid compile of the
framework libs: a few minutes on all cores, only after changing `custom_sdkconfig` /
`custom_component_remove`. Run one build at a time (VS Code + terminal in parallel corrupt `.pio`);
`nice -n 19 pio run -j 6` keeps the PC usable. Without XIP from PSRAM any flash write (NVS on Wi-Fi
init, OTA) panics in the RGB bounce-buffer ISR.
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
- Network: Wi-Fi STA with `setSleep(false)` (modem sleep drops ESP-NOW frames), auto-reconnect,
  NTP with the same servers and time zone as the lamps, ArduinoOTA (`src/net/network.cpp`).
- Backlight is on/off only (CH422G EXIO2). The CH422G keeps its outputs across an ESP32 reset, so the
  backlight is switched off before every restart (OTA, `r`) and stays off at boot until the first
  frame is drawn — otherwise the un-driven panel shows ~1 s of garbage. With the backlight off the first touch only wakes the
  screen and is not passed to the UI.

## Demo screen

- "Lampy (demo)" card: Auto / Noc / Test mode buttons and "Program…" with a confirmation modal
  (mock data, logged on Serial).
- "Test fontów" card: Polish glyphs at three sizes, spinner animation (tearing / smoothness check).
- Bottom bar: FPS / CPU overlay switch, "Wygaś ekran" (backlight off, touch to wake).

- Header: Wi-Fi state (IP, RSSI) and NTP clock (Europe/Warsaw), uptime until the clock is set.
- OTA upload shows a progress modal on the screen.

Serial commands: `i` memory info, `b` toggle backlight, `w` network / time info, `r` restart.

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

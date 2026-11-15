# home_display-iot — aquarium touch panel

Central touch panel for the aquarium nodes, on the
[Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B)
(800x480 RGB565 ST7262, GT911 touch, CH422G IO expander). Design notes (Polish):
`Panel dotykowy ESP-NOW — ustalenia i stack.md`, rollout plan: `docs/PLAN_WDROZENIA.md`.

Current state: stage 6 — Wi-Fi STA, NTP, ArduinoOTA, ESP-NOW with the lamps as a controller
(`FT_CTRL_HB`, lamp FW >= 0.3.0 shows "panel: Panel akwarium") and the panel GUI over live lamp data:
lamp group, program catalog read from a lamp, alarms, group control (Auto / Noc / Test, program change).
Top-off node (stages D3–D5, `docs/PLAN_DOLEWKA.md`): status received over ESP-NOW, tile "Dolewka" and the
top-off screen with live data, node alarms on the alarm strip, "brak łączności" after 15 s without a status.
Controls on the top-off screen: Auto / Serwis, manual portions (bargraph 1–10 + ON, STOP during the
session), "Kasuj alarm" and the daily limit (both with a dialog). A tapped control shows "sent" (outline)
until the node's ACK and next status; the result (or the node's refusal) comes as a toast.
Thermostat node (stage T1, `docs/PLAN_TERMOSTAT.md`): status received over ESP-NOW and logged on Serial, no
screen yet.

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
`nice -n 19 pio run -j 6` keeps the PC usable. The panel has its own package dir
(`packages_dir = ~/.platformio-panel/packages`, ~6 GB): the hybrid compile rewrites the framework libs
in place, and the lamps use the same pioarduino platform — a shared dir meant a re-download or a
full recompile on every switch between the projects. Without XIP from PSRAM any flash write (NVS on Wi-Fi
init, OTA) panics in the RGB bounce-buffer ISR.
Serial goes over native USB (USB-CDC on boot). If the port does not show up, hold BOOT and press RESET.

## GUI simulator (PC)

The panel GUI (`src/gui/`, LVGL only) also runs on the PC in an 800x480 SDL2 window, with mock data
(`sim/mock.cpp`): mouse = touch, keys toggle states (lamp offline, FRAM/RTC error, mode mismatch,
alarms, command without ACK, no Wi-Fi, top-off reservoir level / daily limit / no ToF reading / offline /
auto cycle, thermostat temperature / sensor fault / FRAM-RTC / trend / offline, ...; `h` prints the list). The
top-off screen (tile "Dolewka") and the thermostat screen (tile "Termostat", stage T2) answer commands from the mock.

```sh
sudo apt install libsdl2-dev                     # once
pio run                                          # once: downloads LVGL (lib_deps) used by the simulator
cmake -S sim -B sim/build && cmake --build sim/build -j 6
./sim/build/panel_sim
```

Scripted run without a person (screenshots for review, PPM = window pixels including modals):

```sh
./sim/build/panel_sim --script "w1500;k5;c130,160;w800;s/tmp/lamps.ppm;q"
# wN wait N ms · kC key · cX,Y click · sFILE screenshot · q quit
```

Screens: overview (tiles per node / group), lamp group (rows per lamp, Auto / Noc / Test,
Program…), program list with confirmation, alarm list; alarm strip under the top bar only while
alarms are active; screensaver after 60 s without input. On the panel the same code runs over the
model built from the lamp protocol (`src/app/panel_model.cpp`).

## Stack

- Display: ESP-IDF `esp_lcd` RGB panel, two framebuffers in PSRAM + bounce buffers in SRAM.
  LVGL renders in `DIRECT` mode into the hidden framebuffer; the flush swaps buffers and waits
  for the end of the frame in progress (`src/lcd.cpp`, `src/ui/lvgl_port.cpp`).
- GUI: LVGL 9.2.2 (`lib_deps`), configuration in `include/lv_conf.h`.
- Touch: own GT911 driver (`src/gt911.cpp`) as an LVGL pointer input.
- Fonts: Noto Sans Regular/Bold TTF embedded in flash (`src/gui/fonts.S`, `assets/fonts/`, OFL)
  and rendered at runtime by LVGL TinyTTF — full Polish character set without a font converter.
  LVGL symbol icons come from the built-in Montserrat fonts set as fallback.
- Network: Wi-Fi STA with `setSleep(false)` (modem sleep drops ESP-NOW frames), auto-reconnect,
  NTP with the same servers and time zone as the lamps, ArduinoOTA (`src/net/network.cpp`).
- Lamps: passive listener of the lamp ESP-NOW protocol (`src/lamps/`): group key
  `SHA-256("RL90-ESPNOW-v1:" + Wi-Fi password)`, HMAC-SHA256/16 per frame, replay check by
  `(boot_id, seq)` per sender, lamp online = heartbeat within 45 s. Wire format copied from
  `led_lamp_aquarium-iot` (`lamp_proto.h`, sizes checked at compile time). Only broadcasts are
  visible (heartbeats, live mode frames). Without NTP the clock is taken from a lamp heartbeat with
  NTP time. The panel broadcasts `FT_CTRL_HB` (name, FW from `src/version.h`, IP) every 15 s while
  connected: lamps register it as a controller, not as a lamp.
- Backlight is on/off only (CH422G EXIO2). The CH422G keeps its outputs across an ESP32 reset, so the
  backlight is switched off before every restart (OTA, `r`) and stays off at boot until the first
  frame is drawn — otherwise the un-driven panel shows ~1 s of garbage. With the backlight off the first touch only wakes the
  screen and is not passed to the UI.

## Panel GUI

- Lamp names = device names from the lamps' provisioning (heartbeat), sorted by name.
- Group mode / program = the variant held by most online lamps; any difference = "rozjazd" (P3).
- Program list: when `catalog_hash` in the heartbeats changes, the panel asks one lamp with working
  FRAM for its program ids (`FT_LIST_REQ`) and reads each unknown name from chunk 0 of the program
  (`FT_PROG_REQ`); retry after 10 s on failure.
- Alarms: P2 lamp offline, lamp FRAM error, no lamp heartbeat 60 s after boot, no Wi-Fi 30 s after boot;
  P3 lamp RTC error, orphaned program, mode / program mismatch, no NTP time 2 min after boot.
- Control (`src/lamps/lamp_control.cpp`, lamp protocol): mode = `FT_MODE` to every online lamp, new
  session `clock = max(UTC, highest seen + 1)`, retried every 250 ms until `FT_MODE_ACK`, Test at 50 %;
  program = `FT_ACTIVATE` to every online lamp, retried every 500 ms until `FT_ACK`. A lamp without the
  program fetches it from the panel, which relays the blob from a lamp that has it. The GUI stays
  "pending" until the heartbeats show the new state, then one toast with the result. Offline lamps are
  not addressed (mismatch shown when they return).
- OTA upload shows a progress modal on the screen.

Serial commands: `i` memory info, `b` toggle backlight, `w` network / time info, `l` lamp table with
all heartbeat fields and counters (a growing "bad HMAC" = different Wi-Fi password than the lamps),
`t` top-off status and counters, `h` thermostat status and counters, `f` toggle FPS / CPU overlay, `r` restart.
Top-off command tests (stage D4, answer logged as `Topoff: ACK ...`): `A` Auto, `S` Service, `P` 3 portions,
`X` stop portions, `E` error reset, `L` daily limit +50 ml, `K` daily limit 50 ml (out of range), `D` repeat
the last command with the same `cmd_id` (node must not run it again), `Y` last command with a new `cmd_id`
and the old nonce (`STALE_NONCE`).

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

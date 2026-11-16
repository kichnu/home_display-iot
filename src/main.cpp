// Touch panel firmware for Waveshare ESP32-S3-Touch-LCD-4.3B — stage 6: panel GUI with live lamp data and group control.
//
// Boot: I2C scan, CH422G (backlight off until the first frame), RGB panel with two PSRAM framebuffers, GT911,
// LVGL 9 port (DIRECT mode, TinyTTF Noto fonts), Wi-Fi STA + NTP + ArduinoOTA, ESP-NOW listener of the lamps
// (HMAC + replay check, lamp table, program catalog) with controller heartbeat FT_CTRL_HB, GUI (src/gui/,
// same code as the PC simulator) over the model built in src/app/panel_model.
//
// Serial commands: i = memory info, b = toggle backlight, w = network / time info,
// l = lamp table, t = top-off status, f = toggle FPS / CPU overlay, r = restart.
// Top-off command tests (stage D4): A = Auto, S = Service, P = 3 portions, X = stop portions,
// E = error reset, L = daily limit +50 ml, K = daily limit 50 ml (out of range),
// D = repeat the last command with the same cmd_id, Y = last command with a new cmd_id and old nonce.

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <string.h>

#include "app/panel_model.h"
#include "board_pins.h"
#include "ch422g.h"
#include "gt911.h"
#include "gui/gui.h"
#include "lamps/lamp_listener.h"
#include "thermo/thermo_listener.h"
#include "topoff/topoff_listener.h"
#include "lcd.h"
#include "version.h"
#include "net/network.h"
#include "ui/lvgl_port.h"

namespace {

bool g_perfShown = false;

void i2cScan() {
    Serial.print("I2C:");
    for (uint8_t a = 1; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X", a);
    }
    Serial.println("  (0x20-0x3F = CH422G commands)");
}

void printMemory() {
    Serial.printf("Heap: internal %u free (min %u), PSRAM %u free (min %u)\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}

void topoffTest(char c) {
    using namespace topoff;
    bool ok = false;
    switch (c) {
        case 'A': ok = sendCommand(CMD_SET_MODE, 1); break;
        case 'S': ok = sendCommand(CMD_SET_MODE, 0); break;
        case 'P': ok = sendCommand(CMD_PORTIONS, 3); break;
        case 'X': ok = sendCommand(CMD_PUMP_STOP, 0); break;
        case 'E': ok = sendCommand(CMD_ERROR_RESET, 0); break;
        case 'L': ok = sendCommand(CMD_SET_DAILY_LIMIT, node().st.daily_limit_ml + 50); break;
        case 'K': ok = sendCommand(CMD_SET_DAILY_LIMIT, 50); break;
        case 'D': ok = testRepeatLast(); break;
        case 'Y': ok = testStaleNonce(); break;
    }
    if (!ok) Serial.println("Topoff: command not sent (no key, node offline or a command in flight)");
}

void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == 'i') {
            printMemory();
        } else if (c == 'b') {
            ui::setBacklight(!ui::backlight());
            Serial.printf("Backlight %s\n", ui::backlight() ? "on" : "off");
        } else if (c == 'w') {
            net::printInfo();
        } else if (c == 'l') {
            lamps::printTable();
        } else if (c == 't') {
            topoff::printStatus();
        } else if (c == 'h') {
            thermo::printStatus();
        } else if (c && strchr("ASPXELKDY", c)) {
            topoffTest(c);
        } else if (c == 'f') {
            g_perfShown = !g_perfShown;
            if (g_perfShown) lv_sysmon_show_performance(nullptr);
            else lv_sysmon_hide_performance(nullptr);
        } else if (c == 'r') {
            Serial.println("Restart");
            ui::restart();
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);
    Serial.println("\n=== home_display-iot " PANEL_FW_VERSION ": stage 6 + D5 + T3 (GUI, lamps, top-off, thermostat status read only) ===");
    Serial.printf("Chip %s rev %d, PSRAM %u B\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getPsramSize());

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    i2cScan();

    if (!ch422g::begin()) Serial.println("CH422G: not responding");
    if (!lcd::begin()) {
        Serial.println("LCD: init failed, halting");
        while (true) delay(1000);
    }

    gt911::Info tp = {};
    if (gt911::begin(&tp)) {
        Serial.printf("GT911: 0x%02X id %s fw 0x%04X cfg %ux%u\n", tp.addr, tp.productId, tp.firmware, tp.cfgX,
                      tp.cfgY);
    } else {
        Serial.println("GT911: not found");
    }

    if (!ui::begin()) {
        Serial.println("LVGL: display create failed, halting");
        while (true) delay(1000);
    }
    app::update();
    gui::begin(&app::model(), &app::commands());
    lv_sysmon_hide_performance(nullptr);  // Serial 'f' shows it
    lv_refr_now(nullptr);  // first full frame into the framebuffer, then light the screen
    ui::setBacklight(true);
    net::begin();
    lamps::begin();
    topoff::begin();
    thermo::begin();
    printMemory();
}

void loop() {
    net::update();
    lamps::update();
    topoff::update();
    app::update();
    ui::update();
    gui::update();
    handleSerial();
    delay(2);
}

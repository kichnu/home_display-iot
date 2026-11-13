// Touch panel firmware for Waveshare ESP32-S3-Touch-LCD-4.3B — stage 4: lamp listener + controller heartbeat.
//
// Boot: I2C scan, CH422G (backlight off until the first frame), RGB panel with two PSRAM framebuffers, GT911,
// LVGL 9 port (DIRECT mode, TinyTTF Noto fonts), test screen, Wi-Fi STA + NTP + ArduinoOTA,
// ESP-NOW listener of the lamps (HMAC + replay check, lamp table) and controller heartbeat FT_CTRL_HB.
//
// Serial commands: i = memory info, b = toggle backlight, w = network / time info,
// l = lamp table, r = restart.

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "board_pins.h"
#include "ch422g.h"
#include "gt911.h"
#include "lamps/lamp_listener.h"
#include "lcd.h"
#include "version.h"
#include "net/network.h"
#include "ui/demo_screen.h"
#include "ui/lvgl_port.h"

namespace {

uint32_t g_lastTick = 0;

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
    Serial.println("\n=== home_display-iot " PANEL_FW_VERSION ": stage 4 (controller heartbeat) ===");
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
    ui::createDemoScreen();
    lv_refr_now(nullptr);  // first full frame into the framebuffer, then light the screen
    ui::setBacklight(true);
    net::begin();
    lamps::begin();
    printMemory();
}

void loop() {
    net::update();
    lamps::update();
    ui::update();
    handleSerial();
    if (millis() - g_lastTick >= 1000) {
        g_lastTick = millis();
        ui::demoTick();
    }
    delay(2);
}

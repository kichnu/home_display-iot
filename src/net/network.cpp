#include "network.h"

#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sntp.h>
#include <lvgl.h>
#include <time.h>

#include "ota_secret.h"
#include "../gui/fonts.h"
#include "../ui/lvgl_port.h"
#include "wifi_secret.h"

namespace net {

namespace {
constexpr const char* HOSTNAME = "home-display";
// Same servers and zone as the lamps (IP addresses: no dependency on DNS of the IoT VLAN)
constexpr const char* NTP_SERVER_1 = "216.239.35.0";   // time.google.com
constexpr const char* NTP_SERVER_2 = "216.239.35.4";   // time2.google.com
constexpr const char* NTP_SERVER_3 = "162.159.200.1";  // time.cloudflare.com
constexpr const char* POLAND_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr time_t MIN_VALID_EPOCH = 1735689600;  // 2025-01-01

volatile bool s_ntpSynced = false;  // set from the lwIP/SNTP task, logged from loop()
bool s_wasConnected = false;
bool s_otaActive = false;
uint32_t s_otaLastDraw = 0;
lv_obj_t* s_otaBox = nullptr;
lv_obj_t* s_otaBar = nullptr;

void onTimeSync(struct timeval*) { s_ntpSynced = true; }

// From eFuse: WiFi.macAddress() returns zeros until the driver has started
String staMac() {
    uint8_t m[6] = {};
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(buf);
}

// OTA runs synchronously inside ArduinoOTA.handle() (loop task = UI task), so the progress
// callback may call lv_* and refresh the screen itself.
void otaShow() {
    s_otaBox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_otaBox, 520, 200);
    lv_obj_center(s_otaBox);
    lv_obj_set_flex_flow(s_otaBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_otaBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_otaBox, 24, 0);
    lv_obj_t* t = lv_label_create(s_otaBox);
    lv_obj_set_style_text_font(t, gui::fonts().title, 0);
    lv_label_set_text(t, LV_SYMBOL_DOWNLOAD "  Aktualizacja firmware");
    s_otaBar = lv_bar_create(s_otaBox);
    lv_obj_set_size(s_otaBar, lv_pct(90), 28);
    lv_bar_set_range(s_otaBar, 0, 100);
    ui::setBacklight(true);
    ui::update();
}

void otaHide() {
    if (s_otaBox) lv_obj_delete(s_otaBox);
    s_otaBox = nullptr;
    s_otaBar = nullptr;
}

void otaBegin() {
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.setRebootOnSuccess(false);  // restart via ui::restart() — backlight off first
    ArduinoOTA.onStart([]() {
        s_otaActive = true;
        Serial.println("=== OTA start ===");
        otaShow();
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        if (!s_otaBar || total == 0 || millis() - s_otaLastDraw < 250) return;
        s_otaLastDraw = millis();
        lv_bar_set_value(s_otaBar, (int)(100ULL * done / total), LV_ANIM_OFF);
        ui::update();
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("=== OTA OK, restarting ===");
        Serial.flush();
        ui::restart();
    });
    ArduinoOTA.onError([](ota_error_t error) {
        s_otaActive = false;
        Serial.printf("=== OTA error (%d) ===\n", (int)error);
        otaHide();
    });
    ArduinoOTA.begin();
    Serial.println("ArduinoOTA ready (port 3232)");
}
}  // namespace

void begin() {
    WiFi.persistent(false);  // credentials come from the firmware, do not wear NVS
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // modem sleep drops ESP-NOW frames between beacons
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    Serial.printf("Wi-Fi MAC %s\n", staMac().c_str());

    // NTP + time zone; the SNTP client starts once the network is up
    sntp_set_time_sync_notification_cb(onTimeSync);
    configTzTime(POLAND_TZ, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

    if (WIFI_SSID[0] == '\0') {
        Serial.println("Wi-Fi: no credentials in this build (set IOT_WIFI_FALLBACK_* in ~/.secrets/iot.env)");
        return;
    }
    Serial.printf("Wi-Fi: connecting to %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // no waiting: the UI runs without network
    otaBegin();
}

void update() {
    if (WIFI_SSID[0] == '\0') return;
    ArduinoOTA.handle();
    if (s_otaActive) return;

    bool c = connected();
    if (c != s_wasConnected) {
        s_wasConnected = c;
        if (c) {
            Serial.printf("Wi-Fi connected: IP %s, RSSI %d dBm, channel %d\n", WiFi.localIP().toString().c_str(),
                          WiFi.RSSI(), WiFi.channel());
        } else {
            Serial.println("Wi-Fi disconnected");
        }
    }
    if (s_ntpSynced) {
        s_ntpSynced = false;
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &lt);
        Serial.printf("NTP: time set %s\n", buf);
    }
}

bool connected() { return WiFi.status() == WL_CONNECTED; }

bool timeValid() { return time(nullptr) >= MIN_VALID_EPOCH; }

String ip() { return connected() ? WiFi.localIP().toString() : String(); }

int rssi() { return connected() ? WiFi.RSSI() : 0; }

void printInfo() {
    Serial.printf("Wi-Fi: MAC %s, SSID %s, status %d", staMac().c_str(), WIFI_SSID, (int)WiFi.status());
    if (connected()) {
        Serial.printf(", IP %s, RSSI %d dBm, channel %d", WiFi.localIP().toString().c_str(), WiFi.RSSI(),
                      WiFi.channel());
    }
    Serial.println();
    if (timeValid()) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &lt);
        Serial.printf("Time: %s\n", buf);
    } else {
        Serial.println("Time: not set");
    }
}

}  // namespace net

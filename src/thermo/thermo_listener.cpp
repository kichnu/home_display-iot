#include "thermo_listener.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <mbedtls/md.h>
#include <string.h>

#include "wifi_secret.h"

namespace thermo {

namespace {

uint8_t s_key[32];
Stats s_stats = {};
Node s_node = {};
uint32_t s_bootId = 0;
uint32_t s_lastSeq = 0;

bool tagOk(const uint8_t* data, size_t len, const uint8_t* tag) {
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), data, len, full);
    uint8_t diff = 0;
    for (size_t i = 0; i < TAG_LEN; i++) diff |= full[i] ^ tag[i];
    return diff == 0;
}

const char* statusName(uint8_t s) {
    switch (s) {
        case NS_OK: return "OK";
        case NS_WARNING: return "Uwaga";
        case NS_ALARM: return "Alarm";
        case NS_FAULT: return "Awaria";
        case NS_SERVICE: return "Serwis";
        default: return "?";
    }
}

const char* stateName(uint8_t s) {
    switch (s) {
        case AS_IDLE: return "Czuwa";
        case AS_HEATING: return "Grzeje";
        case AS_COOLING: return "Chłodzi";
        case AS_ALARM_LOW: return "Za zimno";
        case AS_ALARM_HIGH: return "Za ciepło";
        case AS_SENSOR_FAULT: return "Awaria czujnika";
        case AS_TREND_ALARM: return "Grzałka nieskuteczna";
        default: return "?";
    }
}

// Fields worth a log line when they change (no clock, countdown or temperature)
bool sameKey(const StatusBody& a, const StatusBody& b) {
    return a.node_status == b.node_status && a.auto_mode == b.auto_mode && a.algo_state == b.algo_state &&
           (a.flags & ~SF_TIME_VALID) == (b.flags & ~SF_TIME_VALID) && a.fan_pct == b.fan_pct &&
           a.alarms == b.alarms && a.muted == b.muted && a.hw_fault == b.hw_fault;
}

void logStatus(const Node& n) {
    const StatusBody& s = n.st;
    char temp[12];
    if (s.flags & SF_TEMP_VALID) snprintf(temp, sizeof(temp), "%.2f C", s.temp_x100 / 100.0f);
    else snprintf(temp, sizeof(temp), "-- C");
    Serial.printf("Thermo: %.32s %s %s (%u s) | %s | %s | heater %s fan %u%% | flags 0x%02X | alarms 0x%04X muted 0x%04X"
                  " hw 0x%02X | fw %.12s rssi %d\n",
                  s.name, statusName(s.node_status), s.auto_mode ? "Auto" : "Serwis", s.service_left_s, temp,
                  stateName(s.algo_state), (s.flags & SF_HEATER_ON) ? "ON" : "OFF", s.fan_pct, s.flags, s.alarms,
                  s.muted, s.hw_fault, s.fw, n.rssi);
}

}  // namespace

void begin() {
    if (WIFI_PASSWORD[0] == '\0') {
        Serial.println("Thermo: no Wi-Fi password in this build — listener disabled");
        return;
    }
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)KEY_CONTEXT, strlen(KEY_CONTEXT));
    mbedtls_md_update(&ctx, (const uint8_t*)WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    mbedtls_md_finish(&ctx, s_key);
    mbedtls_md_free(&ctx);
    s_stats.enabled = true;
}

void handleFrame(const uint8_t* mac, int8_t rssi, const uint8_t* data, size_t len) {
    if (!s_stats.enabled) return;
    if (len < sizeof(FrameHdr) + TAG_LEN) {
        s_stats.rxBadFormat++;
        return;
    }
    size_t n = len - TAG_LEN;
    FrameHdr h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != FRAME_MAGIC || h.version != FRAME_VERSION) {
        s_stats.rxBadFormat++;
        return;
    }
    if (!tagOk(data, n, data + n)) {
        s_stats.rxBadTag++;
        return;
    }
    bool sameSender = s_node.have && memcmp(s_node.mac, mac, 6) == 0;
    if (sameSender && h.boot_id == s_bootId && h.seq <= s_lastSeq) {
        s_stats.rxReplay++;
        return;
    }
    s_bootId = h.boot_id;
    s_lastSeq = h.seq;
    s_stats.rxOk++;

    if (h.type != FT_STATUS || n - sizeof(h) < sizeof(StatusBody)) return;
    StatusBody b;
    memcpy(&b, data + sizeof(h), sizeof(b));
    b.name[sizeof(b.name) - 1] = '\0';
    b.fw[sizeof(b.fw) - 1] = '\0';

    bool changed = !s_node.have || !sameKey(b, s_node.st);
    memcpy(s_node.mac, mac, 6);
    s_node.st = b;
    s_node.lastMs = millis();
    s_node.count++;
    s_node.rssi = rssi;
    s_node.have = true;
    if (changed) logStatus(s_node);
}

const Node& node() { return s_node; }
const Stats& stats() { return s_stats; }

void printStatus() {
    Serial.printf("Thermo: rx ok %lu, bad format %lu, bad HMAC %lu, replay %lu\n", (unsigned long)s_stats.rxOk,
                  (unsigned long)s_stats.rxBadFormat, (unsigned long)s_stats.rxBadTag,
                  (unsigned long)s_stats.rxReplay);
    if (!s_node.have) {
        Serial.println("Thermo: no status yet");
        return;
    }
    Serial.printf("Thermo: %02X:%02X:%02X:%02X:%02X:%02X %s, status #%lu %lu s ago, utc %lu\n", s_node.mac[0],
                  s_node.mac[1], s_node.mac[2], s_node.mac[3], s_node.mac[4], s_node.mac[5],
                  IPAddress(s_node.st.ip).toString().c_str(), (unsigned long)s_node.count,
                  (unsigned long)((millis() - s_node.lastMs) / 1000), (unsigned long)s_node.st.utc);
    logStatus(s_node);
}

}  // namespace thermo

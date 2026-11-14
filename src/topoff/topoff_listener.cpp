#include "topoff_listener.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <mbedtls/md.h>
#include <string.h>

#include "wifi_secret.h"

namespace topoff {

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

// Fields worth a log line when they change (no clock, countdowns or ToF noise)
bool sameKey(const StatusBody& a, const StatusBody& b) {
    return a.node_status == b.node_status && a.auto_mode == b.auto_mode && a.algo_state == b.algo_state &&
           a.error_code == b.error_code && (a.flags & ~SF_TIME_VALID) == (b.flags & ~SF_TIME_VALID) &&
           a.manual_left == b.manual_left && a.volume_24h_ml == b.volume_24h_ml &&
           a.daily_limit_ml == b.daily_limit_ml && a.alarms == b.alarms && a.res_zone == b.res_zone &&
           a.res_portions == b.res_portions && a.hw_fault == b.hw_fault;
}

void logStatus(const Node& n) {
    const StatusBody& s = n.st;
    Serial.printf("Topoff: %.32s %s %s | state %u err %u | flags 0x%02X manual %u/%u | 24h %u/%u ml dose %u"
                  " | res zone %u %u%% (%u/%u%%) %u portions %.1f h | alarms 0x%04X hw 0x%02X | fw %.12s rssi %d\n",
                  s.name, statusName(s.node_status), s.auto_mode ? "Auto" : "Serwis", s.algo_state, s.error_code,
                  s.flags, s.manual_done, s.manual_left, s.volume_24h_ml, s.daily_limit_ml, s.dose_ml, s.res_zone,
                  s.res_pct, s.res_reserve_pct, s.res_alarm_pct, s.res_portions,
                  s.res_hours_x10 < 0 ? -1.0f : s.res_hours_x10 / 10.0f, s.alarms, s.hw_fault, s.fw, n.rssi);
}

}  // namespace

void begin() {
    if (WIFI_PASSWORD[0] == '\0') {
        Serial.println("Topoff: no Wi-Fi password in this build — listener disabled");
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
    Serial.printf("Topoff: rx ok %lu, bad format %lu, bad HMAC %lu, replay %lu\n", (unsigned long)s_stats.rxOk,
                  (unsigned long)s_stats.rxBadFormat, (unsigned long)s_stats.rxBadTag,
                  (unsigned long)s_stats.rxReplay);
    if (!s_node.have) {
        Serial.println("Topoff: no status yet");
        return;
    }
    Serial.printf("Topoff: %02X:%02X:%02X:%02X:%02X:%02X %s, status #%lu %lu s ago, utc %lu\n", s_node.mac[0],
                  s_node.mac[1], s_node.mac[2], s_node.mac[3], s_node.mac[4], s_node.mac[5],
                  IPAddress(s_node.st.ip).toString().c_str(), (unsigned long)s_node.count,
                  (unsigned long)((millis() - s_node.lastMs) / 1000), (unsigned long)s_node.st.utc);
    logStatus(s_node);
}

}  // namespace topoff

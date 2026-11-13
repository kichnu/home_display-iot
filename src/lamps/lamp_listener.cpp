#include "lamp_listener.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <mbedtls/md.h>
#include <sys/time.h>

#include "../net/network.h"
#include "wifi_secret.h"

namespace lamps {

namespace {

struct RxItem {
    uint8_t mac[6];
    int8_t rssi;
    uint8_t len;
    uint8_t data[FRAME_MAX];
};

constexpr int RX_QUEUE_LEN = 16;
constexpr int RX_PER_UPDATE = 8;
constexpr time_t UTC_VALID_MIN = 1735689600;  // 2025-01-01

QueueHandle_t s_rxQueue = nullptr;
volatile uint32_t s_rxDropped = 0;  // written from the Wi-Fi task
uint8_t s_key[32];
Lamp s_lamps[MAX_LAMPS];
// Replay state per sender, kept apart from Lamp (the UI only needs Lamp)
uint32_t s_bootId[MAX_LAMPS];
uint32_t s_lastSeq[MAX_LAMPS];
Stats s_stats = {};
uint32_t s_lastOnlineCheckMs = 0;

void macStr(const uint8_t* m, char* out) {  // 18 B
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

bool tagOk(const uint8_t* data, size_t len, const uint8_t* tag) {
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), data, len, full);
    uint8_t diff = 0;
    for (size_t i = 0; i < TAG_LEN; i++) diff |= full[i] ^ tag[i];
    return diff == 0;
}

int findLamp(const uint8_t* mac) {
    for (int i = 0; i < MAX_LAMPS; i++)
        if (s_lamps[i].used && memcmp(s_lamps[i].mac, mac, 6) == 0) return i;
    return -1;
}

int addLamp(const uint8_t* mac) {
    int slot = -1;
    for (int i = 0; i < MAX_LAMPS; i++)
        if (!s_lamps[i].used) {
            slot = i;
            break;
        }
    if (slot < 0) {  // full: evict the longest silent one
        slot = 0;
        uint32_t now = millis();
        for (int i = 1; i < MAX_LAMPS; i++)
            if (now - s_lamps[i].lastSeenMs > now - s_lamps[slot].lastSeenMs) slot = i;
    }
    memset(&s_lamps[slot], 0, sizeof(Lamp));
    s_lamps[slot].used = true;
    memcpy(s_lamps[slot].mac, mac, 6);
    char m[18];
    macStr(mac, m);
    Serial.printf("Lamps: new sender %s\n", m);
    return slot;
}

// Fallback clock: no NTP on the panel yet, a lamp has fresh NTP time (design: time from node heartbeats)
void maybeTakeTime(const HbBody& hb) {
    if (net::timeValid() || hb.time_q != TQ_NTP || hb.utc < UTC_VALID_MIN) return;
    struct timeval tv = {(time_t)hb.utc, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("Lamps: clock set from %.32s heartbeat (no NTP yet)\n", hb.name);
}

void onHeartbeat(Lamp& l, const uint8_t* body, size_t blen) {
    HbBody b = {};
    memcpy(&b, body, blen < sizeof(b) ? blen : sizeof(b));
    b.name[sizeof(b.name) - 1] = '\0';
    b.active_name[sizeof(b.active_name) - 1] = '\0';
    b.fw[sizeof(b.fw) - 1] = '\0';

    bool first = !l.haveHb;
    bool changed = first || b.mode != l.hb.mode || b.active_id != l.hb.active_id || b.flags != l.hb.flags ||
                   b.catalog_hash != l.hb.catalog_hash || b.time_q != l.hb.time_q;
    l.hb = b;
    l.haveHb = true;
    l.hbHasModeClock = blen >= sizeof(HbBody);
    l.lastHbMs = millis();
    l.hbCount++;
    if (!l.online) {
        l.online = true;
        Serial.printf("Lamps: %s online\n", b.name);
    }
    if (changed) {
        Serial.printf("Lamps: %s mode %s, program \"%s\", catalog %08lX (%u), time %s, flags 0x%02X, fw %s\n", b.name,
                      modeName(b.mode), b.active_name, (unsigned long)b.catalog_hash, b.prog_count,
                      timeQualityName(b.time_q), b.flags, b.fw);
    }
    maybeTakeTime(b);
}

void handleFrame(const RxItem& it) {
    if (it.len < sizeof(FrameHdr) + TAG_LEN) {
        s_stats.rxBadFormat++;
        return;
    }
    size_t n = it.len - TAG_LEN;
    FrameHdr h;
    memcpy(&h, it.data, sizeof(h));
    if (h.magic != FRAME_MAGIC || h.version != FRAME_VERSION) {
        s_stats.rxBadFormat++;
        return;
    }
    if (!tagOk(it.data, n, it.data + n)) {
        s_stats.rxBadTag++;
        return;
    }

    int i = findLamp(it.mac);
    if (i >= 0 && s_bootId[i] == h.boot_id && h.seq <= s_lastSeq[i]) {
        s_stats.rxReplay++;
        return;
    }
    if (i < 0) i = addLamp(it.mac);
    s_bootId[i] = h.boot_id;
    s_lastSeq[i] = h.seq;

    Lamp& l = s_lamps[i];
    l.lastSeenMs = millis();
    l.rssi = it.rssi;
    s_stats.rxOk++;
    if (h.type < FT_COUNT) s_stats.byType[h.type]++;

    const uint8_t* body = it.data + sizeof(h);
    size_t blen = n - sizeof(h);
    switch (h.type) {
        case FT_HB:
            // Old firmware: HB without mode_clock — accepted, like the lamps do
            if (blen >= offsetof(HbBody, mode_clock)) onHeartbeat(l, body, blen);
            break;
        case FT_MODE:
            if (blen >= sizeof(ModeBody)) {
                ModeBody b;
                memcpy(&b, body, sizeof(b));
                Serial.printf("Lamps: %s FT_MODE %s clock %lu seq %lu ack %u\n", l.haveHb ? l.hb.name : "?",
                              modeName(b.mode), (unsigned long)b.clock, (unsigned long)b.seq, b.ack);
            }
            break;
        default:
            break;
    }
}

void checkOnline() {
    uint32_t now = millis();  // after the RX queue: lastHbMs may be later than an earlier now
    for (auto& l : s_lamps) {
        if (!l.used || !l.online) continue;
        if (now - l.lastHbMs >= ONLINE_MS) {
            l.online = false;
            Serial.printf("Lamps: %s offline (no heartbeat for %lu s)\n", l.hb.name,
                          (unsigned long)((now - l.lastHbMs) / 1000));
        }
    }
}

// Wi-Fi task: copy to the queue only
void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (!s_rxQueue || len <= 0 || len > (int)FRAME_MAX) return;
    RxItem it;
    memcpy(it.mac, info->src_addr, 6);
    it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
    it.len = (uint8_t)len;
    memcpy(it.data, data, len);
    if (xQueueSend(s_rxQueue, &it, 0) != pdTRUE) s_rxDropped = s_rxDropped + 1;
}

}  // namespace

void begin() {
    if (WIFI_PASSWORD[0] == '\0') {
        Serial.println("Lamps: no Wi-Fi password in this build — no group key, listener disabled");
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

    if (esp_now_init() != ESP_OK) {
        Serial.println("Lamps: esp_now_init failed");
        return;
    }
    s_rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(RxItem));
    esp_now_register_recv_cb(onRecv);
    s_stats.enabled = true;
    Serial.println("Lamps: ESP-NOW listener ready (passive, channel = Wi-Fi channel)");
}

void update() {
    if (!s_stats.enabled) return;
    RxItem it;
    for (int i = 0; i < RX_PER_UPDATE && xQueueReceive(s_rxQueue, &it, 0) == pdTRUE; i++) handleFrame(it);
    s_stats.rxDropped = s_rxDropped;
    if (millis() - s_lastOnlineCheckMs >= 1000) {
        s_lastOnlineCheckMs = millis();
        checkOnline();
    }
}

const Lamp* lamp(int i) { return (i >= 0 && i < MAX_LAMPS && s_lamps[i].used) ? &s_lamps[i] : nullptr; }

const Stats& stats() { return s_stats; }

const char* modeName(uint8_t mode) {
    switch (mode) {
        case MODE_PROGRAM: return "Auto";
        case MODE_TEST: return "Test";
        case MODE_NIGHT: return "Noc";
        default: return "?";
    }
}

const char* timeQualityName(uint8_t q) {
    switch (q) {
        case TQ_NTP: return "NTP";
        case TQ_RTC: return "RTC";
        default: return "-";
    }
}

void printTable() {
    const Stats& s = s_stats;
    Serial.printf("Lamps: rx ok %lu, bad format %lu, bad HMAC %lu, replay %lu, dropped %lu | HB %lu MODE %lu other %lu\n",
                  (unsigned long)s.rxOk, (unsigned long)s.rxBadFormat, (unsigned long)s.rxBadTag,
                  (unsigned long)s.rxReplay, (unsigned long)s.rxDropped, (unsigned long)s.byType[FT_HB],
                  (unsigned long)s.byType[FT_MODE],
                  (unsigned long)(s.rxOk - s.byType[FT_HB] - s.byType[FT_MODE]));
    uint32_t now = millis();
    for (const auto& l : s_lamps) {
        if (!l.used) continue;
        char m[18];
        macStr(l.mac, m);
        if (!l.haveHb) {
            Serial.printf("  %s  (no heartbeat yet), seen %lu s ago\n", m, (unsigned long)((now - l.lastSeenMs) / 1000));
            continue;
        }
        IPAddress ip(l.hb.ip);
        Serial.printf("  %s %-16s %-15s %-7s hb %2lus ago (#%lu) rssi %d | %-4s \"%s\" catalog %08lX/%u | time %s utc %lu"
                      " | flags 0x%02X | mode_clock %lu%s | fw %s\n",
                      m, l.hb.name, ip.toString().c_str(), l.online ? "online" : "OFFLINE",
                      (unsigned long)((now - l.lastHbMs) / 1000), (unsigned long)l.hbCount, l.rssi, modeName(l.hb.mode),
                      l.hb.active_name, (unsigned long)l.hb.catalog_hash, l.hb.prog_count,
                      timeQualityName(l.hb.time_q), (unsigned long)l.hb.utc, l.hb.flags,
                      (unsigned long)l.hb.mode_clock, l.hbHasModeClock ? "" : " (old fw)", l.hb.fw);
    }
}

}  // namespace lamps

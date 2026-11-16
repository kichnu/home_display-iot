#include "thermo_listener.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <esp_now.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <string.h>

#include "../net/network.h"
#include "wifi_secret.h"

namespace thermo {

namespace {

uint8_t s_key[32];
Stats s_stats = {};
Node s_node = {};
constexpr time_t UTC_VALID_MIN = 1735689600;  // 2025-01-01
uint32_t s_bootId = 0;
uint32_t s_lastSeq = 0;
uint32_t s_ownBootId = 0;   // our command frames
uint32_t s_txSeq = 0;
uint32_t s_nonce = 0;       // node's current cmd_nonce (latest status or ACK)
Command s_cmd = {};

bool sendFrame(const uint8_t* mac, FrameType type, const void* body, size_t bodyLen) {
    uint8_t buf[FRAME_MAX];
    FrameHdr h = {FRAME_MAGIC, FRAME_VERSION, (uint8_t)type, s_ownBootId, ++s_txSeq};
    size_t n = sizeof(h) + bodyLen;
    if (n + TAG_LEN > sizeof(buf)) return false;
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), body, bodyLen);
    uint8_t full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), s_key, sizeof(s_key), buf, n, full);
    memcpy(buf + n, full, TAG_LEN);
    if (esp_now_send(mac, buf, n + TAG_LEN) != ESP_OK) {
        s_stats.txFail++;
        return false;
    }
    s_stats.txCmd++;
    return true;
}

bool ensurePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, mac, 6);
    p.channel = 0;
    p.ifidx = WIFI_IF_STA;
    p.encrypt = false;
    return esp_now_add_peer(&p) == ESP_OK;
}

void transmit() {
    s_cmd.tries++;
    s_cmd.lastMs = millis();
    sendFrame(s_node.mac, FT_CMD, &s_cmd.body, sizeof(s_cmd.body));
}

// Start a command with the given body (cmd_id / nonce already set)
bool start(const CmdBody& b) {
    if (!s_stats.enabled || !online() || s_cmd.state == CmdState::Waiting || !ensurePeer(s_node.mac)) return false;
    s_cmd.state = CmdState::Waiting;
    s_cmd.body = b;
    s_cmd.tries = 0;
    s_cmd.ack = {};
    transmit();
    Serial.printf("Thermo: command %s arg %u sent (id %08lX)\n", cmdName(b.cmd), b.arg, (unsigned long)b.cmd_id);
    return true;
}

uint32_t newCmdId() {
    uint32_t id;
    do id = esp_random();
    while (id == 0 || id == s_cmd.body.cmd_id);
    return id;
}

void onAck(const uint8_t* body, size_t blen) {
    if (blen < sizeof(AckBody)) return;
    AckBody a;
    memcpy(&a, body, sizeof(a));
    s_nonce = a.cmd_nonce;
    if (s_cmd.state != CmdState::Waiting || a.cmd_id != s_cmd.body.cmd_id) return;  // late repeat
    s_cmd.state = CmdState::Acked;
    s_cmd.ack = a;
    s_cmd.finished++;
    Serial.printf("Thermo: ACK %s: %s, value %u, alarms 0x%04X, muted 0x%04X (after %u tries)\n", cmdName(a.cmd),
                  ackName(a.result), a.value, a.alarms, a.muted, s_cmd.tries);
}

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
    s_ownBootId = esp_random();
    s_stats.enabled = true;
}

void update() {
    if (s_cmd.state != CmdState::Waiting || millis() - s_cmd.lastMs < CMD_RETRY_MS) return;
    if (s_cmd.tries >= CMD_RETRIES) {
        s_cmd.state = CmdState::NoAck;
        s_cmd.finished++;
        Serial.printf("Thermo: command %s — no ACK after %u tries\n", cmdName(s_cmd.body.cmd), s_cmd.tries);
        return;
    }
    transmit();
}

bool sendCommand(uint8_t cmd, uint16_t arg) {
    CmdBody b = {};
    b.cmd_id = newCmdId();
    b.cmd_nonce = s_nonce;
    b.cmd = cmd;
    b.arg = arg;
    return start(b);
}

bool testRepeatLast() {
    if (!s_cmd.body.cmd_id) return false;
    return start(s_cmd.body);
}

bool testStaleNonce() {
    if (!s_cmd.body.cmd_id) return false;
    CmdBody b = s_cmd.body;
    b.cmd_id = newCmdId();
    return start(b);
}

const Command& command() { return s_cmd; }

const char* cmdName(uint8_t c) {
    switch (c) {
        case CMD_SET_MODE: return "SET_MODE";
        case CMD_ALARM_MUTE: return "ALARM_MUTE";
        case CMD_SENSOR_RESET: return "SENSOR_RESET";
        case CMD_HEATER: return "HEATER";
        case CMD_FAN: return "FAN";
        default: return "?";
    }
}

const char* ackName(uint8_t r) {
    switch (r) {
        case ACK_OK: return "OK";
        case ACK_NOT_SERVICE: return "NOT_SERVICE";
        case ACK_SENSOR_FAULT: return "SENSOR_FAULT";
        case ACK_TEMP_HIGH: return "TEMP_HIGH";
        case ACK_STALE_NONCE: return "STALE_NONCE";
        case ACK_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case ACK_BAD_CMD: return "BAD_CMD";
        default: return "?";
    }
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

    if (h.type == FT_ACK) {
        onAck(data + sizeof(h), n - sizeof(h));
        return;
    }
    if (h.type != FT_STATUS || n - sizeof(h) < sizeof(StatusBody)) return;
    StatusBody b;
    memcpy(&b, data + sizeof(h), sizeof(b));
    b.name[sizeof(b.name) - 1] = '\0';
    b.fw[sizeof(b.fw) - 1] = '\0';

    bool changed = !s_node.have || !sameKey(b, s_node.st);
    memcpy(s_node.mac, mac, 6);
    s_node.st = b;
    s_nonce = b.cmd_nonce;
    s_node.lastMs = millis();
    s_node.lastUtc = net::timeValid() || time(nullptr) >= UTC_VALID_MIN ? time(nullptr) : 0;
    s_node.count++;
    s_node.rssi = rssi;
    s_node.have = true;
    if (changed) logStatus(s_node);
}

const Node& node() { return s_node; }

bool online() { return s_node.have && millis() - s_node.lastMs < OFFLINE_MS; }
const Stats& stats() { return s_stats; }

void printStatus() {
    Serial.printf("Thermo: rx ok %lu, bad format %lu, bad HMAC %lu, replay %lu; tx cmd %lu, fail %lu\n",
                  (unsigned long)s_stats.rxOk, (unsigned long)s_stats.rxBadFormat, (unsigned long)s_stats.rxBadTag,
                  (unsigned long)s_stats.rxReplay, (unsigned long)s_stats.txCmd, (unsigned long)s_stats.txFail);
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

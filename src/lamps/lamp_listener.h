#pragma once
// Lamp ESP-NOW protocol on the panel: listener (stage 3) + controller heartbeat FT_CTRL_HB (stage 4),
// broadcast every 15 s while Wi-Fi is connected, so the lamps register the panel as a controller.
// No commands yet. RX callback (Wi-Fi task) only queues; update() from loop() verifies HMAC + replay
// and keeps the lamp table. Frames between lamps sent by unicast are not visible here — only broadcasts
// (heartbeats, live Test Light mode frames).

#include <stdint.h>

#include "lamp_proto.h"

namespace lamps {

constexpr int MAX_LAMPS = 6;

struct Lamp {
    bool used;
    uint8_t mac[6];
    bool haveHb;
    HbBody hb;
    bool hbHasModeClock;  // false = old firmware without mode_clock
    uint32_t lastSeenMs;  // any valid frame
    uint32_t lastHbMs;
    uint32_t hbCount;
    int8_t rssi;          // last valid frame
    bool online;          // heartbeat within ONLINE_MS
};

struct Stats {
    bool enabled;         // key derived, esp_now_init OK
    uint32_t rxOk;
    uint32_t rxBadFormat;  // too short / bad magic or version
    uint32_t rxBadTag;     // HMAC mismatch (other Wi-Fi password / foreign sender)
    uint32_t rxReplay;
    uint32_t rxDropped;    // RX queue full
    uint32_t byType[FT_COUNT];
    uint32_t txHb;         // controller heartbeats sent
    uint32_t txFail;       // esp_now_send errors
};

void begin();   // after net::begin() (WiFi.mode(STA))
void update();  // every loop() iteration

const Lamp* lamp(int i);  // nullptr if the slot is empty
const Stats& stats();
void printTable();        // Serial

const char* modeName(uint8_t mode);
const char* timeQualityName(uint8_t q);

}  // namespace lamps

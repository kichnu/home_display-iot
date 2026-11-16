#pragma once
// Thermostat node (thermo_control-iot) over ESP-NOW — status listener (stages T1, T3).
// The lamp listener owns the ESP-NOW receive callback and hands over frames with the thermostat magic
// (handleFrame); here: HMAC with the thermostat key, replay check, last status, Serial log on change.
// The GUI model is built from node() in src/app/panel_model; commands come in stages T4–T5
// (docs/PLAN_TERMOSTAT.md).

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "thermo_proto.h"

namespace thermo {

constexpr uint32_t OFFLINE_MS = 3 * STATUS_INTERVAL_MS;  // 3 missed statuses = THERMO_OFFLINE

struct Stats {
    bool enabled;       // key derived
    uint32_t rxOk;
    uint32_t rxBadFormat;
    uint32_t rxBadTag;
    uint32_t rxReplay;
};

struct Node {
    bool have;          // at least one valid status
    uint8_t mac[6];
    StatusBody st;
    uint32_t lastMs;    // last valid status
    time_t lastUtc;     // wall clock of the last status (0 while the panel has no time)
    uint32_t count;
    int8_t rssi;
};

void begin();  // derives the key; ESP-NOW itself is started by lamps::begin()
void handleFrame(const uint8_t* mac, int8_t rssi, const uint8_t* data, size_t len);  // loop() context

const Node& node();
bool online();  // status within OFFLINE_MS
const Stats& stats();
void printStatus();  // Serial

}  // namespace thermo

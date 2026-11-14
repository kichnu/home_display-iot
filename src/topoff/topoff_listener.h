#pragma once
// Top-off node (top_off_water_new-iot) over ESP-NOW — stage D1: status listener for verification.
// The lamp listener owns the ESP-NOW receive callback and hands over frames with the top-off magic
// (handleFrame); here: HMAC with the top-off key, replay check, last status, Serial log on change.
// Model, UI and commands come in stages D3–D5 (docs/PLAN_DOLEWKA.md).

#include <stddef.h>
#include <stdint.h>

#include "topoff_proto.h"

namespace topoff {

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
    uint32_t count;
    int8_t rssi;
};

void begin();  // derives the key; ESP-NOW itself is started by lamps::begin()
void handleFrame(const uint8_t* mac, int8_t rssi, const uint8_t* data, size_t len);  // loop() context

const Node& node();
const Stats& stats();
void printStatus();  // Serial

}  // namespace topoff

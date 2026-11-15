#pragma once
// Top-off node (top_off_water_new-iot) over ESP-NOW — status listener (stages D1, D3) and commands (D4).
// The lamp listener owns the ESP-NOW receive callback and hands over frames with the top-off magic
// (handleFrame); here: HMAC with the top-off key, replay check, last status, Serial log on change.
// Commands: TO_CMD by unicast with the node's current cmd_nonce, repeated every CMD_RETRY_MS (at most
// CMD_RETRIES times) with the same cmd_id until TO_ACK. One command at a time.
// The GUI model is built from node() in src/app/panel_model (docs/PLAN_DOLEWKA.md).

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "topoff_proto.h"

namespace topoff {

constexpr uint32_t OFFLINE_MS = 3 * STATUS_INTERVAL_MS;  // 3 missed statuses = TOPOFF_OFFLINE

struct Stats {
    bool enabled;       // key derived
    uint32_t rxOk;
    uint32_t rxBadFormat;
    uint32_t rxBadTag;
    uint32_t rxReplay;
    uint32_t txCmd;      // command frames sent (incl. repeats)
    uint32_t txFail;
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

enum class CmdState : uint8_t { Idle, Waiting, Acked, NoAck };

struct Command {
    CmdState state;
    CmdBody body;
    uint8_t tries;
    uint32_t lastMs;
    AckBody ack;         // with CmdState::Acked
    uint32_t finished;   // +1 every time a command ends (ACK or no ACK)
};

void begin();  // derives the key; ESP-NOW itself is started by lamps::begin()
void update(); // every loop() iteration: command repeats
void handleFrame(const uint8_t* mac, int8_t rssi, const uint8_t* data, size_t len);  // loop() context

// false: no key, node offline / unknown, or a command still waiting for its ACK
bool sendCommand(uint8_t cmd, uint16_t arg);
const Command& command();
const char* cmdName(uint8_t cmd);
const char* ackName(uint8_t result);

// Serial tests (stage D4): repeat the last command with the same cmd_id (lost ACK — node must not
// execute it again), or with a new cmd_id and the old nonce (recorded command — STALE_NONCE)
bool testRepeatLast();
bool testStaleNonce();

const Node& node();
bool online();  // status within OFFLINE_MS
const Stats& stats();
void printStatus();  // Serial

}  // namespace topoff

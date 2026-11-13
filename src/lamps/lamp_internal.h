#pragma once
// Shared between lamp_listener.cpp (radio, frame check, lamp table, catalog) and lamp_control.cpp
// (commands, program relay). Not for the rest of the firmware.

#include <stddef.h>
#include <stdint.h>

#include "lamp_listener.h"

namespace lamps {
namespace detail {

// Listener → control
bool sendFrame(const uint8_t* mac, FrameType type, const void* body, size_t bodyLen);  // HMAC-tagged
bool ensurePeer(const uint8_t* mac);  // unicast needs an ESP-NOW peer entry
const Lamp* catalogSource();          // online lamp with a heartbeat and working FRAM, or nullptr
uint32_t ownBootId();
uint32_t maxModeClock();              // highest mode session clock seen (heartbeats, FT_MODE)
void noteModeClock(uint32_t clock);

// Control ← listener (frames from a verified sender in lamp slot `slot`)
void controlOnModeAck(int slot, const uint8_t* body, size_t blen);
void controlOnAck(int slot, const uint8_t* body, size_t blen);
void controlOnProgReq(int slot, const uint8_t* mac, const uint8_t* body, size_t blen);
bool controlOnProgData(const uint8_t* mac, const uint8_t* body, size_t blen);  // false = not for the relay
void controlUpdate();

}  // namespace detail
}  // namespace lamps

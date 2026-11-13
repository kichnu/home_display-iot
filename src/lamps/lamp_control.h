#pragma once
// Lamp group commands from the panel (stage 6), same protocol as between the lamps:
// - mode (Auto / Night / Test): FT_MODE to every online lamp, new session with
//   clock = max(UTC, highest seen + 1), retried every 250 ms (8x) until FT_MODE_ACK;
//   Test starts at 50 % on all channels, like Test started in the lamp's web GUI;
// - program: FT_ACTIVATE ("set on all lamps") to every online lamp, retried every 500 ms until FT_ACK;
//   a lamp without the program answers "fetching" and requests it from the panel — the panel relays the
//   program blob from a lamp that has it (the receiving lamp checks the CRC on import).
// Offline lamps are not addressed: the panel shows the mismatch when they come back.

#include <stdint.h>

#include "lamp_listener.h"

namespace lamps {

enum class CmdKind : uint8_t { None, Mode, Program };
enum class TargetState : uint8_t { Waiting, Fetching, Done, Rejected, NoAck };

struct CmdTarget {
    int slot;  // lamp(slot)
    char name[32];
    TargetState state;
};

struct Command {
    CmdKind kind;
    bool active;        // waiting for acknowledgements
    uint8_t mode;       // CmdKind::Mode
    uint64_t programId; // CmdKind::Program
    CmdTarget t[MAX_LAMPS];
    int count;
    uint32_t finished;  // +1 every time a command ends (all targets final)
};

// false: another command in flight, radio off or no lamp online
bool sendMode(uint8_t mode);
bool sendActivate(uint64_t programId);

const Command& command();

}  // namespace lamps

#pragma once
// GUI view model. The GUI only reads it and calls Commands; it is filled by the lamp protocol adapter
// on the panel (stage 5) and by mock data in the PC simulator. Bump Model::version after every
// change the GUI should show (seconds-based texts are refreshed by the GUI itself once a second).

#include <stdint.h>
#include <time.h>

namespace gui {

// Severity: status colour of tiles / rows and alarm priority (ISA-18.2 style, P1 highest)
enum class Sev : uint8_t { Ok, P3, P2, P1, Offline };

enum class Screen : uint8_t { Overview, Lamps, Programs, Alarms };

// Same values as the lamp protocol (LampMode)
enum LampMode : uint8_t { MODE_AUTO = 0, MODE_TEST = 1, MODE_NIGHT = 2 };

constexpr uint8_t LAMP_FLAG_ORPHAN = 0x01;
constexpr uint8_t LAMP_FLAG_FRAM = 0x02;
constexpr uint8_t LAMP_FLAG_RTC = 0x04;

constexpr int MAX_LAMPS = 6;
constexpr int MAX_PROGRAMS = 24;
constexpr int MAX_TILES = 6;
constexpr int MAX_ALARMS = 16;

struct LampRow {
    char name[32];
    bool online;
    time_t lastSeen;     // last heartbeat (wall clock), 0 = never
    uint8_t mode;        // LampMode
    char program[24];
    uint64_t programId;
    uint8_t flags;       // LAMP_FLAG_*
};

enum class Pending : uint8_t { None, Mode, Program };

struct LampGroup {
    LampRow lamps[MAX_LAMPS];
    int count;
    uint8_t mode;          // group mode (majority of online lamps)
    char program[24];
    uint64_t programId;
    bool mismatch;         // online lamps differ in mode or program
    Pending pending;       // command sent, waiting for ACKs
    uint8_t pendingMode;
    uint64_t pendingProgram;
};

struct Program {
    uint64_t id;
    char name[24];
};

// Overview tile of another node (lamps have their own tile built from LampGroup)
struct NodeTile {
    char title[24];
    Sev status;
    char line1[40];
    char line2[40];
    bool hasScreen;
};

struct Alarm {
    Sev prio;            // P1..P3
    char node[24];
    char text[64];
    time_t since;
    Screen target;       // where a tap leads
};

struct Model {
    uint32_t version;
    bool wifi;
    int rssi;
    bool timeValid;
    LampGroup lamps;
    Program programs[MAX_PROGRAMS];
    int programCount;
    bool programsLoading;
    NodeTile tiles[MAX_TILES];
    int tileCount;
    Alarm alarms[MAX_ALARMS];  // sorted: highest priority first, then oldest
    int alarmCount;
};

class Commands {
public:
    virtual ~Commands() = default;
    virtual void setMode(uint8_t mode) = 0;           // all lamps online, also re-sends to align a mismatch
    virtual void activateProgram(uint64_t id) = 0;    // "set on all lamps"
    virtual void refreshPrograms() = 0;               // entering the program list
};

}  // namespace gui

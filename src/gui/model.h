#pragma once
// GUI view model. The GUI only reads it and calls Commands; it is filled by the lamp protocol adapter
// on the panel (stage 5) and by mock data in the PC simulator. Bump Model::version after every
// change the GUI should show (seconds-based texts are refreshed by the GUI itself once a second).

#include <stdint.h>
#include <time.h>

namespace gui {

// Severity: status colour of tiles / rows and alarm priority (ISA-18.2 style, P1 highest)
enum class Sev : uint8_t { Ok, P3, P2, P1, Offline };

enum class Screen : uint8_t { Overview, Lamps, Programs, Alarms, Topoff, Thermo };

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

// ---- Top-off node (docs/PLAN_DOLEWKA.md) ----

// Alarm bits, same values as the top-off protocol (topoff::AL_*)
constexpr uint16_t TOPOFF_AL_LOW_RESERVOIR = 0x0001;       // P1
constexpr uint16_t TOPOFF_AL_DAILY_LIMIT = 0x0002;         // P1
constexpr uint16_t TOPOFF_AL_RATE_ANOMALY = 0x0004;        // P1
constexpr uint16_t TOPOFF_AL_NO_TOPOFF = 0x0008;           // P1
constexpr uint16_t TOPOFF_AL_HW_FAULT = 0x0010;            // P1
constexpr uint16_t TOPOFF_AL_RESERVE_LOW = 0x0020;         // P3
constexpr uint16_t TOPOFF_AL_LEVEL_NO_READING = 0x0040;    // P3
constexpr uint16_t TOPOFF_AL_LEVEL_SENSOR_FAULT = 0x0080;  // P3
constexpr uint16_t TOPOFF_AL_UNCALIBRATED = 0x0100;        // P3
constexpr uint16_t TOPOFF_AL_RESETTABLE = 0x001F;          // latched P1 alarms — "Kasuj alarm"

// One alarm per bit (panel adapter and simulator)
struct TopoffAlarmDef {
    uint16_t bit;
    Sev prio;
    const char* text;
};
constexpr TopoffAlarmDef TOPOFF_ALARMS[] = {
    {TOPOFF_AL_LOW_RESERVOIR, Sev::P1, "zbiornik RO pusty — pompa zatrzymana"},
    {TOPOFF_AL_DAILY_LIMIT, Sev::P1, "limit dobowy osiągnięty — dolewanie zablokowane"},
    {TOPOFF_AL_RATE_ANOMALY, Sev::P1, "anomalia tempa dolewania"},
    {TOPOFF_AL_NO_TOPOFF, Sev::P1, "24 h bez dolewki"},
    {TOPOFF_AL_HW_FAULT, Sev::P1, "awaria FRAM / RTC"},
    {TOPOFF_AL_RESERVE_LOW, Sev::P3, "zbiornik RO: rezerwa"},
    {TOPOFF_AL_LEVEL_NO_READING, Sev::P3, "brak odczytu poziomu zbiornika"},
    {TOPOFF_AL_LEVEL_SENSOR_FAULT, Sev::P3, "awaria czujnika poziomu"},
    {TOPOFF_AL_UNCALIBRATED, Sev::P3, "pompa nieskalibrowana"},
};
constexpr int TOPOFF_ALARM_COUNT = sizeof(TOPOFF_ALARMS) / sizeof(TOPOFF_ALARMS[0]);

// Same values as topoff::NodeStatus
enum TopoffStatus : uint8_t { TS_OK = 0, TS_WARNING, TS_ALARM, TS_FAULT, TS_SERVICE };
// Same values as the node's AlgorithmState
enum TopoffState : uint8_t { TA_IDLE = 0, TA_DEBOUNCE, TA_PUMPING, TA_LOGGING, TA_ERROR };
// Reservoir zone (res_zone)
enum TopoffZone : uint8_t { TZ_OK = 0, TZ_RESERVE, TZ_ALARM, TZ_UNKNOWN };

constexpr int TOPOFF_PORTIONS_MAX = 10;     // one command (bargraph)
constexpr uint16_t TOPOFF_LIMIT_MIN = 100;  // daily limit, ml (node also requires >= 2 x dose)
constexpr uint16_t TOPOFF_LIMIT_MAX = 10000;
constexpr uint16_t TOPOFF_LIMIT_STEP = 50;

enum class TopoffPending : uint8_t { None, Mode, Portions, Stop, Reset, Limit };

struct TopoffView {
    bool present;            // a status was ever received
    bool online;             // status within the last 15 s
    bool controls;           // commands available (panel: top-off key derived)
    time_t lastSeen;
    char name[32];
    uint8_t status;          // TopoffStatus
    bool autoMode;           // false = Service Mode
    uint16_t serviceLeftMin; // Service Mode: minutes to the auto return
    uint8_t state;           // TopoffState
    uint16_t phaseRemainingS;
    bool pumpOn;
    bool sumpLow;
    bool manualSession;      // manual portions running (incl. pauses)
    uint8_t manualLeft;      // incl. the running one
    uint8_t manualDone;
    uint16_t volume24h;      // ml, auto top-offs
    uint16_t dailyLimit;     // ml
    uint16_t dose;           // ml per portion
    uint8_t resZone;         // TopoffZone
    uint8_t resPct;          // fill %
    uint8_t resReservePct;   // threshold markers, % of capacity
    uint8_t resAlarmPct;
    uint16_t resPortions;    // portions above the alarm level
    int16_t resHoursX10;     // −1 = unknown
    bool resEstimated;       // no ToF reading — minimum estimate
    uint16_t alarms;         // TOPOFF_AL_*
    TopoffPending pending;   // command sent, waiting for the ACK
    bool pendingAuto;
    uint8_t pendingPortions;
    uint16_t pendingLimit;
};

// ---- Thermostat node (docs/PLAN_TERMOSTAT.md) ----

// Alarm bits, same values as the thermostat protocol (thermo::AL_*). Rule: latched alarms need
// intervention and cannot be muted; TEMP_LOW / TEMP_HIGH clear by themselves and can be muted.
constexpr uint16_t THERMO_AL_TEMP_LOW = 0x0001;      // P2, buzzer, mutable
constexpr uint16_t THERMO_AL_TEMP_HIGH = 0x0002;     // P2, buzzer, mutable
constexpr uint16_t THERMO_AL_TREND = 0x0004;         // P3 warning, no buzzer
constexpr uint16_t THERMO_AL_SENSOR_FAULT = 0x0008;  // P1 latched — "Kasuj awarię czujnika"
constexpr uint16_t THERMO_AL_HW_FAULT = 0x0010;      // P1 latched — acknowledge in the node's web GUI
constexpr uint16_t THERMO_AL_MUTABLE = THERMO_AL_TEMP_LOW | THERMO_AL_TEMP_HIGH;

struct ThermoAlarmDef {
    uint16_t bit;
    Sev prio;
    const char* text;
};
constexpr ThermoAlarmDef THERMO_ALARMS[] = {
    {THERMO_AL_SENSOR_FAULT, Sev::P1, "awaria czujnika — grzałka i wentylator wyłączone"},
    {THERMO_AL_HW_FAULT, Sev::P1, "awaria FRAM / RTC — potwierdź w web GUI termostatu"},
    {THERMO_AL_TEMP_LOW, Sev::P2, "za zimno"},
    {THERMO_AL_TEMP_HIGH, Sev::P2, "za ciepło"},
    {THERMO_AL_TREND, Sev::P3, "grzałka nieskuteczna (ostrzeżenie)"},
};
constexpr int THERMO_ALARM_COUNT = sizeof(THERMO_ALARMS) / sizeof(THERMO_ALARMS[0]);

// Same values as thermo::AlgoState (node's AlgorithmState); status = TopoffStatus values (TS_*)
enum ThermoState : uint8_t { TH_IDLE = 0, TH_HEATING, TH_COOLING, TH_ALARM_LOW, TH_ALARM_HIGH, TH_SENSOR_FAULT, TH_TREND };

constexpr uint8_t THERMO_FAN_STEP = 5;          // %, ▼ / ▲ (as the node's web GUI)
constexpr uint8_t THERMO_FAN_MANUAL_PCT = 50;   // "Wentylator ON" (as the node's web GUI)

enum class ThermoPending : uint8_t { None, Mode, Mute, SensorReset, Heater, Fan };

struct ThermoView {
    bool present;            // a status was ever received
    bool online;             // status within the last 15 s
    bool controls;           // commands available (panel: thermostat key derived)
    time_t lastSeen;
    char name[32];
    uint8_t status;          // TopoffStatus values (TS_*)
    bool autoMode;           // false = Service Mode
    uint16_t serviceLeftS;   // Service Mode: seconds to the auto return
    uint8_t state;           // ThermoState
    bool tempValid;
    int16_t tempX100;        // °C × 100
    bool heaterOn;
    uint8_t fanPct;          // 0–100
    bool inBuffer;           // temp >= target, fan not yet on (yellow)
    uint16_t alarms;         // THERMO_AL_*
    uint16_t muted;          // THERMO_AL_* silenced (TEMP_LOW / TEMP_HIGH only)
    ThermoPending pending;   // command sent, waiting for the ACK
    bool pendingAuto;
    bool pendingHeater;
    uint8_t pendingFan;
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
    Screen target;       // with hasScreen
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
    TopoffView topoff;
    ThermoView thermo;
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
    // Top-off node: every command is validated by the node, the answer comes back in the status/ACK
    virtual void topoffSetAuto(bool on) = 0;           // Auto / Service Mode, no confirmation
    virtual void topoffPortions(uint8_t n) = 0;        // manual portions (Service Mode), 1..10
    virtual void topoffStop() = 0;                     // stop the manual session
    virtual void topoffReset() = 0;                    // clear latched alarms (after confirmation)
    virtual void topoffSetLimit(uint16_t ml) = 0;      // daily limit (after confirmation)
    // Thermostat node: Service Mode for the manual heater / fan, validated by the node
    virtual void thermoSetAuto(bool on) = 0;           // Auto / Service Mode, no confirmation
    virtual void thermoMute() = 0;                     // mute TEMP_LOW / TEMP_HIGH (current episode)
    virtual void thermoSensorReset() = 0;              // clear the sensor fault latch (after confirmation)
    virtual void thermoHeater(bool on) = 0;            // manual heater (Service Mode)
    virtual void thermoFan(uint8_t pct) = 0;           // manual fan 0–100 % (Service Mode)
};

}  // namespace gui

#pragma once
// Wire format of the lamps' ESP-NOW protocol (led_lamp_aquarium-iot, src/network/espnow_sync.cpp,
// FRAME_VERSION 1). Copied, not shared: keep in sync with the lamp firmware. Sizes are checked below.
//
// Frame = FrameHdr + body + HMAC-SHA256 truncated to 16 B over (hdr + body).
// Group key = SHA-256(KEY_CONTEXT + Wi-Fi password).

#include <stddef.h>
#include <stdint.h>

namespace lamps {

constexpr uint16_t FRAME_MAGIC = 0x4E52;  // "RN"
constexpr uint8_t FRAME_VERSION = 1;
constexpr size_t FRAME_MAX = 250;  // ESP-NOW v1
constexpr size_t TAG_LEN = 16;
constexpr const char* KEY_CONTEXT = "RL90-ESPNOW-v1:";

constexpr uint32_t HB_INTERVAL_MS = 15000;
constexpr uint32_t ONLINE_MS = 45000;  // 3 heartbeats, same as the lamps

constexpr size_t PROGRAM_NAME_LEN = 24;  // with '\0'
constexpr size_t NUM_CHANNELS = 4;

enum FrameType : uint8_t {
    FT_HB = 1,
    FT_LIST_REQ,
    FT_LIST,
    FT_PROG_REQ,
    FT_PROG_DATA,
    FT_ACTIVATE,
    FT_ACK,
    FT_MODE,
    FT_MODE_ACK,
    FT_CTRL_HB,  // controller (this panel) heartbeat — lamp FW >= 0.3.0
    FT_COUNT     // number of known types + 1
};

enum LampMode : uint8_t { MODE_PROGRAM = 0, MODE_TEST, MODE_NIGHT };

// HbBody.time_q
enum TimeQuality : uint8_t { TQ_NONE = 0, TQ_RTC = 1, TQ_NTP = 2 };

// HbBody.flags
constexpr uint8_t HB_FLAG_ORPHAN = 0x01;  // active program deleted elsewhere
constexpr uint8_t HB_FLAG_FRAM_FAIL = 0x02;  // lamp runs from RAM (lamp FW >= 0.3.0)
constexpr uint8_t HB_FLAG_RTC_FAIL = 0x04;   // DS3231 missing or without valid time (lamp FW >= 0.3.0)

#pragma pack(push, 1)
struct FrameHdr {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t boot_id;  // random at every boot of the sender
    uint32_t seq;      // increases within boot_id
};

struct HbBody {
    char name[32];
    uint32_t ip;
    uint64_t active_id;
    char active_name[PROGRAM_NAME_LEN];
    uint32_t catalog_hash;
    uint32_t utc;  // 0 = no valid time
    uint32_t programs_created;
    uint8_t prog_count;
    uint8_t mode;  // LampMode
    uint8_t time_q;
    uint8_t flags;
    char fw[12];
    uint32_t mode_clock;  // missing in old firmware (shorter frame)
};

struct CtrlHbBody {
    char name[32];
    char fw[12];
    uint32_t ip;
};

struct ModeBody {
    uint32_t clock;
    uint32_t seq;
    uint8_t mode;
    uint8_t ack;
    uint16_t v[NUM_CHANNELS];  // Test Light values, hundredths of %
};
#pragma pack(pop)

static_assert(sizeof(FrameHdr) == 12, "FrameHdr layout");
static_assert(sizeof(HbBody) == 100, "HbBody layout");
static_assert(offsetof(HbBody, mode_clock) == 96, "HbBody layout");
static_assert(sizeof(ModeBody) == 18, "ModeBody layout");
static_assert(sizeof(CtrlHbBody) == 48, "CtrlHbBody layout (lamp copy checks the same)");
static_assert(FT_CTRL_HB == 10, "frame type numbering must match the lamps");

}  // namespace lamps

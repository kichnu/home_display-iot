#include "lamp_control.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

#include "../net/network.h"
#include "lamp_internal.h"

namespace lamps {

namespace {

// Same timing as the lamps (espnow_sync.cpp)
constexpr uint32_t MODE_RETRY_MS = 250;
constexpr int MODE_TRIES = 8;
constexpr uint32_t ACT_RETRY_MS = 500;
constexpr int ACT_TRIES = 6;
constexpr uint32_t FETCH_WAIT_MS = 15000;  // lamp answers "fetching": wait this long for the import
constexpr uint16_t TEST_V = 5000;          // 50 %, the lamps' TEST_START_V

// FT_ACK status (lamp AckStatus)
constexpr uint8_t ACK_ACTIVATED = 0;
constexpr uint8_t ACK_FETCHING = 1;
constexpr uint8_t ACK_REJECTED = 2;

// Program relay
constexpr size_t BLOB_MAX = 1024;  // lamp PROGRAM_BLOB_MAX
constexpr uint32_t CHUNK_TIMEOUT_MS = 400;
constexpr int CHUNK_TRIES = 5;

struct TargetTx {
    uint8_t mac[6];
    uint32_t lastMs;
    uint32_t fetchSinceMs;
    int tries;
};

Command s_cmd = {};
TargetTx s_tx[MAX_LAMPS];
ModeBody s_modeBody;
uint32_t s_cmdId = 0;
uint32_t s_cmdCounter = 0;

struct Blob {
    bool active;    // fetching
    bool complete;
    uint64_t id;
    uint8_t src[6];
    uint16_t total;
    uint8_t chunks;
    uint8_t next;
    uint32_t reqMs;
    int tries;
    uint8_t data[BLOB_MAX];
} s_blob = {};

bool isFinal(TargetState s) { return s == TargetState::Done || s == TargetState::Rejected || s == TargetState::NoAck; }

void finishIfDone() {
    if (!s_cmd.active) return;
    for (int i = 0; i < s_cmd.count; i++)
        if (!isFinal(s_cmd.t[i].state)) return;
    s_cmd.active = false;
    s_cmd.finished++;
    int done = 0;
    for (int i = 0; i < s_cmd.count; i++) done += s_cmd.t[i].state == TargetState::Done;
    Serial.printf("Lamps: command %s finished, %d/%d acknowledged\n", s_cmd.kind == CmdKind::Mode ? "mode" : "program",
                  done, s_cmd.count);
}

int targetOf(int slot) {
    if (!s_cmd.active || slot < 0) return -1;
    for (int i = 0; i < s_cmd.count; i++)
        if (s_cmd.t[i].slot == slot) return i;
    return -1;
}

// Online lamps with a heartbeat become the targets
bool startCommand(CmdKind kind) {
    if (s_cmd.active || !stats().enabled || WiFi.status() != WL_CONNECTED) return false;
    Command c = {};
    c.kind = kind;
    c.finished = s_cmd.finished;
    for (int i = 0; i < MAX_LAMPS; i++) {
        const Lamp* l = lamp(i);
        if (!l || !l->haveHb || !l->online || !detail::ensurePeer(l->mac)) continue;
        CmdTarget& t = c.t[c.count];
        t.slot = i;
        snprintf(t.name, sizeof(t.name), "%s", l->hb.name);
        t.state = TargetState::Waiting;
        memcpy(s_tx[c.count].mac, l->mac, 6);
        s_tx[c.count].tries = 0;
        s_tx[c.count].lastMs = 0;
        c.count++;
    }
    if (c.count == 0) return false;
    c.active = true;
    s_cmd = c;
    return true;
}

void sendModeTo(int i) {
    detail::sendFrame(s_tx[i].mac, FT_MODE, &s_modeBody, sizeof(s_modeBody));
    s_tx[i].lastMs = millis();
    s_tx[i].tries++;
}

void sendActivateTo(int i) {
    struct __attribute__((packed)) {
        uint64_t id;
        uint32_t cmd_id;
    } b = {s_cmd.programId, s_cmdId};
    static_assert(sizeof(b) == 12, "ActivateBody layout");
    detail::sendFrame(s_tx[i].mac, FT_ACTIVATE, &b, sizeof(b));
    s_tx[i].lastMs = millis();
    s_tx[i].tries++;
}

// ---- Program relay ----

void blobRequest() {
    ProgReqBody b = {s_blob.id, s_blob.next};
    detail::sendFrame(s_blob.src, FT_PROG_REQ, &b, sizeof(b));
    s_blob.reqMs = millis();
}

// Start reading the program from a lamp that has it (the catalog source), not from `exceptMac`
void blobFetch(uint64_t id, const uint8_t* exceptMac) {
    if ((s_blob.complete || s_blob.active) && s_blob.id == id) return;
    const Lamp* src = detail::catalogSource();
    if (src && exceptMac && memcmp(src->mac, exceptMac, 6) == 0) {
        src = nullptr;
        for (int i = 0; i < MAX_LAMPS; i++) {  // another lamp with working FRAM
            const Lamp* l = lamp(i);
            if (l && l->online && l->haveHb && !(l->hb.flags & HB_FLAG_FRAM_FAIL) && memcmp(l->mac, exceptMac, 6) != 0) {
                src = l;
                break;
            }
        }
    }
    if (!src || !detail::ensurePeer(src->mac)) return;
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.active = true;
    s_blob.id = id;
    memcpy(s_blob.src, src->mac, 6);
    blobRequest();
}

bool inCatalog(uint64_t id) {
    const Catalog& c = catalog();
    for (int i = 0; i < c.count; i++)
        if (c.e[i].id == id) return true;
    return false;
}

}  // namespace

bool sendMode(uint8_t mode) {
    if (mode > MODE_NIGHT || !startCommand(CmdKind::Mode)) return false;
    uint32_t clock = detail::maxModeClock() + 1;
    if (net::timeValid() && (uint32_t)time(nullptr) > clock) clock = (uint32_t)time(nullptr);
    detail::noteModeClock(clock);
    memset(&s_modeBody, 0, sizeof(s_modeBody));
    s_modeBody.clock = clock;
    s_modeBody.seq = 0;
    s_modeBody.mode = mode;
    s_modeBody.ack = 1;
    for (auto& v : s_modeBody.v) v = TEST_V;  // used by the lamps only in Test
    s_cmd.mode = mode;
    for (int i = 0; i < s_cmd.count; i++) sendModeTo(i);  // all now: lamps switch together
    Serial.printf("Lamps: mode %s sent to %d lamps (clock %lu)\n", modeName(mode), s_cmd.count, (unsigned long)clock);
    return true;
}

bool sendActivate(uint64_t programId) {
    if (programId == 0 || !startCommand(CmdKind::Program)) return false;
    s_cmd.programId = programId;
    s_cmdId = (++s_cmdCounter) ^ (detail::ownBootId() & 0xFFFF0000);
    blobFetch(programId, nullptr);  // ready in case a lamp has to fetch it from us
    for (int i = 0; i < s_cmd.count; i++) sendActivateTo(i);
    Serial.printf("Lamps: program %08lX%08lX sent to %d lamps\n", (unsigned long)(programId >> 32),
                  (unsigned long)programId, s_cmd.count);
    return true;
}

const Command& command() { return s_cmd; }

namespace detail {

void controlOnModeAck(int slot, const uint8_t* body, size_t blen) {
    int i = targetOf(slot);
    if (i < 0 || s_cmd.kind != CmdKind::Mode || blen < 8) return;
    uint32_t clock, seq;
    memcpy(&clock, body, 4);
    memcpy(&seq, body + 4, 4);
    if (clock != s_modeBody.clock || seq != s_modeBody.seq) return;
    s_cmd.t[i].state = TargetState::Done;
    finishIfDone();
}

void controlOnAck(int slot, const uint8_t* body, size_t blen) {
    int i = targetOf(slot);
    if (i < 0 || s_cmd.kind != CmdKind::Program || blen < 5) return;
    uint32_t cmdId;
    memcpy(&cmdId, body, 4);
    if (cmdId != s_cmdId) return;
    CmdTarget& t = s_cmd.t[i];
    switch (body[4]) {
        case ACK_ACTIVATED: t.state = TargetState::Done; break;
        case ACK_REJECTED: t.state = TargetState::Rejected; break;
        case ACK_FETCHING:
            if (t.state != TargetState::Fetching) {
                t.state = TargetState::Fetching;
                s_tx[i].fetchSinceMs = millis();
                Serial.printf("Lamps: %s fetches the program\n", t.name);
            }
            break;
        default: return;
    }
    finishIfDone();
}

void controlOnProgReq(int slot, const uint8_t* mac, const uint8_t* body, size_t blen) {
    (void)slot;
    if (blen < sizeof(ProgReqBody)) return;
    ProgReqBody r;
    memcpy(&r, body, sizeof(r));
    if (s_blob.complete && s_blob.id == r.id) {
        ProgDataBody b = {};
        b.id = r.id;
        b.total = s_blob.total;
        b.chunks = s_blob.chunks;
        if (r.chunk < s_blob.chunks) {
            uint16_t off = r.chunk * CHUNK_DATA;
            b.chunk = r.chunk;
            b.len = (s_blob.total - off > CHUNK_DATA) ? CHUNK_DATA : s_blob.total - off;
            memcpy(b.data, s_blob.data + off, b.len);
        } else {
            b.total = 0;
        }
        detail::ensurePeer(mac);
        detail::sendFrame(mac, FT_PROG_DATA, &b, offsetof(ProgDataBody, data) + b.len);
        return;
    }
    if (!inCatalog(r.id)) {  // unknown to the group: "no such program"
        ProgDataBody b = {};
        b.id = r.id;
        detail::ensurePeer(mac);
        detail::sendFrame(mac, FT_PROG_DATA, &b, offsetof(ProgDataBody, data));
        return;
    }
    blobFetch(r.id, mac);  // no answer now: the lamp retries the chunk in 400 ms
}

bool controlOnProgData(const uint8_t* mac, const uint8_t* body, size_t blen) {
    if (!s_blob.active || memcmp(mac, s_blob.src, 6) != 0 || blen < offsetof(ProgDataBody, data)) return false;
    ProgDataBody b = {};
    memcpy(&b, body, blen < sizeof(b) ? blen : sizeof(b));
    if (b.id != s_blob.id || b.chunk != s_blob.next) return false;
    if (b.total == 0 || b.total > BLOB_MAX || b.chunks != (b.total + CHUNK_DATA - 1) / CHUNK_DATA ||
        (s_blob.next > 0 && b.total != s_blob.total)) {
        Serial.println("Lamps: program relay — source has no such program");
        s_blob.active = false;
        return true;
    }
    uint16_t off = b.chunk * CHUNK_DATA;
    uint16_t expect = (b.total - off > CHUNK_DATA) ? CHUNK_DATA : b.total - off;
    if (b.len != expect || blen < offsetof(ProgDataBody, data) + b.len) return true;
    s_blob.total = b.total;
    s_blob.chunks = b.chunks;
    memcpy(s_blob.data + off, b.data, b.len);
    s_blob.tries = 0;
    if (++s_blob.next >= s_blob.chunks) {
        s_blob.active = false;
        s_blob.complete = true;
        Serial.printf("Lamps: program relay ready (%u B)\n", s_blob.total);
    } else {
        blobRequest();
    }
    return true;
}

void controlUpdate() {
    uint32_t now = millis();
    if (s_blob.active && now - s_blob.reqMs >= CHUNK_TIMEOUT_MS) {
        if (++s_blob.tries > CHUNK_TRIES) {
            Serial.println("Lamps: program relay — source timeout");
            s_blob.active = false;
        } else {
            blobRequest();
        }
    }

    if (!s_cmd.active) return;
    for (int i = 0; i < s_cmd.count; i++) {
        CmdTarget& t = s_cmd.t[i];
        TargetTx& tx = s_tx[i];
        if (isFinal(t.state)) continue;
        if (s_cmd.kind == CmdKind::Mode) {
            if (now - tx.lastMs < MODE_RETRY_MS) continue;
            if (tx.tries >= MODE_TRIES) t.state = TargetState::NoAck;
            else sendModeTo(i);
        } else {
            if (now - tx.lastMs < ACT_RETRY_MS) continue;
            bool giveUp = t.state == TargetState::Fetching ? now - tx.fetchSinceMs >= FETCH_WAIT_MS : tx.tries >= ACT_TRIES;
            if (giveUp) t.state = TargetState::NoAck;
            else sendActivateTo(i);  // same cmd_id: the lamp answers again (fetching → activated)
        }
        if (t.state == TargetState::NoAck) Serial.printf("Lamps: no acknowledgement from %s\n", t.name);
    }
    finishIfDone();
}

}  // namespace detail

}  // namespace lamps

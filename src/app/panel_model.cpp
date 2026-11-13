#include "panel_model.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "gui/gui.h"
#include "lamps/lamp_control.h"
#include "lamps/lamp_listener.h"
#include "net/network.h"
#include "version.h"

using namespace gui;

namespace app {

namespace {

constexpr uint32_t REBUILD_MS = 500;
constexpr uint32_t WIFI_GRACE_MS = 30000;   // first connection after boot is not an alarm
constexpr uint32_t LAMPS_GRACE_MS = 60000;  // heartbeats every 15 s: no lamp after 60 s = alarm
constexpr uint32_t TIME_GRACE_MS = 120000;
// After the last ACK the GUI stays "pending" until the heartbeats show the new state (no mismatch
// flash in between), at most this long
constexpr uint32_t CONFIRM_MS = 4000;

static_assert(gui::MAX_LAMPS == lamps::MAX_LAMPS, "lamp slots map 1:1");
static_assert((int)gui::MODE_AUTO == (int)lamps::MODE_PROGRAM && (int)gui::MODE_TEST == (int)lamps::MODE_TEST &&
                  (int)gui::MODE_NIGHT == (int)lamps::MODE_NIGHT,
              "GUI mode values = protocol values");
static_assert(LAMP_FLAG_ORPHAN == lamps::HB_FLAG_ORPHAN && LAMP_FLAG_FRAM == lamps::HB_FLAG_FRAM_FAIL &&
                  LAMP_FLAG_RTC == lamps::HB_FLAG_RTC_FAIL,
              "GUI lamp flags = heartbeat flags");

// Alarm "since" per condition: per lamp slot, and for the group / panel
enum LampAlarm { LA_OFFLINE, LA_FRAM, LA_RTC, LA_ORPHAN, LA_COUNT };
enum PanelAlarm { PA_MISMATCH, PA_NO_LAMPS, PA_WIFI, PA_TIME, PA_COUNT };
time_t s_lampSince[lamps::MAX_LAMPS][LA_COUNT];
time_t s_panelSince[PA_COUNT];

Model s_model = {};
Model s_next = {};
uint32_t s_lastBuildMs = 0;

void copy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src); }

// Stable sort that moves whole elements with memcpy: padding bytes stay as memset left them, so
// memcmp() of two models only sees real changes (std::sort assigns members, padding undefined).
template <typename T, int N, typename Less>
void sortBytes(T (&a)[N], int n, Less less) {
    T tmp[N];
    int idx[N];
    memcpy(tmp, a, n * sizeof(T));
    for (int i = 0; i < n; i++) idx[i] = i;
    std::stable_sort(idx, idx + n, [&](int x, int y) { return less(tmp[x], tmp[y]); });
    for (int i = 0; i < n; i++) memcpy(&a[i], &tmp[idx[i]], sizeof(T));
}

// Command in flight (lamp_control) + confirmation by heartbeats, then one toast with the result
struct Tracker {
    bool on;
    uint32_t seenFinished;
    uint32_t ackedMs;  // all targets final; 0 = still waiting for ACKs
} s_track = {};

const char* programName(uint64_t id) {
    const lamps::Catalog& c = lamps::catalog();
    for (int i = 0; i < c.count; i++)
        if (c.e[i].id == id && c.e[i].named) return c.e[i].name;
    return "?";
}

const char* modeLabel(uint8_t mode) { return lamps::modeName(mode); }

class PanelCommands : public Commands {
public:
    void setMode(uint8_t mode) override {
        if (!lamps::sendMode(mode)) return refused();
        s_track = {true, lamps::command().finished, 0};
        s_lastBuildMs = 0;  // show "pending" at once
    }
    void activateProgram(uint64_t id) override {
        if (!lamps::sendActivate(id)) return refused();
        s_track = {true, lamps::command().finished, 0};
        s_lastBuildMs = 0;
    }
    void refreshPrograms() override {}  // catalog follows catalog_hash by itself

private:
    static void refused() {
        toast(net::connected() ? "Nie wysłano: brak lamp online lub poprzednie polecenie w toku" : "Nie wysłano: brak Wi-Fi",
              Sev::P2);
    }
};
PanelCommands s_commands;

void fillLamps(Model& m) {
    LampGroup& lg = m.lamps;
    for (int i = 0; i < lamps::MAX_LAMPS; i++) {
        const lamps::Lamp* l = lamps::lamp(i);
        if (!l || !l->haveHb) continue;
        LampRow& r = lg.lamps[lg.count++];
        copy(r.name, sizeof(r.name), l->hb.name);
        r.online = l->online;
        r.lastSeen = l->lastHbUtc;
        r.mode = l->hb.mode;
        copy(r.program, sizeof(r.program), l->hb.active_name);
        r.programId = l->hb.active_id;
        r.flags = l->hb.flags & (LAMP_FLAG_ORPHAN | LAMP_FLAG_FRAM | LAMP_FLAG_RTC);
    }

    // Stable order on screen: by name (the listener's slots depend on who was heard first)
    sortBytes(lg.lamps, lg.count, [](const LampRow& a, const LampRow& b) { return strcmp(a.name, b.name) < 0; });

    // Group mode / program: the most common among online lamps (first by name on a tie)
    int best = -1, bestVotes = 0;
    for (int i = 0; i < lg.count; i++) {
        if (!lg.lamps[i].online) continue;
        int votes = 0;
        for (int j = 0; j < lg.count; j++)
            if (lg.lamps[j].online && lg.lamps[j].mode == lg.lamps[i].mode &&
                lg.lamps[j].programId == lg.lamps[i].programId)
                votes++;
        if (votes > bestVotes) {
            best = i;
            bestVotes = votes;
        }
    }
    if (best >= 0) {
        lg.mode = lg.lamps[best].mode;
        lg.programId = lg.lamps[best].programId;
        copy(lg.program, sizeof(lg.program), lg.lamps[best].program);
        for (int i = 0; i < lg.count; i++) {
            const LampRow& r = lg.lamps[i];
            if (r.online && (r.mode != lg.mode || r.programId != lg.programId)) lg.mismatch = true;
        }
    }
    lg.pending = Pending::None;
    if (s_track.on) {
        const lamps::Command& c = lamps::command();
        lg.pending = c.kind == lamps::CmdKind::Mode ? Pending::Mode : Pending::Program;
        lg.pendingMode = c.mode;
        lg.pendingProgram = c.programId;
    }
}

void fillPrograms(Model& m) {
    const lamps::Catalog& c = lamps::catalog();
    for (int i = 0; i < c.count && m.programCount < MAX_PROGRAMS; i++) {
        if (!c.e[i].named) continue;
        Program& p = m.programs[m.programCount++];
        p.id = c.e[i].id;
        copy(p.name, sizeof(p.name), c.e[i].name);
    }
    // Alphabetical, like the lamps' GUI
    sortBytes(m.programs, m.programCount, [](const Program& a, const Program& b) { return strcmp(a.name, b.name) < 0; });
    m.programsLoading = m.programCount == 0 && (c.busy || !c.valid);
}

void fillTiles(Model& m) {
    NodeTile& t = m.tiles[m.tileCount++];
    copy(t.title, sizeof(t.title), "Panel");
    t.status = m.wifi ? Sev::Ok : Sev::P2;
    if (m.wifi) snprintf(t.line1, sizeof(t.line1), "Wi-Fi %d dBm", m.rssi);
    else copy(t.line1, sizeof(t.line1), "brak Wi-Fi");
    snprintf(t.line2, sizeof(t.line2), "FW %s%s", PANEL_FW_VERSION, m.timeValid ? "" : " · brak czasu");
    t.hasScreen = false;
}

void fillAlarms(Model& m) {
    time_t now = time(nullptr);
    uint32_t up = millis();
    auto add = [&](bool on, time_t& since, Sev prio, const char* node, const char* text, Screen target) {
        if (!on) {
            since = 0;
            return;
        }
        if (!since) since = now;
        if (m.alarmCount >= MAX_ALARMS) return;
        Alarm& a = m.alarms[m.alarmCount++];
        a.prio = prio;
        copy(a.node, sizeof(a.node), node);
        copy(a.text, sizeof(a.text), text);
        a.since = since;
        a.target = target;
    };

    int lampsSeen = 0;
    for (int i = 0; i < lamps::MAX_LAMPS; i++) {
        const lamps::Lamp* l = lamps::lamp(i);
        bool have = l && l->haveHb;
        if (have) lampsSeen++;
        const char* name = have ? l->hb.name : "";
        uint8_t f = have ? l->hb.flags : 0;
        add(have && !l->online, s_lampSince[i][LA_OFFLINE], Sev::P2, name, "brak łączności", Screen::Lamps);
        add(have && (f & lamps::HB_FLAG_FRAM_FAIL), s_lampSince[i][LA_FRAM], Sev::P2, name,
            "awaria FRAM — praca z RAM", Screen::Lamps);
        add(have && (f & lamps::HB_FLAG_RTC_FAIL), s_lampSince[i][LA_RTC], Sev::P3, name, "awaria zegara RTC",
            Screen::Lamps);
        add(have && (f & lamps::HB_FLAG_ORPHAN), s_lampSince[i][LA_ORPHAN], Sev::P3, name,
            "aktywny program usunięty w innej lampie", Screen::Lamps);
    }
    add(m.lamps.mismatch && m.lamps.pending == Pending::None, s_panelSince[PA_MISMATCH], Sev::P3, "Lampy", "rozjazd trybu lub programu", Screen::Lamps);
    add(lampsSeen == 0 && up > LAMPS_GRACE_MS && lamps::stats().enabled, s_panelSince[PA_NO_LAMPS], Sev::P2, "Lampy",
        "brak heartbeatów lamp", Screen::Lamps);
    add(!m.wifi && up > WIFI_GRACE_MS, s_panelSince[PA_WIFI], Sev::P2, "Panel", "brak Wi-Fi", Screen::Overview);
    add(!m.timeValid && up > TIME_GRACE_MS, s_panelSince[PA_TIME], Sev::P3, "Panel", "brak czasu (NTP)",
        Screen::Overview);

    sortBytes(m.alarms, m.alarmCount, [](const Alarm& a, const Alarm& b) {
        if (a.prio != b.prio) return (uint8_t)a.prio > (uint8_t)b.prio;
        return a.since < b.since;
    });
}

// Heartbeats of the lamps that acknowledged show the commanded state
bool confirmedByHeartbeats(const lamps::Command& c) {
    for (int i = 0; i < c.count; i++) {
        if (c.t[i].state != lamps::TargetState::Done) continue;
        const lamps::Lamp* l = lamps::lamp(c.t[i].slot);
        if (!l || !l->haveHb) continue;
        if (c.kind == lamps::CmdKind::Mode ? l->hb.mode != c.mode : l->hb.active_id != c.programId) return false;
    }
    return true;
}

void resultToast(const lamps::Command& c) {
    char names[96] = "";
    int done = 0, rejected = 0, noAck = 0;
    for (int i = 0; i < c.count; i++) {
        const lamps::CmdTarget& t = c.t[i];
        if (t.state == lamps::TargetState::Done) {
            done++;
            continue;
        }
        if (t.state == lamps::TargetState::Rejected) rejected++;
        else noAck++;
        size_t n = strlen(names);
        snprintf(names + n, sizeof(names) - n, "%s%s", n ? ", " : "", t.name);
    }
    char msg[160];
    if (rejected) {
        snprintf(msg, sizeof(msg), "Program odrzucony (usunięty w GUI lampy): %s", names);
        toast(msg, Sev::P2);
    } else if (noAck) {
        snprintf(msg, sizeof(msg), "Brak potwierdzenia od: %s", names);
        toast(msg, Sev::P2);
    } else if (c.kind == lamps::CmdKind::Mode) {
        snprintf(msg, sizeof(msg), "Tryb %s ustawiony (lampy: %d)", modeLabel(c.mode), done);
        toast(msg);
    } else {
        snprintf(msg, sizeof(msg), "Program „%s” ustawiony (lampy: %d)", programName(c.programId), done);
        toast(msg);
    }
}

void trackCommand() {
    if (!s_track.on) return;
    const lamps::Command& c = lamps::command();
    if (c.active || c.finished == s_track.seenFinished) return;
    if (!s_track.ackedMs) s_track.ackedMs = millis() | 1;
    if (!confirmedByHeartbeats(c) && millis() - s_track.ackedMs < CONFIRM_MS) return;
    s_track.on = false;
    s_lastBuildMs = 0;
    resultToast(c);
}

void build(Model& m) {
    memset(&m, 0, sizeof(m));
    m.wifi = net::connected();
    m.rssi = m.wifi ? net::rssi() : 0;
    m.timeValid = net::timeValid();
    fillLamps(m);
    fillPrograms(m);
    fillTiles(m);
    fillAlarms(m);
}

}  // namespace

void update() {
    trackCommand();
    if (s_lastBuildMs && millis() - s_lastBuildMs < REBUILD_MS) return;
    s_lastBuildMs = millis();
    build(s_next);
    s_next.version = s_model.version;
    // Version changes only when something visible changed (both copies are memset first: padding equal)
    if (memcmp(&s_next, &s_model, sizeof(Model)) != 0) {
        uint32_t v = s_model.version + 1;
        memcpy(&s_model, &s_next, sizeof(Model));
        s_model.version = v;
    }
}

Model& model() { return s_model; }

Commands& commands() { return s_commands; }

}  // namespace app

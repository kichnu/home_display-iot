#include "mock.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "gui/gui.h"

using namespace gui;

namespace {
struct ProgDef {
    uint64_t id;
    const char* name;
};
const ProgDef PROGRAMS[] = {
    {0x1001, "Rafa dzień"},   {0x1002, "Rafa zima"},     {0x1003, "Aklimatyzacja"},
    {0x524C393046414354ULL, "Fabryczny"}, {0x1005, "Wieczór długi"}, {0x1006, "Test bieli"},
    {0x1007, "Program 0007"}, {0x1008, "Lato 2026"},     {0x1009, "Zachód 20:00"},
};
constexpr int PROGRAM_COUNT = sizeof(PROGRAMS) / sizeof(PROGRAMS[0]);
constexpr uint32_t CMD_DELAY_MS = 900;
constexpr uint32_t PROGRAMS_SLOW_MS = 2000;

// Alarm slots (since_ index): first time a condition was seen
enum { A_OFF0, A_OFF1, A_FRAM0, A_RTC1, A_MISMATCH, A_DOSING, A_TOPOFF, A_WIFI };

void copy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src); }
}  // namespace

void Mock::help() {
    printf(
        "\nGUI simulator — keys (focus on the window):\n"
        "  1  Lampa czarna offline / online       2  Lampa biała: awaria FRAM\n"
        "  3  Lampa czarna: awaria RTC            4  rozjazd: Lampa czarna w trybie Noc\n"
        "  5  Dozownik: alarm P1                  6  Dolewka: ostrzeżenie P3\n"
        "  7  następne polecenie bez ACK          8  Wi-Fi panelu wył. / wł.\n"
        "  9  czas nieważny / ważny               0  wolne pobieranie listy programów\n"
        "  s  wygaszacz teraz                     h  ta pomoc\n"
        "Screensaver also after 60 s without mouse input.\n\n");
}

void Mock::init() {
    Model& m = model;
    m.wifi = true;
    m.rssi = -59;
    m.timeValid = true;
    LampGroup& lg = m.lamps;
    lg.count = 2;
    time_t now = time(nullptr);
    const char* names[2] = {"Lampa biała", "Lampa czarna"};
    for (int i = 0; i < 2; i++) {
        LampRow& l = lg.lamps[i];
        copy(l.name, sizeof(l.name), names[i]);
        l.online = true;
        l.lastSeen = now;
        l.mode = MODE_AUTO;
        l.programId = PROGRAMS[0].id;
        copy(l.program, sizeof(l.program), PROGRAMS[0].name);
    }

    m.tileCount = 5;
    NodeTile* t = m.tiles;
    t[0] = {"Dolewka", Sev::Ok, "poziom OK", "dziś 1,2 l", false};
    t[1] = {"Dozownik", Sev::Ok, "4 kanały OK", "następna dawka 18:00", false};
    t[2] = {"Termostat", Sev::Ok, "25,4 °C", "grzałka wył.", false};
    t[3] = {"Timer", Sev::Ok, "3/4 włączone", "CO₂ do 20:00", false};
    t[4] = {"Panel", Sev::Ok, "Wi-Fi -59 dBm", "FW 0.4.0", false};
    fillPrograms();
    derive();
}

const char* Mock::programName(uint64_t id) const {
    for (const auto& p : PROGRAMS)
        if (p.id == id) return p.name;
    return "?";
}

void Mock::fillPrograms() {
    model.programCount = PROGRAM_COUNT;
    for (int i = 0; i < PROGRAM_COUNT; i++) {
        model.programs[i].id = PROGRAMS[i].id;
        copy(model.programs[i].name, sizeof(model.programs[i].name), PROGRAMS[i].name);
    }
    // Alphabetical, like the lamps' GUI
    std::sort(model.programs, model.programs + PROGRAM_COUNT,
              [](const Program& a, const Program& b) { return strcmp(a.name, b.name) < 0; });
    model.programsLoading = false;
}

void Mock::derive() {
    Model& m = model;
    LampGroup& lg = m.lamps;
    time_t now = time(nullptr);
    for (int i = 0; i < 2; i++) lg.lamps[i].online = !offline_[i];

    // Group mode / program: first online lamp (majority with two lamps is the same thing)
    int ref = -1;
    for (int i = 0; i < lg.count; i++)
        if (lg.lamps[i].online) {
            ref = i;
            break;
        }
    lg.mismatch = false;
    if (ref >= 0) {
        lg.mode = lg.lamps[ref].mode;
        lg.programId = lg.lamps[ref].programId;
        copy(lg.program, sizeof(lg.program), lg.lamps[ref].program);
        for (int i = 0; i < lg.count; i++) {
            const LampRow& l = lg.lamps[i];
            if (l.online && (l.mode != lg.mode || l.programId != lg.programId)) lg.mismatch = true;
        }
    }

    // Alarms: (condition, slot, prio, node, text, target)
    m.alarmCount = 0;
    auto add = [&](bool on, int slot, Sev prio, const char* node, const char* text, Screen target) {
        if (!on) {
            since_[slot] = 0;
            return;
        }
        if (!since_[slot]) since_[slot] = now;
        Alarm& a = m.alarms[m.alarmCount++];
        a.prio = prio;
        copy(a.node, sizeof(a.node), node);
        copy(a.text, sizeof(a.text), text);
        a.since = since_[slot];
        a.target = target;
    };
    add(dosingP1_, A_DOSING, Sev::P1, "Dozownik", "pompa 2 pracuje za długo", Screen::Overview);
    add(offline_[0], A_OFF0, Sev::P2, "Lampa biała", "brak łączności", Screen::Lamps);
    add(offline_[1], A_OFF1, Sev::P2, "Lampa czarna", "brak łączności", Screen::Lamps);
    add(lg.lamps[0].flags & LAMP_FLAG_FRAM, A_FRAM0, Sev::P2, "Lampa biała", "awaria FRAM — praca z RAM",
        Screen::Lamps);
    add(!m.wifi, A_WIFI, Sev::P2, "Panel", "brak Wi-Fi", Screen::Overview);
    add(lg.lamps[1].flags & LAMP_FLAG_RTC, A_RTC1, Sev::P3, "Lampa czarna", "awaria zegara RTC", Screen::Lamps);
    add(lg.mismatch, A_MISMATCH, Sev::P3, "Lampy", "rozjazd trybu lub programu", Screen::Lamps);
    add(topoffP3_, A_TOPOFF, Sev::P3, "Dolewka", "zbiornik RODI poniżej 20 %", Screen::Overview);
    std::stable_sort(m.alarms, m.alarms + m.alarmCount, [](const Alarm& a, const Alarm& b) {
        if (a.prio != b.prio) return (uint8_t)a.prio > (uint8_t)b.prio;
        return a.since < b.since;
    });

    m.tiles[1].status = dosingP1_ ? Sev::P1 : Sev::Ok;
    copy(m.tiles[1].line1, sizeof(m.tiles[1].line1), dosingP1_ ? "alarm: pompa 2" : "4 kanały OK");
    m.tiles[0].status = topoffP3_ ? Sev::P3 : Sev::Ok;
    copy(m.tiles[0].line1, sizeof(m.tiles[0].line1), topoffP3_ ? "RODI < 20 %" : "poziom OK");
    m.tiles[4].status = m.wifi ? Sev::Ok : Sev::P2;
    copy(m.tiles[4].line1, sizeof(m.tiles[4].line1), m.wifi ? "Wi-Fi -59 dBm" : "brak Wi-Fi");
    m.version++;
}

void Mock::tick(uint32_t nowMs) {
    nowMs_ = nowMs;
    LampGroup& lg = model.lamps;
    if (nowMs - lastHbMs_ >= 15000) {  // heartbeats: lastSeen of online lamps
        lastHbMs_ = nowMs;
        for (int i = 0; i < lg.count; i++)
            if (lg.lamps[i].online) lg.lamps[i].lastSeen = time(nullptr);
    }

    if (cmdDueMs_ && (int32_t)(nowMs - cmdDueMs_) >= 0) {
        cmdDueMs_ = 0;
        Pending p = lg.pending;
        lg.pending = Pending::None;
        int done = 0;
        if (rejectNext_) {
            rejectNext_ = false;
            gui::toast(p == Pending::Program ? "Lampa czarna odrzuciła program (usunięty w GUI lampy)"
                                             : "Brak potwierdzenia od: Lampa czarna",
                       Sev::P2);
            for (int i = 0; i < lg.count; i++) {  // the other lamp did switch
                LampRow& l = lg.lamps[i];
                if (!l.online || i == 1) continue;
                if (p == Pending::Mode) l.mode = lg.pendingMode;
                else {
                    l.programId = lg.pendingProgram;
                    copy(l.program, sizeof(l.program), programName(l.programId));
                }
            }
        } else {
            for (int i = 0; i < lg.count; i++) {
                LampRow& l = lg.lamps[i];
                if (!l.online) continue;
                if (p == Pending::Mode) l.mode = lg.pendingMode;
                else {
                    l.programId = lg.pendingProgram;
                    copy(l.program, sizeof(l.program), programName(l.programId));
                }
                done++;
            }
            char msg[96];
            if (p == Pending::Mode) snprintf(msg, sizeof(msg), "Tryb %s ustawiony (lampy: %d)",
                                             lg.pendingMode == MODE_AUTO ? "Auto" : lg.pendingMode == MODE_NIGHT ? "Noc" : "Test", done);
            else snprintf(msg, sizeof(msg), "Program „%s” ustawiony (lampy: %d)", programName(lg.pendingProgram), done);
            gui::toast(msg);
        }
        derive();
    }

    if (programsDueMs_ && (int32_t)(nowMs - programsDueMs_) >= 0) {
        programsDueMs_ = 0;
        fillPrograms();
        model.version++;
    }
}

void Mock::setMode(uint8_t mode) {
    model.lamps.pending = Pending::Mode;
    model.lamps.pendingMode = mode;
    cmdDueMs_ = nowMs_ + CMD_DELAY_MS;
    model.version++;
}

void Mock::activateProgram(uint64_t id) {
    model.lamps.pending = Pending::Program;
    model.lamps.pendingProgram = id;
    cmdDueMs_ = nowMs_ + CMD_DELAY_MS + 400;
    model.version++;
}

void Mock::refreshPrograms() {
    if (!slowPrograms_) return;  // names cached on the panel: list ready at once
    model.programCount = 0;
    model.programsLoading = true;
    programsDueMs_ = nowMs_ + PROGRAMS_SLOW_MS;
    model.version++;
}

void Mock::key(char k) {
    LampGroup& lg = model.lamps;
    switch (k) {
        case '1':
            offline_[1] = !offline_[1];
            if (offline_[1]) offlineSince_[1] = time(nullptr);
            break;
        case '2': lg.lamps[0].flags ^= LAMP_FLAG_FRAM; break;
        case '3': lg.lamps[1].flags ^= LAMP_FLAG_RTC; break;
        case '4': lg.lamps[1].mode = lg.lamps[1].mode == MODE_NIGHT ? lg.lamps[0].mode : (uint8_t)MODE_NIGHT; break;
        case '5': dosingP1_ = !dosingP1_; break;
        case '6': topoffP3_ = !topoffP3_; break;
        case '7':
            rejectNext_ = !rejectNext_;
            printf("next command %s\n", rejectNext_ ? "without ACK from Lampa czarna" : "normal");
            return;
        case '8': model.wifi = !model.wifi; break;
        case '9': model.timeValid = !model.timeValid; break;
        case '0':
            slowPrograms_ = !slowPrograms_;
            printf("program list %s\n", slowPrograms_ ? "loads in 2 s" : "cached");
            return;
        case 's': gui::showScreensaver(); return;
        case 'h': help(); return;
        default: return;
    }
    derive();
}

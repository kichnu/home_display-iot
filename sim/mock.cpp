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
enum { A_OFF0, A_OFF1, A_FRAM0, A_RTC1, A_MISMATCH, A_DOSING, A_WIFI, A_T_OFFLINE, A_T_BIT0 };  // + 9 top-off bits
enum { A_TH_OFFLINE = A_T_BIT0 + 9, A_TH_BIT0 };  // + 5 thermostat bits (since_[24])

// Top-off simulation: shortened times so a session is watchable
constexpr uint32_t T_PORTION_MS = 4000;  // node: dose from the calibration (~30 s)
constexpr uint32_t T_PAUSE_MS = 2000;    // node: 5 s
constexpr uint32_t T_CYCLE_MS = 6000;    // auto top-off pumping phase
constexpr uint32_t T_CMD_MS = 900;
constexpr int T_CAPACITY_PORTIONS = 82;  // alarm level to the marker
constexpr float T_RATE_ML_H = 92.0f;
const int T_LEVELS[] = {42, 25, 12};     // key 6: reservoir fill %
// Thermostat (target 25,0 °C, fan from 26,0 °C): key t steps heating / buffer / cooling / too warm / too cold
const int16_t TH_TEMPS[] = {2440, 2540, 2660, 2780, 2270};
constexpr int TH_TEMP_STEPS = sizeof(TH_TEMPS) / sizeof(TH_TEMPS[0]);
constexpr uint32_t TH_SERVICE_S = 15 * 60;
constexpr uint32_t TH_CMD_MS = 900;

void copy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src); }
}  // namespace

void Mock::help() {
    printf(
        "\nGUI simulator — keys (focus on the window):\n"
        "  1  Lampa czarna offline / online       2  Lampa biała: awaria FRAM\n"
        "  3  Lampa czarna: awaria RTC            4  rozjazd: Lampa czarna w trybie Noc\n"
        "  5  Dozownik: alarm P1                  6  Dolewka: poziom zbiornika 42 / 25 / 12 %%\n"
        "  7  następne polecenie bez ACK          8  Wi-Fi panelu wył. / wł.\n"
        "  9  czas nieważny / ważny               0  wolne pobieranie listy programów\n"
        "  d  Dolewka: limit dobowy (alarm P1)    e  Dolewka: brak odczytu ToF (szacunek)\n"
        "  o  Dolewka offline / online            p  Dolewka: cykl dolewki auto\n"
        "  t  Termostat: temperatura (grzeje / bufor / chłodzi / za ciepło / za zimno)\n"
        "  f  Termostat: czujnik uszkodzony / OK  g  Termostat: awaria FRAM/RTC\n"
        "  r  Termostat: grzałka nieskuteczna     n  Termostat offline / online\n"
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
    t[0] = {"Dolewka", Sev::Ok, "", "", true, Screen::Topoff};
    t[1] = {"Dozownik", Sev::Ok, "4 kanały OK", "następna dawka 18:00", false, Screen::Overview};
    t[2] = {"Termostat", Sev::Ok, "", "", true, Screen::Thermo};
    t[3] = {"Timer", Sev::Ok, "3/4 włączone", "CO₂ do 20:00", false, Screen::Overview};
    t[4] = {"Panel", Sev::Ok, "Wi-Fi -59 dBm", "FW 0.8.0", false, Screen::Overview};
    topoffInit();
    thermoInit();
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
    topoffDerive();
    thermoDerive();

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
    // Top-off: offline = P2, otherwise one alarm per bit
    const TopoffView& tv = m.topoff;
    add(tv.present && !tv.online, A_T_OFFLINE, Sev::P2, "Dolewka", "brak łączności", Screen::Topoff);
    for (int i = 0; i < TOPOFF_ALARM_COUNT; i++)
        add(tv.online && (tv.alarms & TOPOFF_ALARMS[i].bit), A_T_BIT0 + i, TOPOFF_ALARMS[i].prio, "Dolewka",
            TOPOFF_ALARMS[i].text, Screen::Topoff);
    // Thermostat: offline = P2, otherwise one alarm per bit (muted ones say so)
    const ThermoView& th = m.thermo;
    add(th.present && !th.online, A_TH_OFFLINE, Sev::P2, "Termostat", "brak łączności", Screen::Thermo);
    for (int i = 0; i < THERMO_ALARM_COUNT; i++) {
        char text[64];
        snprintf(text, sizeof(text), "%s%s", THERMO_ALARMS[i].text,
                 (th.muted & THERMO_ALARMS[i].bit) ? " (wyciszony)" : "");
        add(th.online && (th.alarms & THERMO_ALARMS[i].bit), A_TH_BIT0 + i, THERMO_ALARMS[i].prio, "Termostat",
            text, Screen::Thermo);
    }
    std::stable_sort(m.alarms, m.alarms + m.alarmCount, [](const Alarm& a, const Alarm& b) {
        if (a.prio != b.prio) return (uint8_t)a.prio > (uint8_t)b.prio;
        return a.since < b.since;
    });

    m.tiles[1].status = dosingP1_ ? Sev::P1 : Sev::Ok;
    copy(m.tiles[1].line1, sizeof(m.tiles[1].line1), dosingP1_ ? "alarm: pompa 2" : "4 kanały OK");
    // Top-off tile: worst alarm of the node
    {
        NodeTile& t = m.tiles[0];
        Sev worst = Sev::Ok;
        for (int i = 0; i < m.alarmCount; i++)
            if (m.alarms[i].target == Screen::Topoff && (uint8_t)m.alarms[i].prio > (uint8_t)worst)
                worst = m.alarms[i].prio;
        t.status = tv.online ? worst : Sev::Offline;
        if (!tv.online) {
            copy(t.line1, sizeof(t.line1), "Brak łączności");
            copy(t.line2, sizeof(t.line2), "");
        } else {
            snprintf(t.line1, sizeof(t.line1), "RO %u %% · ~%d h", tv.resPct, (tv.resHoursX10 + 5) / 10);
            if (tv.manualSession)
                snprintf(t.line2, sizeof(t.line2), "Porcje: %u z %u", tv.manualDone + 1, tv.manualDone + tv.manualLeft);
            else if (!tv.autoMode)
                snprintf(t.line2, sizeof(t.line2), "Serwis · Auto za %u min", tv.serviceLeftMin);
            else
                snprintf(t.line2, sizeof(t.line2), "24 h: %u / %u ml", tv.volume24h, tv.dailyLimit);
        }
    }
    // Thermostat tile: temperature, state / Service countdown
    {
        NodeTile& t = m.tiles[2];
        Sev worst = Sev::Ok;
        for (int i = 0; i < m.alarmCount; i++)
            if (m.alarms[i].target == Screen::Thermo && (uint8_t)m.alarms[i].prio > (uint8_t)worst)
                worst = m.alarms[i].prio;
        t.status = th.online ? worst : Sev::Offline;
        if (!th.online) {
            copy(t.line1, sizeof(t.line1), "Brak łączności");
            copy(t.line2, sizeof(t.line2), "");
        } else {
            int v = (th.tempX100 + 5) / 10;
            if (th.tempValid) snprintf(t.line1, sizeof(t.line1), "%d,%d °C", v / 10, v % 10);
            else copy(t.line1, sizeof(t.line1), "brak odczytu");
            const char* st[] = {"Czuwa", "Grzeje", "Chłodzi", "Za zimno", "Za ciepło", "Awaria czujnika", "Grzałka nieskuteczna"};
            if (!th.autoMode) snprintf(t.line2, sizeof(t.line2), "Serwis · Auto za %u min", (th.serviceLeftS + 59) / 60);
            else if (th.fanPct) snprintf(t.line2, sizeof(t.line2), "%s · wentylator %u %%", st[th.state], th.fanPct);
            else copy(t.line2, sizeof(t.line2), st[th.state < 7 ? th.state : 0]);
        }
    }
    m.tiles[4].status = m.wifi ? Sev::Ok : Sev::P2;
    copy(m.tiles[4].line1, sizeof(m.tiles[4].line1), m.wifi ? "Wi-Fi -59 dBm" : "brak Wi-Fi");
    m.version++;
}

void Mock::tick(uint32_t nowMs) {
    nowMs_ = nowMs;
    topoffTick(nowMs);
    thermoTick(nowMs);
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

// ---- top-off ----

void Mock::topoffInit() {
    TopoffView& t = model.topoff;
    t.present = true;
    t.online = true;
    t.controls = true;
    t.lastSeen = time(nullptr);
    copy(t.name, sizeof(t.name), "DOLEWKA");
    t.autoMode = true;
    t.state = TA_IDLE;
    t.volume24h = 750;
    t.dailyLimit = 4000;
    t.dose = 250;
    t.resPct = (uint8_t)T_LEVELS[0];
    t.resReservePct = 30;
    t.resAlarmPct = 15;
    topoffDerive();
}

void Mock::topoffDerive() {
    TopoffView& t = model.topoff;
    t.online = !topoffOffline_;
    if (t.online) t.lastSeen = time(nullptr);
    int above = (int)t.resPct - t.resAlarmPct;
    t.resPortions = above > 0 ? (uint16_t)(above * T_CAPACITY_PORTIONS / 100) : 0;
    t.resHoursX10 = (int16_t)(t.resPortions * t.dose * 10 / T_RATE_ML_H);
    t.resEstimated = topoffEstimated_;
    t.resZone = t.resPct <= t.resAlarmPct ? TZ_ALARM : t.resPct <= t.resReservePct ? TZ_RESERVE : TZ_OK;
    if (t.resZone == TZ_ALARM) topoffLowLatched_ = true;  // latched until reset with a refilled reservoir

    uint16_t a = 0;
    if (topoffLowLatched_) a |= TOPOFF_AL_LOW_RESERVOIR;
    if (topoffDailyLimit_) a |= TOPOFF_AL_DAILY_LIMIT;
    if (t.resZone == TZ_RESERVE) a |= TOPOFF_AL_RESERVE_LOW;
    if (topoffEstimated_) a |= TOPOFF_AL_LEVEL_NO_READING;
    t.alarms = a;
    if (topoffDailyLimit_ || topoffLowLatched_) t.state = TA_ERROR;
    else if (t.state == TA_ERROR) t.state = TA_IDLE;

    // Node rule: Fault > Alarm (P1) > Service > Warning (P3) > OK
    if (a & TOPOFF_AL_HW_FAULT) t.status = TS_FAULT;
    else if (a & TOPOFF_AL_RESETTABLE) t.status = TS_ALARM;
    else if (!t.autoMode) t.status = TS_SERVICE;
    else if (a) t.status = TS_WARNING;
    else t.status = TS_OK;
}

void Mock::topoffSend(TopoffPending p) {
    TopoffView& t = model.topoff;
    if (t.pending != TopoffPending::None || !t.online) return;
    t.pending = p;
    topoffCmdDueMs_ = nowMs_ + T_CMD_MS;
    model.version++;
}

void Mock::topoffTick(uint32_t nowMs) {
    TopoffView& t = model.topoff;
    bool changed = false;

    // Command answer (status / ACK from the node)
    if (topoffCmdDueMs_ && (int32_t)(nowMs - topoffCmdDueMs_) >= 0) {
        topoffCmdDueMs_ = 0;
        TopoffPending p = t.pending;
        t.pending = TopoffPending::None;
        changed = true;
        if (rejectNext_ || !t.online) {
            rejectNext_ = false;
            gui::toast("Dolewka: brak potwierdzenia — polecenie nie wykonane", Sev::P2);
        } else {
            switch (p) {
                case TopoffPending::Mode:
                    t.autoMode = t.pendingAuto;
                    if (t.autoMode && t.manualSession) {  // the switch to Auto stops the session
                        t.manualSession = false;
                        t.manualLeft = 0;
                        t.pumpOn = false;
                    }
                    topoffServiceMs_ = nowMs;
                    t.serviceLeftMin = t.autoMode ? 0 : 180;
                    gui::toast(t.autoMode ? "Dolewka: tryb Auto" : "Dolewka: tryb Serwis (powrót do Auto po 180 min)");
                    break;
                case TopoffPending::Portions:
                    if (t.autoMode) {
                        gui::toast("Dolewka odrzuciła: porcje tylko w trybie Serwis", Sev::P2);
                    } else if (topoffLowLatched_) {
                        gui::toast("Dolewka odrzuciła: zbiornik RO pusty", Sev::P2);
                    } else {
                        t.manualSession = true;
                        t.manualDone = 0;
                        t.manualLeft = t.pendingPortions;
                        t.pumpOn = true;
                        topoffPause_ = false;
                        topoffPhaseMs_ = nowMs + T_PORTION_MS;
                    }
                    break;
                case TopoffPending::Stop:
                    t.manualSession = false;
                    t.manualLeft = 0;
                    t.pumpOn = false;
                    gui::toast("Dolewka: STOP — bieżąca porcja zostanie dokończona");
                    break;
                case TopoffPending::Reset:
                    topoffDailyLimit_ = false;
                    if (topoffLowLatched_ && t.resPct > t.resAlarmPct + 2) topoffLowLatched_ = false;
                    gui::toast(topoffLowLatched_ ? "Skasowano — alarm zbiornika trwa do uzupełnienia"
                                                 : "Dolewka: alarmy skasowane",
                               topoffLowLatched_ ? Sev::P3 : Sev::Ok);
                    break;
                case TopoffPending::Limit: {
                    t.dailyLimit = t.pendingLimit;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Dolewka: limit dobowy %u ml", t.dailyLimit);
                    gui::toast(msg);
                    break;
                }
                default: break;
            }
        }
    }

    // Manual session: portion → pause → portion …
    if (t.manualSession && (int32_t)(nowMs - topoffPhaseMs_) >= 0) {
        changed = true;
        if (!topoffPause_) {
            t.pumpOn = false;
            t.manualDone++;
            t.manualLeft--;
            if (t.resPct > 0) t.resPct--;  // ~one portion of the reservoir
            if (t.manualLeft == 0) t.manualSession = false;
            topoffPause_ = true;
            topoffPhaseMs_ = nowMs + T_PAUSE_MS;
        } else {
            topoffPause_ = false;
            t.pumpOn = true;
            topoffPhaseMs_ = nowMs + T_PORTION_MS;
        }
    }

    // Auto top-off cycle (key p)
    if (t.state == TA_PUMPING) {
        if ((int32_t)(nowMs - topoffPhaseMs_) >= 0) {
            t.state = TA_IDLE;
            t.pumpOn = false;
            t.volume24h += t.dose;
            if (t.resPct > 0) t.resPct--;
            changed = true;
        } else {
            uint16_t rem = (uint16_t)((topoffPhaseMs_ - nowMs + 999) / 1000);
            if (rem != t.phaseRemainingS) {
                t.phaseRemainingS = rem;
                changed = true;
            }
        }
    }

    // Service Mode countdown (1 min per real minute)
    if (!t.autoMode) {
        uint32_t el = (nowMs - topoffServiceMs_) / 60000;
        uint16_t left = el >= 180 ? 0 : (uint16_t)(180 - el);
        if (left != t.serviceLeftMin) {
            t.serviceLeftMin = left;
            changed = true;
        }
    }

    if (changed) derive();
}

void Mock::topoffSetAuto(bool on) {
    model.topoff.pendingAuto = on;
    topoffSend(TopoffPending::Mode);
}

void Mock::topoffPortions(uint8_t n) {
    model.topoff.pendingPortions = n;
    topoffSend(TopoffPending::Portions);
}

void Mock::topoffStop() { topoffSend(TopoffPending::Stop); }

void Mock::topoffReset() { topoffSend(TopoffPending::Reset); }

void Mock::topoffSetLimit(uint16_t ml) {
    model.topoff.pendingLimit = ml;
    topoffSend(TopoffPending::Limit);
}

// ---- thermostat ----

void Mock::thermoInit() {
    ThermoView& t = model.thermo;
    t.present = true;
    t.online = true;
    t.controls = true;
    t.lastSeen = time(nullptr);
    copy(t.name, sizeof(t.name), "TERMOSTAT");
    t.autoMode = true;
    t.tempValid = true;
    t.tempX100 = TH_TEMPS[thermoTempStep_];
    thermoDerive();
}

void Mock::thermoDerive() {
    ThermoView& t = model.thermo;
    t.online = !thermoOffline_;
    if (t.online) t.lastSeen = time(nullptr);
    if (thermoSensorCause_) thermoSensorLatched_ = true;  // latched until reset with a working sensor
    t.tempValid = !thermoSensorCause_;
    t.tempX100 = TH_TEMPS[thermoTempStep_];
    bool low = t.tempValid && t.tempX100 < 2300, high = t.tempValid && t.tempX100 > 2750;

    if (thermoSensorLatched_) {  // node: heater and fan stopped until the reset
        t.heaterOn = false;
        t.fanPct = 0;
        t.state = TH_SENSOR_FAULT;
    } else if (t.autoMode) {     // node algorithm (Service Mode: manual heater / fan stay)
        t.heaterOn = low || t.tempX100 < 2500;
        t.fanPct = high ? 100 : t.tempX100 >= 2600 ? (uint8_t)(30 + (t.tempX100 - 2600) * 70 / 150) : 0;
        t.state = low ? TH_ALARM_LOW : high ? TH_ALARM_HIGH : t.heaterOn ? TH_HEATING : t.fanPct ? TH_COOLING : TH_IDLE;
    }
    t.inBuffer = t.tempValid && !t.heaterOn && !t.fanPct && t.tempX100 >= 2500 && t.tempX100 < 2600;

    uint16_t a = 0;
    if (low) a |= THERMO_AL_TEMP_LOW;
    if (high) a |= THERMO_AL_TEMP_HIGH;
    if (thermoTrend_) a |= THERMO_AL_TREND;
    if (thermoSensorLatched_) a |= THERMO_AL_SENSOR_FAULT;
    if (thermoHw_) a |= THERMO_AL_HW_FAULT;
    t.alarms = a;
    t.muted &= a;  // an episode that ended — the next one sounds again

    // Node rule: Fault (latched) > Alarm (temperature) > Service > Warning (trend) > OK
    if (a & (THERMO_AL_SENSOR_FAULT | THERMO_AL_HW_FAULT)) t.status = TS_FAULT;
    else if (a & THERMO_AL_MUTABLE) t.status = TS_ALARM;
    else if (!t.autoMode) t.status = TS_SERVICE;
    else if (a) t.status = TS_WARNING;
    else t.status = TS_OK;
}

void Mock::thermoSend(ThermoPending p) {
    ThermoView& t = model.thermo;
    if (t.pending != ThermoPending::None || !t.online) return;
    t.pending = p;
    thermoCmdDueMs_ = nowMs_ + TH_CMD_MS;
    model.version++;
}

void Mock::thermoTick(uint32_t nowMs) {
    ThermoView& t = model.thermo;
    bool changed = false;

    if (thermoCmdDueMs_ && (int32_t)(nowMs - thermoCmdDueMs_) >= 0) {
        thermoCmdDueMs_ = 0;
        ThermoPending p = t.pending;
        t.pending = ThermoPending::None;
        changed = true;
        if (rejectNext_ || !t.online) {
            rejectNext_ = false;
            gui::toast("Termostat: brak potwierdzenia — sprawdź stan", Sev::P2);
        } else {
            switch (p) {
                case ThermoPending::Mode:
                    t.autoMode = t.pendingAuto;
                    thermoServiceMs_ = nowMs;
                    t.serviceLeftS = t.autoMode ? 0 : TH_SERVICE_S;
                    gui::toast(t.autoMode ? "Termostat: tryb Auto" : "Termostat: tryb Serwis (powrót do Auto po 15 min)");
                    break;
                case ThermoPending::Mute:
                    t.muted |= t.alarms & THERMO_AL_MUTABLE;
                    gui::toast("Termostat: alarm wyciszony — wróci przy kolejnym wystąpieniu");
                    break;
                case ThermoPending::SensorReset:
                    thermoSensorLatched_ = false;
                    if (thermoSensorCause_)
                        gui::toast("Awaria czujnika trwa — czujnik dalej nie działa", Sev::P2);
                    else gui::toast("Termostat: awaria czujnika skasowana");
                    break;
                case ThermoPending::Heater:
                    if (t.autoMode) gui::toast("Termostat odrzucił: ręczne sterowanie tylko w trybie Serwis", Sev::P2);
                    else t.heaterOn = t.pendingHeater;
                    break;
                case ThermoPending::Fan:
                    if (t.autoMode) gui::toast("Termostat odrzucił: ręczne sterowanie tylko w trybie Serwis", Sev::P2);
                    else t.fanPct = t.pendingFan;
                    break;
                default: break;
            }
        }
    }

    // Service Mode countdown (real seconds), then back to Auto
    if (!t.autoMode) {
        uint32_t el = (nowMs - thermoServiceMs_) / 1000;
        uint16_t left = el >= TH_SERVICE_S ? 0 : (uint16_t)(TH_SERVICE_S - el);
        if (left != t.serviceLeftS) {
            t.serviceLeftS = left;
            if (!left) t.autoMode = true;
            changed = true;
        }
    }

    if (changed) derive();
}

void Mock::thermoSetAuto(bool on) {
    model.thermo.pendingAuto = on;
    thermoSend(ThermoPending::Mode);
}

void Mock::thermoMute() { thermoSend(ThermoPending::Mute); }

void Mock::thermoSensorReset() { thermoSend(ThermoPending::SensorReset); }

void Mock::thermoHeater(bool on) {
    model.thermo.pendingHeater = on;
    thermoSend(ThermoPending::Heater);
}

void Mock::thermoFan(uint8_t pct) {
    model.thermo.pendingFan = pct;
    thermoSend(ThermoPending::Fan);
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
        case '6':
            topoffLevel_ = (topoffLevel_ + 1) % 3;
            model.topoff.resPct = (uint8_t)T_LEVELS[topoffLevel_];
            break;
        case 'd':
            topoffDailyLimit_ = !topoffDailyLimit_;
            if (topoffDailyLimit_) model.topoff.volume24h = model.topoff.dailyLimit;
            break;
        case 'e': topoffEstimated_ = !topoffEstimated_; break;
        case 'o':
            topoffOffline_ = !topoffOffline_;
            if (!topoffOffline_) model.topoff.lastSeen = time(nullptr);
            break;
        case 'p':
            if (model.topoff.autoMode && model.topoff.state == TA_IDLE && !topoffDailyLimit_ && !topoffLowLatched_) {
                model.topoff.state = TA_PUMPING;
                model.topoff.pumpOn = true;
                topoffPhaseMs_ = nowMs_ + T_CYCLE_MS;
            }
            break;
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
        case 't': thermoTempStep_ = (thermoTempStep_ + 1) % TH_TEMP_STEPS; break;
        case 'f': thermoSensorCause_ = !thermoSensorCause_; break;
        case 'g': thermoHw_ = !thermoHw_; break;
        case 'r': thermoTrend_ = !thermoTrend_; break;
        case 'n':
            thermoOffline_ = !thermoOffline_;
            if (!thermoOffline_) model.thermo.lastSeen = time(nullptr);
            break;
        case 's': gui::showScreensaver(); return;
        case 'h': help(); return;
        default: return;
    }
    derive();
}

// L2 thermostat node (docs/PLAN_TERMOSTAT.md): summary (status, first alarm, state), water temperature
// (read only, colour like the node's web GUI), heater and fan indicators; controls of the node's
// "System Control": Auto / Serwis (no confirmation), "Wycisz alarm" (TEMP_LOW / TEMP_HIGH only),
// "Kasuj awarię czujnika" (dialog), manual heater / fan ON-OFF and fan ▼ ▲ (Service Mode only).
// Every control shows "sent" right after the tap (accent outline) and takes its new state only from
// the node's status / ACK (ThermoView::pending). Without ThermoView::controls the screen is read only.

#include <stdio.h>
#include <string.h>

#include "gui_internal.h"

namespace gui {
namespace thermo {

namespace {
lv_obj_t* s_status = nullptr;
lv_obj_t* s_alarm = nullptr;
lv_obj_t* s_state = nullptr;
lv_obj_t* s_temp = nullptr;
lv_obj_t* s_tempNote = nullptr;
lv_obj_t* s_heaterDot = nullptr;
lv_obj_t* s_heaterVal = nullptr;
lv_obj_t* s_fanDot = nullptr;
lv_obj_t* s_fanVal = nullptr;
lv_obj_t* s_fanBar = nullptr;
lv_obj_t* s_btnAuto = nullptr;
lv_obj_t* s_btnService = nullptr;
lv_obj_t* s_btnMute = nullptr;
lv_obj_t* s_btnMuteLabel = nullptr;
lv_obj_t* s_btnReset = nullptr;
lv_obj_t* s_btnHeater = nullptr;
lv_obj_t* s_btnHeaterLabel = nullptr;
lv_obj_t* s_btnFan = nullptr;
lv_obj_t* s_btnFanLabel = nullptr;
lv_obj_t* s_btnFanDown = nullptr;
lv_obj_t* s_btnFanUp = nullptr;
lv_obj_t* s_fanStepVal = nullptr;
lv_obj_t* s_manualHint = nullptr;

constexpr int32_t SUMMARY_H = 48;
constexpr uint32_t C_TEMP_LOW = 0x4A6FD0;  // TEMP_LOW: dark blue, differs from the heating accent

const ThermoView& tv() { return g.m->thermo; }

bool online() { return tv().present && tv().online; }

bool busy() { return tv().pending != ThermoPending::None; }

// Commands can be sent: node online and the adapter supports them
bool ctl() { return online() && tv().controls; }

// Manual heater / fan: Service Mode only (as the node's web GUI)
bool manualEnabled() { return ctl() && !tv().autoMode; }

const char* statusName(uint8_t s) {
    switch (s) {
        case TS_OK: return "OK";
        case TS_WARNING: return "Uwaga";
        case TS_ALARM: return "Alarm";
        case TS_FAULT: return "Awaria";
        case TS_SERVICE: return "Serwis";
    }
    return "?";
}

const char* stateName(uint8_t s) {
    switch (s) {
        case TH_IDLE: return "Czuwa";
        case TH_HEATING: return "Grzeje";
        case TH_COOLING: return "Chłodzi";
        case TH_ALARM_LOW: return "Za zimno";
        case TH_ALARM_HIGH: return "Za ciepło";
        case TH_SENSOR_FAULT: return "Awaria czujnika";
        case TH_TREND: return "Grzałka nieskuteczna";
    }
    return "?";
}

// "25,4 °C" (Polish decimal comma, one decimal)
void formatTemp(int16_t x100, char* buf, size_t n) {
    int v = x100 >= 0 ? (x100 + 5) / 10 : (x100 - 5) / 10;
    snprintf(buf, n, "%s%d,%d °C", v < 0 ? "-" : "", (v < 0 ? -v : v) / 10, (v < 0 ? -v : v) % 10);
}

void formatSeconds(uint16_t s, char* buf, size_t n) {
    if (s >= 60) snprintf(buf, n, "%u min", (s + 59) / 60);
    else snprintf(buf, n, "%u s", s);
}

// Same priority as the node's web GUI: alarm > heating > thermal buffer > cooling
uint32_t tempColor(const ThermoView& t) {
    if (!online() || !t.tempValid) return C_OFFLINE;
    if (t.alarms & THERMO_AL_TEMP_LOW) return C_TEMP_LOW;
    if (t.alarms & (THERMO_AL_TEMP_HIGH | THERMO_AL_SENSOR_FAULT)) return C_P1;
    if (t.heaterOn) return C_ACCENT;
    if (t.inBuffer) return C_P3;
    if (t.fanPct > 0) return C_P2;
    return C_TEXT;
}

lv_obj_t* card(lv_obj_t* parent) {
    lv_obj_t* c = lv_obj_create(parent);
    lv_obj_set_style_bg_color(c, color(C_SURFACE), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t* button(lv_obj_t* parent, const char* text, int32_t width, lv_obj_t** labelOut = nullptr) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, width, TOUCH_H);
    lv_obj_set_style_bg_color(b, color(C_SURFACE2), 0);
    lv_obj_set_style_bg_color(b, color(C_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_color(b, color(C_ACCENT), 0);
    lv_obj_t* l = label(b, fonts().title, C_TEXT, text);
    lv_obj_center(l);
    if (labelOut) *labelOut = l;
    return b;
}

void setEnabled(lv_obj_t* o, bool on) {
    if (on) lv_obj_remove_state(o, LV_STATE_DISABLED);
    else lv_obj_add_state(o, LV_STATE_DISABLED);
}

void setChecked(lv_obj_t* o, bool on) {
    if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
    else lv_obj_remove_state(o, LV_STATE_CHECKED);
}

// "Sent" mark right after the tap — the button keeps its state until the node answers
void setSent(lv_obj_t* o, bool on) { lv_obj_set_style_border_width(o, on ? 4 : 0, 0); }

// Indicator row: name, coloured dot, value
lv_obj_t* indicator(lv_obj_t* parent, const char* name, lv_obj_t** dot, lv_obj_t** value) {
    lv_obj_t* row = box(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_t* n = label(row, fonts().body, C_MUTED, name);
    lv_obj_set_flex_grow(n, 1);
    *dot = lv_obj_create(row);
    lv_obj_set_size(*dot, 16, 16);
    lv_obj_set_style_radius(*dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(*dot, 0, 0);
    lv_obj_set_style_bg_opa(*dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(*dot, LV_OBJ_FLAG_SCROLLABLE);
    *value = label(row, fonts().title, C_TEXT, "");
    return row;
}

// ---- controls ----

void onAuto(lv_event_t*) {
    if (!ctl() || busy() || tv().autoMode) return;
    g.cmd->thermoSetAuto(true);
}

void onService(lv_event_t*) {
    if (!ctl() || busy() || !tv().autoMode) return;
    g.cmd->thermoSetAuto(false);
}

void onMute(lv_event_t*) {
    if (!ctl() || busy() || !(tv().alarms & THERMO_AL_MUTABLE & ~tv().muted)) return;
    g.cmd->thermoMute();  // only the self-clearing temperature alarms: no dialog
}

void resetOk() {
    if (ctl() && !busy()) g.cmd->thermoSensorReset();
}

void onReset(lv_event_t*) {
    if (!ctl() || busy() || !(tv().alarms & THERMO_AL_SENSOR_FAULT)) return;
    confirm("Kasować awarię czujnika?",
            "Grzałka i wentylator są wyłączone do skasowania. Kasuj dopiero po sprawdzeniu czujnika i kabla — "
            "jeśli czujnik dalej nie działa, awaria wróci przy następnym pomiarze.",
            "Kasuj", resetOk);
}

void onHeater(lv_event_t*) {
    if (!manualEnabled() || busy()) return;
    g.cmd->thermoHeater(!tv().heaterOn);
}

void onFan(lv_event_t*) {
    if (!manualEnabled() || busy()) return;
    g.cmd->thermoFan(tv().fanPct > 0 ? 0 : THERMO_FAN_MANUAL_PCT);
}

void onFanStep(lv_event_t* e) {
    if (!manualEnabled() || busy()) return;
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int v = (int)tv().fanPct + dir * THERMO_FAN_STEP;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v != tv().fanPct) g.cmd->thermoFan((uint8_t)v);
}

void refreshSummary() {
    const ThermoView& t = tv();
    char buf[96];
    if (!t.present) {
        lv_label_set_text(s_status, "Brak danych");
        lv_obj_set_style_text_color(s_status, color(C_OFFLINE), 0);
        lv_label_set_text(s_alarm, "czekam na status termostatu");
        lv_label_set_text(s_state, "");
        return;
    }
    if (!t.online) {
        char ago[24];
        formatAgo(t.lastSeen, ago, sizeof(ago));
        lv_label_set_text(s_status, "Brak łączności");
        lv_obj_set_style_text_color(s_status, color(C_OFFLINE), 0);
        snprintf(buf, sizeof(buf), "ostatni status %s temu", ago);
        lv_label_set_text(s_alarm, buf);
        lv_obj_set_style_text_color(s_alarm, color(C_MUTED), 0);
        lv_label_set_text(s_state, "");
        return;
    }
    Sev sev = thermoSev();
    lv_label_set_text(s_status, statusName(t.status));
    lv_obj_set_style_text_color(s_status, color(t.status == TS_SERVICE && sev == Sev::Ok ? C_ACCENT : sevColor(sev)),
                                0);

    // First alarm of this node (list is sorted by priority; a muted one says so in its text)
    const Alarm* first = nullptr;
    for (int i = 0; i < g.m->alarmCount && !first; i++)
        if (g.m->alarms[i].target == Screen::Thermo) first = &g.m->alarms[i];
    lv_label_set_text(s_alarm, first ? first->text : "");
    lv_obj_set_style_text_color(s_alarm, color(first ? sevColor(first->prio) : C_MUTED), 0);

    if (!t.autoMode && t.serviceLeftS) {
        char left[16];
        formatSeconds(t.serviceLeftS, left, sizeof(left));
        snprintf(buf, sizeof(buf), "Auto za %s", left);
    } else {
        snprintf(buf, sizeof(buf), "%s", t.autoMode ? stateName(t.state) : "Serwis");
    }
    lv_label_set_text(s_state, buf);
}

void refreshValues() {
    const ThermoView& t = tv();
    char buf[32];
    bool on = online();
    if (on && t.tempValid) formatTemp(t.tempX100, buf, sizeof(buf));
    else snprintf(buf, sizeof(buf), "–,– °C");
    lv_label_set_text(s_temp, buf);
    lv_obj_set_style_text_color(s_temp, color(tempColor(t)), 0);

    const char* note = "";
    if (on && !t.tempValid) note = "brak odczytu czujnika";
    else if (on && t.inBuffer) note = "bufor ciepła";
    lv_label_set_text(s_tempNote, note);

    // Indicators: heater ON = accent (heating blue), fan ON = orange (cooling)
    lv_obj_set_style_bg_color(s_heaterDot, color(on && t.heaterOn ? C_ACCENT : C_OFFLINE), 0);
    lv_label_set_text(s_heaterVal, on ? (t.heaterOn ? "ON" : "OFF") : "–");
    lv_obj_set_style_text_color(s_heaterVal, color(on && t.heaterOn ? C_ACCENT : C_TEXT), 0);
    lv_obj_set_style_bg_color(s_fanDot, color(on && t.fanPct ? C_P2 : C_OFFLINE), 0);
    if (!on) lv_label_set_text(s_fanVal, "–");
    else if (t.fanPct) lv_label_set_text_fmt(s_fanVal, "ON · %u %%", t.fanPct);
    else lv_label_set_text(s_fanVal, "OFF");
    lv_obj_set_style_text_color(s_fanVal, color(on && t.fanPct ? C_P2 : C_TEXT), 0);
    lv_bar_set_value(s_fanBar, on ? t.fanPct : 0, LV_ANIM_OFF);
}

void refreshControls() {
    const ThermoView& t = tv();
    bool on = online();
    setChecked(s_btnAuto, on && t.autoMode);
    setChecked(s_btnService, on && !t.autoMode);
    bool c = ctl();
    bool pendingMode = t.pending == ThermoPending::Mode;
    setSent(s_btnAuto, pendingMode && t.pendingAuto);
    setSent(s_btnService, pendingMode && !t.pendingAuto);
    setEnabled(s_btnAuto, c && (!busy() || pendingMode));
    setEnabled(s_btnService, c && (!busy() || pendingMode));

    // Mute: TEMP_LOW / TEMP_HIGH not yet muted
    uint16_t mutable_ = on ? (t.alarms & THERMO_AL_MUTABLE) : 0;
    bool pendingMute = t.pending == ThermoPending::Mute;
    bool allMuted = mutable_ && !(mutable_ & ~t.muted);
    lv_label_set_text(s_btnMuteLabel, allMuted ? LV_SYMBOL_MUTE "  Wyciszono" : LV_SYMBOL_MUTE "  Wycisz alarm");
    lv_obj_set_style_bg_color(s_btnMute, color(mutable_ && !allMuted ? C_P2 : C_SURFACE2), 0);
    setSent(s_btnMute, pendingMute);
    setEnabled(s_btnMute, c && mutable_ && !allMuted && (!busy() || pendingMute));

    bool fault = on && (t.alarms & THERMO_AL_SENSOR_FAULT);
    bool pendingReset = t.pending == ThermoPending::SensorReset;
    lv_obj_set_style_bg_color(s_btnReset, color(fault ? C_P1 : C_SURFACE2), 0);  // red also read only
    setSent(s_btnReset, pendingReset);
    setEnabled(s_btnReset, c && fault && (!busy() || pendingReset));

    // Manual row: current state on the buttons (as the web GUI), Service Mode only
    bool man = manualEnabled();
    bool pendingHeater = t.pending == ThermoPending::Heater;
    bool pendingFan = t.pending == ThermoPending::Fan;
    lv_label_set_text(s_btnHeaterLabel, on && t.heaterOn ? "Grzałka: ON" : "Grzałka: OFF");
    setChecked(s_btnHeater, on && t.heaterOn);
    setSent(s_btnHeater, pendingHeater);
    setEnabled(s_btnHeater, man && (!busy() || pendingHeater));
    lv_label_set_text(s_btnFanLabel, on && t.fanPct ? "Wentylator: ON" : "Wentylator: OFF");
    setChecked(s_btnFan, on && t.fanPct);
    setSent(s_btnFan, pendingFan);
    setEnabled(s_btnFan, man && (!busy() || pendingFan));
    setEnabled(s_btnFanDown, man && !busy() && t.fanPct > 0);
    setEnabled(s_btnFanUp, man && !busy() && t.fanPct < 100);
    if (on) lv_label_set_text_fmt(s_fanStepVal, "%u %%", t.fanPct);
    else lv_label_set_text(s_fanStepVal, "– %");

    if (!on) lv_label_set_text(s_manualHint, "");
    else if (!t.controls) lv_label_set_text(s_manualHint, "Sterowanie z panelu w kolejnym etapie");
    else if (t.autoMode) lv_label_set_text(s_manualHint, "Ręczne sterowanie tylko w trybie Serwis");
    else lv_label_set_text(s_manualHint, "");
}
}  // namespace

void build() {
    lv_obj_set_flex_flow(g.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g.content, PAD, 0);

    // Summary: status · first alarm · state
    lv_obj_t* sum = card(g.content);
    lv_obj_set_size(sum, lv_pct(100), SUMMARY_H);
    lv_obj_set_flex_flow(sum, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sum, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(sum, 0, 0);
    lv_obj_set_style_pad_column(sum, 16, 0);
    s_status = label(sum, fonts().title, C_OK, "");
    s_alarm = label(sum, fonts().body, C_MUTED, "");
    lv_obj_set_flex_grow(s_alarm, 1);
    lv_label_set_long_mode(s_alarm, LV_LABEL_LONG_DOT);
    s_state = label(sum, fonts().body, C_TEXT, "");

    // Values: temperature + heater / fan indicators
    lv_obj_t* vals = box(g.content);
    lv_obj_set_width(vals, lv_pct(100));
    lv_obj_set_flex_grow(vals, 1);
    lv_obj_set_flex_flow(vals, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(vals, PAD, 0);

    lv_obj_t* tc = card(vals);
    lv_obj_set_height(tc, lv_pct(100));
    lv_obj_set_flex_grow(tc, 3);
    lv_obj_set_flex_flow(tc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tc, 16, 0);
    s_temp = label(tc, fonts().huge, C_TEXT, "");
    lv_obj_t* tside = box(tc);
    lv_obj_set_size(tside, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tside, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tside, 4, 0);
    label(tside, fonts().body, C_MUTED, "Woda");
    s_tempNote = label(tside, fonts().small, C_P3, "");

    lv_obj_t* ic = card(vals);
    lv_obj_set_height(ic, lv_pct(100));
    lv_obj_set_flex_grow(ic, 2);
    lv_obj_set_flex_align(ic, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    indicator(ic, "Grzałka", &s_heaterDot, &s_heaterVal);
    indicator(ic, "Wentylator", &s_fanDot, &s_fanVal);
    s_fanBar = lv_bar_create(ic);
    lv_obj_set_size(s_fanBar, lv_pct(100), 10);
    lv_bar_set_range(s_fanBar, 0, 100);
    lv_obj_set_style_bg_color(s_fanBar, color(C_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_fanBar, color(C_P2), LV_PART_INDICATOR);

    // Controls 1: mode, mute, sensor fault reset
    lv_obj_t* ctl1 = box(g.content);
    lv_obj_set_size(ctl1, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(ctl1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctl1, PAD, 0);
    s_btnAuto = button(ctl1, "Auto", 130);
    lv_obj_add_event_cb(s_btnAuto, onAuto, LV_EVENT_CLICKED, nullptr);
    s_btnService = button(ctl1, "Serwis", 130);
    lv_obj_add_event_cb(s_btnService, onService, LV_EVENT_CLICKED, nullptr);
    s_btnMute = button(ctl1, "", 150, &s_btnMuteLabel);
    lv_obj_set_flex_grow(s_btnMute, 1);
    lv_obj_add_event_cb(s_btnMute, onMute, LV_EVENT_CLICKED, nullptr);
    s_btnReset = button(ctl1, LV_SYMBOL_WARNING "  Kasuj awarię", 250);
    lv_obj_add_event_cb(s_btnReset, onReset, LV_EVENT_CLICKED, nullptr);

    // Controls 2: manual heater / fan (Service Mode)
    lv_obj_t* ctl2 = box(g.content);
    lv_obj_set_size(ctl2, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(ctl2, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctl2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ctl2, PAD, 0);
    s_btnHeater = button(ctl2, "", 150, &s_btnHeaterLabel);
    lv_obj_set_flex_grow(s_btnHeater, 1);
    lv_obj_add_event_cb(s_btnHeater, onHeater, LV_EVENT_CLICKED, nullptr);
    s_btnFan = button(ctl2, "", 150, &s_btnFanLabel);
    lv_obj_set_flex_grow(s_btnFan, 1);
    lv_obj_add_event_cb(s_btnFan, onFan, LV_EVENT_CLICKED, nullptr);
    s_btnFanDown = button(ctl2, LV_SYMBOL_DOWN, TOUCH_H);
    lv_obj_add_event_cb(s_btnFanDown, onFanStep, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    s_fanStepVal = label(ctl2, fonts().title, C_TEXT, "");
    lv_obj_set_width(s_fanStepVal, 90);
    lv_obj_set_style_text_align(s_fanStepVal, LV_TEXT_ALIGN_CENTER, 0);
    s_btnFanUp = button(ctl2, LV_SYMBOL_UP, TOUCH_H);
    lv_obj_add_event_cb(s_btnFanUp, onFanStep, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    s_manualHint = label(ctl2, fonts().body, C_TEXT, "");
    lv_obj_add_flag(s_manualHint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_manualHint, color(C_BG), 0);
    lv_obj_set_style_bg_opa(s_manualHint, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(s_manualHint, 12, 0);
    lv_obj_set_style_pad_ver(s_manualHint, 6, 0);
    lv_obj_set_style_radius(s_manualHint, 6, 0);

    refresh();
}

void refresh() {
    refreshSummary();
    refreshValues();
    refreshControls();
    // Hint over the manual row: label width changes with the text
    lv_obj_center(s_manualHint);
    if (lv_label_get_text(s_manualHint)[0]) lv_obj_remove_flag(s_manualHint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_manualHint, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace thermo
}  // namespace gui

// L2 top-off node (docs/PLAN_DOLEWKA.md): summary (status, first alarm, process state), reservoir bar
// with the reserve / alarm markers, 24 h volume vs the daily limit; controls: Auto / Serwis (no
// confirmation), daily limit (dialog), "Kasuj alarm" (dialog), manual portions: bargraph 1–10 moved
// with the finger + ON (no confirmation), STOP during the session.
// Every control shows "sent" right after the tap (accent outline) and takes its new state only from
// the node's status / ACK (TopoffView::pending). Without TopoffView::controls (panel before stage D5)
// the screen is read only: controls greyed out.

#include <stdio.h>
#include <string.h>

#include <initializer_list>

#include "gui_internal.h"

namespace gui {
namespace topoff {

namespace {
lv_obj_t* s_status = nullptr;
lv_obj_t* s_alarm = nullptr;
lv_obj_t* s_state = nullptr;
lv_obj_t* s_resPct = nullptr;
lv_obj_t* s_resTrack = nullptr;
// Reservoir bar in three coloured parts: alarm (red) / reserve (yellow) / OK (green); each part has a
// dim background and a bright fill up to the current level
constexpr int RES_PARTS = 3;
const uint32_t RES_PART_COL[RES_PARTS] = {C_P1, C_P3, C_OK};
lv_obj_t* s_resPartBg[RES_PARTS] = {};
lv_obj_t* s_resPartFill[RES_PARTS] = {};
lv_obj_t* s_resInfo = nullptr;
lv_obj_t* s_resNote = nullptr;
lv_obj_t* s_vol = nullptr;
lv_obj_t* s_volLimit = nullptr;
lv_obj_t* s_volBar = nullptr;
lv_obj_t* s_btnAuto = nullptr;
lv_obj_t* s_btnService = nullptr;
lv_obj_t* s_btnLimit = nullptr;
lv_obj_t* s_btnLimitLabel = nullptr;
lv_obj_t* s_btnReset = nullptr;
lv_obj_t* s_bar = nullptr;
lv_obj_t* s_seg[TOPOFF_PORTIONS_MAX] = {};
lv_obj_t* s_segLabel[TOPOFF_PORTIONS_MAX] = {};
lv_obj_t* s_barHint = nullptr;
lv_obj_t* s_btnOn = nullptr;
lv_obj_t* s_btnOnTitle = nullptr;
lv_obj_t* s_btnOnSub = nullptr;
lv_obj_t* s_limitValue = nullptr;

int s_sel = 1;              // portions chosen on the bargraph
uint16_t s_limitEdit = 0;   // value in the limit dialog

constexpr int32_t RES_TRACK_H = 24;
constexpr int32_t SUMMARY_H = 48;

const TopoffView& tv() { return g.m->topoff; }

bool online() { return tv().present && tv().online; }

bool busy() { return tv().pending != TopoffPending::None; }

// Commands can be sent: node online and the adapter supports them
bool ctl() { return online() && tv().controls; }

// Node refuses manual portions in Auto and with an empty reservoir (LOW_RESERVOIR) — panel shows why
bool portionsEnabled() {
    return ctl() && !tv().autoMode && !busy() && !(tv().alarms & TOPOFF_AL_LOW_RESERVOIR);
}

uint16_t limitMin() {
    uint32_t m = 2u * tv().dose;
    if (m < TOPOFF_LIMIT_MIN) m = TOPOFF_LIMIT_MIN;
    return (uint16_t)((m + TOPOFF_LIMIT_STEP - 1) / TOPOFF_LIMIT_STEP * TOPOFF_LIMIT_STEP);
}

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

// Polish plural: 1 dolewka, 2–4 dolewki, 5+ dolewek (12–14 dolewek)
const char* portionsWord(unsigned n) {
    if (n == 1) return "dolewka";
    unsigned d = n % 10, t = n % 100;
    return (d >= 2 && d <= 4 && (t < 12 || t > 14)) ? "dolewki" : "dolewek";
}

void formatMinutes(uint16_t min, char* buf, size_t n) {
    if (min >= 60) snprintf(buf, n, "%u h %u min", min / 60, min % 60);
    else snprintf(buf, n, "%u min", min);
}

// Plain card: surface background, column layout
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

// ---- controls ----

void onAuto(lv_event_t*) {
    if (!ctl() || busy() || tv().autoMode) return;
    g.cmd->topoffSetAuto(true);
}

void onService(lv_event_t*) {
    if (!ctl() || busy() || !tv().autoMode) return;
    g.cmd->topoffSetAuto(false);
}

void onOn(lv_event_t*) {
    if (!ctl() || busy()) return;
    if (tv().manualSession) {
        g.cmd->topoffStop();  // STOP: safety 0
        return;
    }
    if (tv().autoMode) return;
    g.cmd->topoffPortions((uint8_t)s_sel);  // bargraph choice is deliberate — no dialog (2026-11-14)
}

void resetOk() {
    if (ctl() && !busy()) g.cmd->topoffReset();
}

void onReset(lv_event_t*) {
    if (!ctl() || busy() || !(tv().alarms & TOPOFF_AL_RESETTABLE)) return;
    char text[400] = "";
    for (int i = 0; i < g.m->alarmCount; i++) {
        const Alarm& a = g.m->alarms[i];
        if (a.target != Screen::Topoff || a.prio != Sev::P1) continue;
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, "• %s\n", a.text);
    }
    size_t n = strlen(text);
    snprintf(text + n, sizeof(text) - n,
             "\nKasuj dopiero po usunięciu przyczyny. Alarm zbiornika zostaje, dopóki zbiornik nie jest "
             "uzupełniony.");
    confirm("Kasować alarm dolewki?", text, "Kasuj", resetOk);
}

void limitShow() {
    if (s_limitValue) lv_label_set_text_fmt(s_limitValue, "%u ml", s_limitEdit);
}

void limitStep(lv_event_t* e) {
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    int32_t v = (int32_t)s_limitEdit + dir * (int32_t)TOPOFF_LIMIT_STEP;
    if (v < limitMin()) v = limitMin();
    if (v > TOPOFF_LIMIT_MAX) v = TOPOFF_LIMIT_MAX;
    s_limitEdit = (uint16_t)v;
    limitShow();
}

void limitOk() {
    s_limitValue = nullptr;
    if (ctl() && !busy() && s_limitEdit != tv().dailyLimit) g.cmd->topoffSetLimit(s_limitEdit);
}

void onLimit(lv_event_t*) {
    if (!ctl() || busy()) return;
    s_limitEdit = tv().dailyLimit;
    lv_obj_t* c = dialog("Limit dobowy dolewki", "Zapisz", limitOk);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 12, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    char info[96];
    snprintf(info, sizeof(info), "Dolano w 24 h: %u ml · zakres %u–%u ml", tv().volume24h, limitMin(),
             TOPOFF_LIMIT_MAX);
    lv_obj_t* il = label(c, fonts().small, C_MUTED, info);
    lv_obj_set_width(il, lv_pct(100));
    lv_obj_t* row = box(c);
    lv_obj_set_size(row, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t* minus = button(row, LV_SYMBOL_MINUS, 120);
    lv_obj_add_event_cb(minus, limitStep, LV_EVENT_CLICKED, (void*)(intptr_t)-1);
    s_limitValue = label(row, fonts().value, C_TEXT, "");
    lv_obj_t* plus = button(row, LV_SYMBOL_PLUS, 120);
    lv_obj_add_event_cb(plus, limitStep, LV_EVENT_CLICKED, (void*)(intptr_t)1);
    limitShow();
}

// Bargraph: press / drag anywhere on it selects 1..10
void onBar(lv_event_t* e) {
    if (!portionsEnabled() || tv().manualSession) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(s_bar, &a);
    int32_t w = lv_area_get_width(&a);
    if (w <= 0) return;
    int n = (int)((p.x - a.x1) * TOPOFF_PORTIONS_MAX / w) + 1;
    if (n < 1) n = 1;
    if (n > TOPOFF_PORTIONS_MAX) n = TOPOFF_PORTIONS_MAX;
    if (n != s_sel) {
        s_sel = n;
        refresh();
    }
}

void refreshBar() {
    const TopoffView& t = tv();
    bool session = online() && t.manualSession;
    bool enabled = portionsEnabled();
    for (int i = 0; i < TOPOFF_PORTIONS_MAX; i++) {
        uint32_t bg = C_SURFACE2, fg = C_MUTED;
        if (session) {
            if (i < t.manualDone) bg = C_OK, fg = 0x101418;
            else if (i < t.manualDone + t.manualLeft) bg = i == t.manualDone ? (uint32_t)C_ACCENT : 0x2A4A78u, fg = C_TEXT;
        } else if (enabled && i < s_sel) {
            // Beyond the reserve above the alarm level: warning colour
            bg = (i >= t.resPortions) ? C_P3 : C_ACCENT;
            fg = (i >= t.resPortions) ? 0x101418u : (uint32_t)C_TEXT;
        }
        lv_obj_set_style_bg_color(s_seg[i], color(bg), 0);
        lv_obj_set_style_bg_opa(s_seg[i], enabled || session ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_text_color(s_segLabel[i], color(fg), 0);
    }

    if (!online()) lv_label_set_text(s_barHint, "");
    else if (!t.controls && !session) lv_label_set_text(s_barHint, "Porcje ręczne na razie w web GUI dolewki");
    else if (t.autoMode) lv_label_set_text(s_barHint, "Porcje ręczne tylko w trybie Serwis");
    else if (!session && (t.alarms & TOPOFF_AL_LOW_RESERVOIR))
        lv_label_set_text(s_barHint, "Zbiornik RO pusty — porcje zablokowane");
    else lv_label_set_text(s_barHint, "");

    // ON / STOP
    if (session) {
        lv_obj_set_style_bg_color(s_btnOn, color(C_P1), 0);
        lv_label_set_text(s_btnOnTitle, "STOP");
        lv_label_set_text_fmt(s_btnOnSub, "porcja %u z %u", t.manualDone + 1, t.manualDone + t.manualLeft);
    } else {
        lv_obj_set_style_bg_color(s_btnOn, color(enabled ? C_OK : C_SURFACE2), 0);
        lv_label_set_text(s_btnOnTitle, "ON");
        lv_label_set_text_fmt(s_btnOnSub, "%d × %u = %u ml", s_sel, t.dose, (unsigned)(s_sel * t.dose));
    }
    bool pendingOn = t.pending == TopoffPending::Portions || t.pending == TopoffPending::Stop;
    setSent(s_btnOn, pendingOn);
    setEnabled(s_btnOn, ctl() && (session || enabled) && !(busy() && !pendingOn));
}

void refreshReservoir() {
    const TopoffView& t = tv();
    bool known = online() && t.resZone != TZ_UNKNOWN;
    uint32_t col = C_OFFLINE;
    if (known) col = t.resZone == TZ_OK ? C_OK : t.resZone == TZ_RESERVE ? C_P3 : C_P1;
    lv_label_set_text_fmt(s_resPct, known ? "%u %%" : "– %%", t.resPct);
    lv_obj_set_style_text_color(s_resPct, color(col), 0);

    lv_obj_update_layout(s_resTrack);
    int32_t w = lv_obj_get_content_width(s_resTrack);
    int32_t level = known ? w * (t.resPct > 100 ? 100 : t.resPct) / 100 : 0;
    const int32_t edge[RES_PARTS + 1] = {0, w * t.resAlarmPct / 100, w * t.resReservePct / 100, w};
    for (int i = 0; i < RES_PARTS; i++) {
        int32_t x0 = edge[i], x1 = edge[i + 1] > x0 ? edge[i + 1] : x0;
        lv_obj_set_pos(s_resPartBg[i], x0, 0);
        lv_obj_set_width(s_resPartBg[i], x1 - x0);
        int32_t f = level <= x0 ? 0 : (level >= x1 ? x1 - x0 : level - x0);
        lv_obj_set_pos(s_resPartFill[i], x0, 0);
        lv_obj_set_width(s_resPartFill[i], f);
        if (f > 0) lv_obj_remove_flag(s_resPartFill[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_resPartFill[i], LV_OBJ_FLAG_HIDDEN);
        // Estimate (no ToF reading): lower opacity of the fill
        lv_obj_set_style_bg_opa(s_resPartFill[i], t.resEstimated ? LV_OPA_50 : LV_OPA_COVER, 0);
    }

    char buf[96];
    if (!known) {
        snprintf(buf, sizeof(buf), online() ? "Poziom nieznany" : "–");
    } else if (t.resPortions == 0) {
        snprintf(buf, sizeof(buf), "Zapas: brak — poziom alarmowy");
    } else if (t.resHoursX10 >= 0) {
        snprintf(buf, sizeof(buf), "Zapas: %u %s · ~%d h", t.resPortions, portionsWord(t.resPortions),
                 (t.resHoursX10 + 5) / 10);
    } else {
        snprintf(buf, sizeof(buf), "Zapas: %u %s", t.resPortions, portionsWord(t.resPortions));
    }
    lv_label_set_text(s_resInfo, buf);
    lv_obj_set_style_text_color(s_resInfo, color(known && t.resPortions == 0 ? C_P1 : C_TEXT), 0);
    // Header note: estimate warning, otherwise the marker legend
    if (known && t.resEstimated) snprintf(buf, sizeof(buf), "szacunek (brak odczytu)");
    else snprintf(buf, sizeof(buf), "rez. %u %% · alarm %u %%", t.resReservePct, t.resAlarmPct);
    lv_label_set_text(s_resNote, buf);
    lv_obj_set_style_text_color(s_resNote, color(known && t.resEstimated ? C_P3 : C_MUTED), 0);
}

void refreshVolume() {
    const TopoffView& t = tv();
    if (!online()) {
        lv_label_set_text(s_vol, "– ml");
        lv_label_set_text(s_volLimit, "");
        lv_bar_set_value(s_volBar, 0, LV_ANIM_OFF);
        return;
    }
    lv_label_set_text_fmt(s_vol, "%u ml", t.volume24h);
    lv_label_set_text_fmt(s_volLimit, "limit %u", t.dailyLimit);
    int32_t pct = t.dailyLimit ? (int32_t)t.volume24h * 100 / t.dailyLimit : 0;
    if (pct > 100) pct = 100;
    lv_bar_set_value(s_volBar, pct, LV_ANIM_OFF);
    bool atLimit = t.alarms & TOPOFF_AL_DAILY_LIMIT;
    lv_obj_set_style_bg_color(s_volBar, color(atLimit ? C_P1 : pct >= 80 ? C_P3 : C_ACCENT), LV_PART_INDICATOR);
}

void refreshSummary() {
    const TopoffView& t = tv();
    char buf[96];
    if (!t.present) {
        lv_label_set_text(s_status, "Brak danych");
        lv_obj_set_style_text_color(s_status, color(C_OFFLINE), 0);
        lv_label_set_text(s_alarm, "czekam na status dolewki");
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
    Sev sev = topoffSev();
    lv_label_set_text(s_status, statusName(t.status));
    lv_obj_set_style_text_color(s_status, color(t.status == TS_SERVICE && sev == Sev::Ok ? C_ACCENT : sevColor(sev)),
                                0);

    // First alarm of this node (list is sorted by priority)
    const Alarm* first = nullptr;
    for (int i = 0; i < g.m->alarmCount && !first; i++)
        if (g.m->alarms[i].target == Screen::Topoff) first = &g.m->alarms[i];
    lv_label_set_text(s_alarm, first ? first->text : "");
    lv_obj_set_style_text_color(s_alarm, color(first ? sevColor(first->prio) : C_MUTED), 0);

    if (t.manualSession) {
        snprintf(buf, sizeof(buf), "Porcje ręczne: %u z %u", t.manualDone + (t.pumpOn ? 1 : 0),
                 t.manualDone + t.manualLeft);
    } else {
        switch (t.state) {
            case TA_DEBOUNCE: snprintf(buf, sizeof(buf), "Pływak: potwierdzanie"); break;
            case TA_PUMPING: snprintf(buf, sizeof(buf), "Pompuje · %u s", t.phaseRemainingS); break;
            case TA_LOGGING: snprintf(buf, sizeof(buf), "Zapis cyklu"); break;
            case TA_ERROR: snprintf(buf, sizeof(buf), "Dolewanie zablokowane"); break;
            default:
                if (!t.autoMode && t.serviceLeftMin) {
                    char m[24];
                    formatMinutes(t.serviceLeftMin, m, sizeof(m));
                    snprintf(buf, sizeof(buf), "Auto za %s", m);
                } else {
                    snprintf(buf, sizeof(buf), t.autoMode ? "Czuwa" : "Serwis");
                }
        }
    }
    lv_label_set_text(s_state, buf);
}

void refreshControls() {
    const TopoffView& t = tv();
    bool on = online();
    setChecked(s_btnAuto, on && t.autoMode);
    setChecked(s_btnService, on && !t.autoMode);
    on = ctl();
    bool pendingMode = t.pending == TopoffPending::Mode;
    setSent(s_btnAuto, pendingMode && t.pendingAuto);
    setSent(s_btnService, pendingMode && !t.pendingAuto);
    setEnabled(s_btnAuto, on && (!busy() || pendingMode));
    setEnabled(s_btnService, on && (!busy() || pendingMode));

    if (online()) lv_label_set_text_fmt(s_btnLimitLabel, "Limit %u ml…", t.dailyLimit);
    else lv_label_set_text(s_btnLimitLabel, "Limit…");
    bool pendingLimit = t.pending == TopoffPending::Limit;
    setSent(s_btnLimit, pendingLimit);
    setEnabled(s_btnLimit, on && (!busy() || pendingLimit));

    bool resettable = on && (t.alarms & TOPOFF_AL_RESETTABLE);
    bool pendingReset = t.pending == TopoffPending::Reset;
    bool latched = online() && (t.alarms & TOPOFF_AL_RESETTABLE);  // red also read only: alarm is there
    lv_obj_set_style_bg_color(s_btnReset, color(latched ? C_P1 : C_SURFACE2), 0);
    setSent(s_btnReset, pendingReset);
    setEnabled(s_btnReset, resettable && (!busy() || pendingReset));
}
}  // namespace

void build() {
    lv_obj_set_flex_flow(g.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g.content, PAD, 0);

    // Summary: status · first alarm · process state
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

    // Values: reservoir + 24 h volume
    lv_obj_t* vals = box(g.content);
    lv_obj_set_width(vals, lv_pct(100));
    lv_obj_set_flex_grow(vals, 1);
    lv_obj_set_flex_flow(vals, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(vals, PAD, 0);

    lv_obj_t* res = card(vals);
    lv_obj_set_height(res, lv_pct(100));
    lv_obj_set_flex_grow(res, 3);
    // Head: title, marker legend / estimate note, fill % on the right
    lv_obj_t* resHead = box(res);
    lv_obj_set_size(resHead, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(resHead, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(resHead, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(resHead, 12, 0);
    label(resHead, fonts().body, C_MUTED, "Zbiornik RO");
    s_resNote = label(resHead, fonts().small, C_MUTED, "");
    lv_obj_set_flex_grow(s_resNote, 1);
    s_resPct = label(resHead, fonts().title, C_OK, "");
    s_resTrack = lv_obj_create(res);
    lv_obj_set_size(s_resTrack, lv_pct(100), RES_TRACK_H);
    lv_obj_set_style_bg_color(s_resTrack, color(C_SURFACE2), 0);
    lv_obj_set_style_border_width(s_resTrack, 0, 0);
    lv_obj_set_style_pad_all(s_resTrack, 0, 0);
    lv_obj_set_style_radius(s_resTrack, 6, 0);
    lv_obj_set_style_clip_corner(s_resTrack, true, 0);
    lv_obj_remove_flag(s_resTrack, LV_OBJ_FLAG_SCROLLABLE);
    for (int pass = 0; pass < 2; pass++) {  // backgrounds first, fills on top
        for (int i = 0; i < RES_PARTS; i++) {
            lv_obj_t* o = lv_obj_create(s_resTrack);
            lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
            lv_obj_set_size(o, 0, lv_pct(100));
            lv_obj_set_style_border_width(o, 0, 0);
            lv_obj_set_style_radius(o, 0, 0);
            lv_color_t c = color(RES_PART_COL[i]);
            lv_obj_set_style_bg_color(o, pass ? c : lv_color_mix(c, color(C_SURFACE2), 70), 0);
            (pass ? s_resPartFill : s_resPartBg)[i] = o;
        }
    }
    s_resInfo = label(res, fonts().body, C_TEXT, "");

    lv_obj_t* vol = card(vals);
    lv_obj_set_height(vol, lv_pct(100));
    lv_obj_set_flex_grow(vol, 2);
    lv_obj_t* volHead = box(vol);
    lv_obj_set_size(volHead, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_t* volTitle = label(volHead, fonts().body, C_MUTED, "Dolano w 24 h");
    lv_obj_align(volTitle, LV_ALIGN_LEFT_MID, 0, 0);
    s_volLimit = label(volHead, fonts().small, C_MUTED, "");
    lv_obj_align(s_volLimit, LV_ALIGN_RIGHT_MID, 0, 0);
    s_vol = label(vol, fonts().value, C_TEXT, "");
    s_volBar = lv_bar_create(vol);
    lv_obj_set_size(s_volBar, lv_pct(100), 10);
    lv_bar_set_range(s_volBar, 0, 100);
    lv_obj_set_style_bg_color(s_volBar, color(C_SURFACE2), LV_PART_MAIN);

    // Controls 1: mode, daily limit, alarm reset
    lv_obj_t* ctl1 = box(g.content);
    lv_obj_set_size(ctl1, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(ctl1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctl1, PAD, 0);
    s_btnAuto = button(ctl1, "Auto", 130);
    lv_obj_add_event_cb(s_btnAuto, onAuto, LV_EVENT_CLICKED, nullptr);
    s_btnService = button(ctl1, "Serwis", 130);
    lv_obj_add_event_cb(s_btnService, onService, LV_EVENT_CLICKED, nullptr);
    s_btnLimit = button(ctl1, "", 150, &s_btnLimitLabel);
    lv_obj_set_flex_grow(s_btnLimit, 1);
    lv_obj_add_event_cb(s_btnLimit, onLimit, LV_EVENT_CLICKED, nullptr);
    s_btnReset = button(ctl1, LV_SYMBOL_BELL "  Kasuj alarm", 230);
    lv_obj_add_event_cb(s_btnReset, onReset, LV_EVENT_CLICKED, nullptr);

    // Controls 2: portions bargraph + ON / STOP
    lv_obj_t* ctl2 = box(g.content);
    lv_obj_set_size(ctl2, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(ctl2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctl2, PAD, 0);
    s_bar = box(ctl2);
    lv_obj_set_height(s_bar, TOUCH_H);
    lv_obj_set_flex_grow(s_bar, 1);
    lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_bar, 4, 0);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_bar, onBar, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_bar, onBar, LV_EVENT_PRESSING, nullptr);
    for (int i = 0; i < TOPOFF_PORTIONS_MAX; i++) {
        lv_obj_t* sgm = lv_obj_create(s_bar);
        lv_obj_set_height(sgm, lv_pct(100));
        lv_obj_set_flex_grow(sgm, 1);
        lv_obj_set_style_border_width(sgm, 0, 0);
        lv_obj_set_style_radius(sgm, 4, 0);
        lv_obj_set_style_pad_all(sgm, 0, 0);
        lv_obj_remove_flag(sgm, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
        char n[4];
        snprintf(n, sizeof(n), "%d", i + 1);
        s_segLabel[i] = label(sgm, fonts().title, C_MUTED, n);
        lv_obj_center(s_segLabel[i]);
        s_seg[i] = sgm;
    }
    s_barHint = label(s_bar, fonts().body, C_TEXT, "");
    lv_obj_add_flag(s_barHint, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_color(s_barHint, color(C_BG), 0);
    lv_obj_set_style_bg_opa(s_barHint, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(s_barHint, 12, 0);
    lv_obj_set_style_pad_ver(s_barHint, 6, 0);
    lv_obj_set_style_radius(s_barHint, 6, 0);
    lv_obj_center(s_barHint);

    s_btnOn = lv_button_create(ctl2);
    lv_obj_set_size(s_btnOn, 170, TOUCH_H);
    lv_obj_set_style_shadow_width(s_btnOn, 0, 0);
    lv_obj_set_style_border_color(s_btnOn, color(C_ACCENT), 0);
    lv_obj_set_flex_flow(s_btnOn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_btnOn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_btnOn, 0, 0);
    s_btnOnTitle = label(s_btnOn, fonts().title, C_TEXT, "ON");
    s_btnOnSub = label(s_btnOn, fonts().small, C_TEXT, "");
    lv_obj_add_event_cb(s_btnOn, onOn, LV_EVENT_CLICKED, nullptr);

    refresh();
}

void refresh() {
    // Hint over the bargraph: label width changes with the text
    refreshSummary();
    refreshReservoir();
    refreshVolume();
    refreshControls();
    refreshBar();
    lv_obj_center(s_barHint);
    if (lv_label_get_text(s_barHint)[0]) lv_obj_remove_flag(s_barHint, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_barHint, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace topoff
}  // namespace gui

#include <stdio.h>
#include <stdlib.h>

#include <initializer_list>

#include "gui_internal.h"

namespace gui {

Ctx g;

namespace {
constexpr uint32_t SCREENSAVER_MS = 60000;
constexpr uint32_t SAVER_MOVE_MS = 60000;
constexpr uint32_t TOAST_MS = 3000;

uint32_t s_seenVersion = 0xFFFFFFFF;
Screen s_prevScreen = Screen::Overview;  // back target of the alarm list

lv_obj_t* s_back = nullptr;
lv_obj_t* s_title = nullptr;
lv_obj_t* s_clock = nullptr;
lv_obj_t* s_wifi = nullptr;
lv_obj_t* s_alarmBtn = nullptr;
lv_obj_t* s_alarmBtnLabel = nullptr;
lv_obj_t* s_strip = nullptr;
lv_obj_t* s_stripLabel = nullptr;
lv_obj_t* s_toast = nullptr;
lv_obj_t* s_saver = nullptr;
lv_obj_t* s_saverBox = nullptr;
lv_obj_t* s_saverClock = nullptr;
lv_obj_t* s_saverInfo = nullptr;
lv_obj_t* s_saverDots = nullptr;
lv_obj_t* s_confirm = nullptr;
void (*s_confirmOk)() = nullptr;

const char* screenTitle(Screen s) {
    switch (s) {
        case Screen::Overview: return "Przegląd";
        case Screen::Lamps: return "Lampy";
        case Screen::Programs: return "Program lamp";
        case Screen::Alarms: return "Alarmy";
        case Screen::Topoff: return "Dolewka";
    }
    return "";
}

void clockText(char* buf, size_t n, bool seconds) {
    if (!g.m->timeValid) {
        snprintf(buf, n, "--:--");
        return;
    }
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(buf, n, seconds ? "%H:%M:%S" : "%H:%M", &lt);
}

void refreshShell() {
    char buf[160];  // fits node + text + age + suffix
    clockText(buf, sizeof(buf), false);
    lv_label_set_text(s_clock, buf);

    if (g.m->wifi) {
        lv_label_set_text(s_wifi, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(s_wifi, color(C_TEXT), 0);
    } else {
        lv_label_set_text(s_wifi, LV_SYMBOL_WIFI " " LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_color(s_wifi, color(C_P2), 0);
    }

    // Alarm badge: count, colour of the highest priority
    int n = g.m->alarmCount;
    Sev top = n ? g.m->alarms[0].prio : Sev::Ok;
    lv_obj_set_style_bg_color(s_alarmBtn, color(n ? sevColor(top) : C_SURFACE2), 0);
    lv_obj_set_style_text_color(s_alarmBtnLabel, color(n ? sevTextColor(top) : C_OK), 0);
    lv_label_set_text_fmt(s_alarmBtnLabel, LV_SYMBOL_BELL " %d", n);

    // Alarm strip only while alarms are active, and not over the alarm list itself
    if (n == 0 || g.screen == Screen::Alarms) {
        lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
    } else {
        const Alarm& a = g.m->alarms[0];
        char ago[24];
        formatAgo(a.since, ago, sizeof(ago));
        lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_strip, color(sevColor(a.prio)), 0);
        lv_obj_set_style_text_color(s_stripLabel, color(sevTextColor(a.prio)), 0);
        if (n > 1)
            snprintf(buf, sizeof(buf), "%s  %s: %s (%s)   +%d  " LV_SYMBOL_RIGHT, sevName(a.prio), a.node, a.text, ago,
                     n - 1);
        else
            snprintf(buf, sizeof(buf), "%s  %s: %s (%s)  " LV_SYMBOL_RIGHT, sevName(a.prio), a.node, a.text, ago);
        lv_label_set_text(s_stripLabel, buf);
    }
}

void refreshScreen() {
    switch (g.screen) {
        case Screen::Overview: overview::refresh(); break;
        case Screen::Lamps: lamps::refresh(); break;
        case Screen::Programs: programs::refresh(); break;
        case Screen::Alarms: alarms::refresh(); break;
        case Screen::Topoff: topoff::refresh(); break;
    }
}

void goBack() {
    switch (g.screen) {
        case Screen::Lamps: navigate(Screen::Overview); break;
        case Screen::Topoff: navigate(Screen::Overview); break;
        case Screen::Programs: navigate(Screen::Lamps); break;
        case Screen::Alarms: navigate(s_prevScreen == Screen::Alarms ? Screen::Overview : s_prevScreen); break;
        default: break;
    }
}

void openAlarms() {
    if (g.screen != Screen::Alarms) navigate(Screen::Alarms);
}

// ---- screensaver ----

void saverRefresh() {
    if (!s_saver) return;
    char buf[32];
    clockText(buf, sizeof(buf), false);
    lv_label_set_text(s_saverClock, buf);
    int n = g.m->alarmCount;
    if (n) {
        lv_label_set_text_fmt(s_saverInfo, LV_SYMBOL_BELL " %d", n);
        lv_obj_set_style_text_color(s_saverInfo, color(sevColor(g.m->alarms[0].prio)), 0);
    } else {
        lv_label_set_text(s_saverInfo, "");
    }
    // One dot per tile: lamps first, then the other nodes
    lv_obj_clean(s_saverDots);
    int count = 1 + g.m->tileCount;
    for (int i = 0; i < count; i++) {
        Sev s = i == 0 ? lampGroupSev() : g.m->tiles[i - 1].status;
        lv_obj_t* d = lv_obj_create(s_saverDots);
        lv_obj_set_size(d, 14, 14);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, color(sevColor(s)), 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
    }
}

void saverMove() {
    if (!s_saverBox) return;
    // Burn-in: random position every minute
    lv_obj_update_layout(s_saverBox);
    int32_t w = lv_obj_get_width(s_saverBox), h = lv_obj_get_height(s_saverBox);
    int32_t x = rand() % (SCREEN_W - w > 0 ? SCREEN_W - w : 1);
    int32_t y = rand() % (SCREEN_H - h > 0 ? SCREEN_H - h : 1);
    lv_obj_set_pos(s_saverBox, x, y);
}

void hideScreensaver() {
    if (s_saver) lv_obj_delete(s_saver);
    s_saver = nullptr;
    s_saverBox = nullptr;
    lv_display_trigger_activity(nullptr);
}

void toastDeleted(lv_event_t* e) {
    if (lv_event_get_target(e) == s_toast) s_toast = nullptr;
}

void secondTick(lv_timer_t*) {
    refreshShell();
    refreshScreen();
    saverRefresh();
}

void saverMoveTick(lv_timer_t*) { saverMove(); }

lv_obj_t* topBar(lv_obj_t* scr) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, lv_pct(100), TOP_H);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, color(C_SURFACE), 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_back = lv_button_create(bar);
    lv_obj_set_size(s_back, 72, TOP_H);
    lv_obj_align(s_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(s_back, 0, 0);
    lv_obj_t* bl = label(s_back, fonts().title, C_TEXT, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(s_back, [](lv_event_t*) { goBack(); }, LV_EVENT_CLICKED, nullptr);

    s_title = label(bar, fonts().title, C_TEXT, "");
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 16, 0);

    s_clock = label(bar, fonts().title, C_TEXT, "--:--");
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    s_alarmBtn = lv_button_create(bar);
    lv_obj_set_size(s_alarmBtn, 96, TOP_H - 12);
    lv_obj_align(s_alarmBtn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_shadow_width(s_alarmBtn, 0, 0);
    lv_obj_set_style_radius(s_alarmBtn, (TOP_H - 12) / 2, 0);
    s_alarmBtnLabel = label(s_alarmBtn, fonts().body, C_OK, "");
    lv_obj_center(s_alarmBtnLabel);
    lv_obj_add_event_cb(s_alarmBtn, [](lv_event_t*) { openAlarms(); }, LV_EVENT_CLICKED, nullptr);

    s_wifi = label(bar, fonts().body, C_TEXT, LV_SYMBOL_WIFI);
    lv_obj_align_to(s_wifi, s_alarmBtn, LV_ALIGN_OUT_LEFT_MID, -16, 0);
    return bar;
}
}  // namespace

// ---- helpers shared with the screens ----

uint32_t sevColor(Sev s) {
    switch (s) {
        case Sev::Ok: return C_OK;
        case Sev::P3: return C_P3;
        case Sev::P2: return C_P2;
        case Sev::P1: return C_P1;
        case Sev::Offline: return C_OFFLINE;
    }
    return C_OFFLINE;
}

uint32_t sevTextColor(Sev s) { return (s == Sev::P3 || s == Sev::P2) ? 0x101418 : 0xFFFFFF; }

const char* sevName(Sev s) {
    switch (s) {
        case Sev::Ok: return "OK";
        case Sev::P3: return "P3";
        case Sev::P2: return "P2";
        case Sev::P1: return "P1";
        case Sev::Offline: return "offline";
    }
    return "?";
}

const char* sevSymbol(Sev s) {
    switch (s) {
        case Sev::Ok: return LV_SYMBOL_OK;
        case Sev::Offline: return LV_SYMBOL_CLOSE;
        default: return LV_SYMBOL_WARNING;
    }
}

const char* modeName(uint8_t mode) {
    switch (mode) {
        case MODE_AUTO: return "Auto";
        case MODE_NIGHT: return "Noc";
        case MODE_TEST: return "Test";
    }
    return "?";
}

void formatAgo(time_t t, char* buf, size_t n) {
    long s = t ? (long)(time(nullptr) - t) : -1;
    if (s < 0) snprintf(buf, n, "-");
    else if (s < 60) snprintf(buf, n, "%ld s", s);
    else if (s < 3600) snprintf(buf, n, "%ld min", s / 60);
    else snprintf(buf, n, "%ld h %ld min", s / 3600, s / 60 % 60);
}

Sev lampGroupSev() {
    if (g.m->lamps.count == 0 || lampsOnline() == 0) return Sev::Offline;
    Sev worst = Sev::Ok;
    for (int i = 0; i < g.m->alarmCount; i++) {
        const Alarm& a = g.m->alarms[i];
        if (a.target == Screen::Lamps && (uint8_t)a.prio > (uint8_t)worst) worst = a.prio;
    }
    return worst;
}

Sev topoffSev() {
    if (!g.m->topoff.present || !g.m->topoff.online) return Sev::Offline;
    Sev worst = Sev::Ok;
    for (int i = 0; i < g.m->alarmCount; i++) {
        const Alarm& a = g.m->alarms[i];
        if (a.target == Screen::Topoff && (uint8_t)a.prio > (uint8_t)worst) worst = a.prio;
    }
    return worst;
}

int lampsOnline() {
    int n = 0;
    for (int i = 0; i < g.m->lamps.count; i++)
        if (g.m->lamps.lamps[i].online) n++;
    return n;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t col, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color(col), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t* box(lv_obj_t* parent) {
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

void confirm(const char* title, const char* text, const char* okLabel, void (*onOk)()) {
    dialog(title, okLabel, onOk);
    lv_msgbox_add_text(s_confirm, text);
}

lv_obj_t* dialog(const char* title, const char* okLabel, void (*onOk)()) {
    if (s_confirm) lv_msgbox_close(s_confirm);
    s_confirmOk = onOk;
    s_confirm = lv_msgbox_create(nullptr);  // modal, on the top layer
    lv_obj_set_width(s_confirm, 560);
    lv_obj_t* t = lv_msgbox_add_title(s_confirm, title);
    lv_obj_set_style_text_font(t, fonts().title, 0);
    lv_obj_t* cancel = lv_msgbox_add_footer_button(s_confirm, "Anuluj");
    lv_obj_t* ok = lv_msgbox_add_footer_button(s_confirm, okLabel);
    for (lv_obj_t* b : {cancel, ok}) {
        lv_obj_set_height(b, TOUCH_H);
        lv_obj_set_flex_grow(b, 1);
    }
    lv_obj_set_style_bg_color(cancel, color(C_SURFACE2), 0);
    // The default theme gives header/footer a fixed small height: buttons would be clipped to it
    lv_obj_t* header = lv_msgbox_get_header(s_confirm);
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(header, 16, 0);
    lv_obj_set_style_pad_all(lv_msgbox_get_content(s_confirm), 16, 0);
    lv_obj_t* footer = lv_msgbox_get_footer(s_confirm);
    lv_obj_set_height(footer, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(footer, 12, 0);
    lv_obj_set_style_pad_column(footer, 12, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t*) {
        lv_msgbox_close(s_confirm);
        s_confirm = nullptr;
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ok, [](lv_event_t*) {
        lv_msgbox_close(s_confirm);
        s_confirm = nullptr;
        if (s_confirmOk) s_confirmOk();
    }, LV_EVENT_CLICKED, nullptr);
    return lv_msgbox_get_content(s_confirm);
}

void toast(const char* text, Sev sev) {
    if (s_toast) lv_obj_delete(s_toast);
    s_toast = lv_label_create(lv_layer_top());
    lv_label_set_text(s_toast, text);
    lv_obj_set_style_text_font(s_toast, fonts().body, 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_toast, color(sev == Sev::Ok ? C_SURFACE2 : sevColor(sev)), 0);
    lv_obj_set_style_text_color(s_toast, color(sev == Sev::Ok ? C_TEXT : sevTextColor(sev)), 0);
    lv_obj_set_style_pad_hor(s_toast, 20, 0);
    lv_obj_set_style_pad_ver(s_toast, 12, 0);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_set_style_border_width(s_toast, 2, 0);
    lv_obj_set_style_border_color(s_toast, color(C_MUTED), 0);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, TOP_H + 8);  // bottom = action buttons
    lv_obj_add_event_cb(s_toast, toastDeleted, LV_EVENT_DELETE, nullptr);
    lv_obj_delete_delayed(s_toast, TOAST_MS);
}

void showScreensaver() {
    if (s_saver) return;
    if (s_confirm) {  // never leave a pending confirmation behind the saver
        lv_msgbox_close(s_confirm);
        s_confirm = nullptr;
    }
    s_saver = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_saver, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_saver, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_saver, 0, 0);
    lv_obj_set_style_radius(s_saver, 0, 0);
    lv_obj_remove_flag(s_saver, LV_OBJ_FLAG_SCROLLABLE);
    // The touch that wakes the screen ends here and is not passed to the UI underneath
    lv_obj_add_event_cb(s_saver, [](lv_event_t*) { hideScreensaver(); }, LV_EVENT_CLICKED, nullptr);

    s_saverBox = box(s_saver);
    lv_obj_set_size(s_saverBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_saverBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_saverBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_saverBox, 10, 0);
    lv_obj_remove_flag(s_saverBox, LV_OBJ_FLAG_CLICKABLE);
    s_saverClock = label(s_saverBox, fonts().value, 0x9AA5B1, "");
    s_saverDots = box(s_saverBox);
    lv_obj_set_size(s_saverDots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_saverDots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_saverDots, 10, 0);
    lv_obj_remove_flag(s_saverDots, LV_OBJ_FLAG_CLICKABLE);
    s_saverInfo = label(s_saverBox, fonts().body, C_MUTED, "");
    saverRefresh();
    saverMove();
}

void navigate(Screen s) {
    if (s == Screen::Alarms && g.screen != Screen::Alarms) s_prevScreen = g.screen;
    g.screen = s;
    lv_obj_clean(g.content);
    lv_obj_set_layout(g.content, LV_LAYOUT_NONE);  // each screen sets its own layout
    lv_label_set_text(s_title, screenTitle(s));
    if (s == Screen::Overview) {
        lv_obj_add_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 16, 0);
    } else {
        lv_obj_remove_flag(s_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 72, 0);
    }
    switch (s) {
        case Screen::Overview: overview::build(); break;
        case Screen::Lamps: lamps::build(); break;
        case Screen::Programs: programs::build(); break;
        case Screen::Alarms: alarms::build(); break;
        case Screen::Topoff: topoff::build(); break;
    }
    refreshShell();
}

void begin(Model* model, Commands* commands) {
    g.m = model;
    g.cmd = commands;

    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, color(C_BG), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    topBar(scr);

    s_strip = lv_button_create(scr);
    lv_obj_set_size(s_strip, lv_pct(100), STRIP_H);
    lv_obj_set_style_radius(s_strip, 0, 0);
    lv_obj_set_style_shadow_width(s_strip, 0, 0);
    s_stripLabel = label(s_strip, fonts().body, 0x101418, "");
    lv_obj_set_width(s_stripLabel, lv_pct(100));
    lv_label_set_long_mode(s_stripLabel, LV_LABEL_LONG_DOT);
    lv_obj_align(s_stripLabel, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(s_strip, [](lv_event_t*) { openAlarms(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_HIDDEN);

    g.content = box(scr);
    lv_obj_set_width(g.content, lv_pct(100));
    lv_obj_set_flex_grow(g.content, 1);
    lv_obj_set_style_pad_all(g.content, PAD, 0);

    navigate(Screen::Overview);
    refreshShell();
    lv_timer_create(secondTick, 1000, nullptr);
    lv_timer_create(saverMoveTick, SAVER_MOVE_MS, nullptr);
}

void update() {
    if (g.m->version != s_seenVersion) {
        s_seenVersion = g.m->version;
        refreshShell();
        refreshScreen();
        saverRefresh();
    }
    if (!s_saver && lv_display_get_inactive_time(nullptr) > SCREENSAVER_MS) showScreensaver();
}

}  // namespace gui

// Active alarms, highest priority first. A tap leads to the node / group screen.

#include <stdio.h>

#include "gui_internal.h"

namespace gui {
namespace alarms {

namespace {
lv_obj_t* s_list = nullptr;
lv_obj_t* s_since[MAX_ALARMS];
uint32_t s_builtVersion = 0;
int s_builtCount = -1;

void onRow(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= g.m->alarmCount) return;
    Screen t = g.m->alarms[i].target;
    if (t == Screen::Lamps) navigate(Screen::Lamps);
    else toast("Ekran tego węzła powstanie w kolejnych etapach");
}

void sinceText(const Alarm& a, char* buf, size_t n) {
    char ago[24];
    formatAgo(a.since, ago, sizeof(ago));
    struct tm lt;
    localtime_r(&a.since, &lt);
    char hm[8];
    strftime(hm, sizeof(hm), "%H:%M", &lt);
    snprintf(buf, n, "od %s (%s)", hm, ago);
}

void buildList() {
    lv_obj_clean(s_list);
    s_builtVersion = g.m->version;
    s_builtCount = g.m->alarmCount;
    if (g.m->alarmCount == 0) {
        lv_obj_t* b = box(s_list);
        lv_obj_set_size(b, lv_pct(100), 260);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 12, 0);
        label(b, fonts().value, C_OK, LV_SYMBOL_OK);
        label(b, fonts().title, C_TEXT, "Brak aktywnych alarmów");
        return;
    }
    for (int i = 0; i < g.m->alarmCount; i++) {
        const Alarm& a = g.m->alarms[i];
        lv_obj_t* r = lv_button_create(s_list);
        lv_obj_set_size(r, lv_pct(100), ROW_H);
        lv_obj_set_style_bg_color(r, color(C_SURFACE), 0);
        lv_obj_set_style_shadow_width(r, 0, 0);
        lv_obj_set_style_border_side(r, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(r, 10, 0);
        lv_obj_set_style_border_color(r, color(sevColor(a.prio)), 0);
        lv_obj_set_style_pad_hor(r, 16, 0);
        lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* prio = label(r, fonts().title, sevColor(a.prio), sevName(a.prio));
        lv_obj_align(prio, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t* text = label(r, fonts().body, C_TEXT, "");
        lv_label_set_text_fmt(text, "%s: %s", a.node, a.text);
        lv_obj_set_width(text, 470);
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 56, -12);
        char since[48];
        sinceText(a, since, sizeof(since));
        s_since[i] = label(r, fonts().small, C_MUTED, since);
        lv_obj_align(s_since[i], LV_ALIGN_LEFT_MID, 56, 16);
        lv_obj_t* go = label(r, fonts().title, C_MUTED, LV_SYMBOL_RIGHT);
        lv_obj_align(go, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}
}  // namespace

void build() {
    s_list = box(g.content);
    lv_obj_set_size(s_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    buildList();
}

void refresh() {
    if (g.m->version != s_builtVersion || g.m->alarmCount != s_builtCount) {
        buildList();
        return;
    }
    char since[48];
    for (int i = 0; i < g.m->alarmCount; i++) {  // once a second: "x min" texts only
        sinceText(g.m->alarms[i], since, sizeof(since));
        lv_label_set_text(s_since[i], since);
    }
}

}  // namespace alarms
}  // namespace gui

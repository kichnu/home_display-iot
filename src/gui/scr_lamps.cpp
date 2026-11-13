// L2 lamp group: summary (group mode / program / state), one row per lamp, control zone with
// Auto / Noc / Test (safety 0) and "Program…" (list + confirmation, safety 1).

#include <stdio.h>
#include <string.h>

#include "gui_internal.h"

namespace gui {
namespace lamps {

namespace {
struct Row {
    lv_obj_t* dot;
    lv_obj_t* name;
    lv_obj_t* mode;
    lv_obj_t* program;
    lv_obj_t* info;
};

const uint8_t MODES[3] = {MODE_AUTO, MODE_NIGHT, MODE_TEST};

lv_obj_t* s_summary = nullptr;
lv_obj_t* s_state = nullptr;
lv_obj_t* s_spinner = nullptr;
lv_obj_t* s_list = nullptr;
lv_obj_t* s_modeBtn[3] = {};
lv_obj_t* s_progBtn = nullptr;
Row s_rows[MAX_LAMPS];
int s_rowCount = -1;

void onMode(lv_event_t* e) {
    uint8_t mode = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    const LampGroup& lg = g.m->lamps;
    if (lg.pending != Pending::None) return;
    // Same mode again = re-send to align lamps that missed the switch (mismatch); otherwise nothing to do
    if (mode == lg.mode && !lg.mismatch) return;
    g.cmd->setMode(mode);
}

lv_obj_t* controlButton(lv_obj_t* parent, const char* text, int32_t width) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, width, TOUCH_H);
    lv_obj_set_style_bg_color(b, color(C_SURFACE2), 0);
    lv_obj_set_style_bg_color(b, color(C_ACCENT), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = label(b, fonts().title, C_TEXT, text);
    lv_obj_center(l);
    return b;
}

void buildRows() {
    lv_obj_clean(s_list);
    s_rowCount = g.m->lamps.count;
    if (s_rowCount == 0) {
        label(s_list, fonts().body, C_MUTED, "Nie wykryto jeszcze żadnej lampy (heartbeat co 15 s).");
        return;
    }
    for (int i = 0; i < s_rowCount; i++) {
        lv_obj_t* r = lv_obj_create(s_list);
        lv_obj_set_size(r, lv_pct(100), 64);
        lv_obj_set_style_bg_color(r, color(C_SURFACE), 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_pad_hor(r, 14, 0);
        lv_obj_set_style_pad_ver(r, 0, 0);
        lv_obj_set_style_pad_column(r, 14, 0);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
        Row& row = s_rows[i];
        row.dot = lv_obj_create(r);
        lv_obj_set_size(row.dot, 18, 18);
        lv_obj_set_style_radius(row.dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(row.dot, 0, 0);
        row.name = label(r, fonts().body, C_TEXT, "");
        lv_obj_set_width(row.name, 190);
        lv_label_set_long_mode(row.name, LV_LABEL_LONG_DOT);
        row.mode = label(r, fonts().body, C_TEXT, "");
        lv_obj_set_width(row.mode, 70);
        row.program = label(r, fonts().small, C_MUTED, "");
        lv_obj_set_flex_grow(row.program, 1);
        lv_label_set_long_mode(row.program, LV_LABEL_LONG_DOT);
        row.info = label(r, fonts().small, C_MUTED, "");
    }
}

void refreshRow(Row& row, const LampRow& l, const LampGroup& lg) {
    Sev sev = !l.online ? Sev::Offline : ((l.flags & (LAMP_FLAG_FRAM | LAMP_FLAG_RTC)) ? Sev::P2 : Sev::Ok);
    lv_obj_set_style_bg_color(row.dot, color(sevColor(sev)), 0);
    lv_label_set_text(row.name, l.name);
    lv_label_set_text(row.mode, l.online ? modeName(l.mode) : "–");
    // Lamp differing from the group: highlighted
    bool differs = l.online && (l.mode != lg.mode || l.programId != lg.programId);
    lv_obj_set_style_text_color(row.mode, color(differs && l.mode != lg.mode ? C_P3 : C_TEXT), 0);
    char prog[48];
    snprintf(prog, sizeof(prog), "%s%s", l.program, (l.flags & LAMP_FLAG_ORPHAN) ? " (osierocony)" : "");
    lv_label_set_text(row.program, prog);
    lv_obj_set_style_text_color(row.program, color(differs && l.programId != lg.programId ? C_P3 : C_MUTED), 0);

    char info[64] = "";
    if (!l.online) {
        char ago[24];
        formatAgo(l.lastSeen, ago, sizeof(ago));
        snprintf(info, sizeof(info), "offline %s", ago);
    } else {
        if (l.flags & LAMP_FLAG_FRAM) strcat(info, "FRAM ");
        if (l.flags & LAMP_FLAG_RTC) strcat(info, "RTC ");
    }
    lv_label_set_text(row.info, info);
    lv_obj_set_style_text_color(row.info, color(!l.online ? C_OFFLINE : C_P2), 0);
}
}  // namespace

void build() {
    lv_obj_set_flex_flow(g.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g.content, PAD, 0);

    // Summary: group mode + program, state on the right
    s_summary = lv_obj_create(g.content);
    lv_obj_set_size(s_summary, lv_pct(100), 64);
    lv_obj_set_style_bg_color(s_summary, color(C_SURFACE), 0);
    lv_obj_set_style_border_width(s_summary, 0, 0);
    lv_obj_set_style_pad_hor(s_summary, 14, 0);
    lv_obj_remove_flag(s_summary, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* head = label(s_summary, fonts().title, C_TEXT, "");
    lv_obj_set_width(head, 470);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_align(head, LV_ALIGN_LEFT_MID, 0, 0);
    s_state = label(s_summary, fonts().body, C_OK, "");
    lv_obj_align(s_state, LV_ALIGN_RIGHT_MID, 0, 0);
    s_spinner = lv_spinner_create(s_summary);
    lv_obj_set_size(s_spinner, 32, 32);
    lv_obj_align_to(s_spinner, s_state, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    // Lamp rows
    s_list = box(g.content);
    lv_obj_set_width(s_list, lv_pct(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    s_rowCount = -1;

    // Control zone
    lv_obj_t* ctl = box(g.content);
    lv_obj_set_size(ctl, lv_pct(100), TOUCH_H);
    lv_obj_set_flex_flow(ctl, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctl, PAD, 0);
    static const char* const names[3] = {"Auto", "Noc", "Test"};
    for (int i = 0; i < 3; i++) {
        s_modeBtn[i] = controlButton(ctl, names[i], 150);
        lv_obj_add_event_cb(s_modeBtn[i], onMode, LV_EVENT_CLICKED, (void*)(intptr_t)MODES[i]);
    }
    s_progBtn = controlButton(ctl, LV_SYMBOL_LIST "  Program…", 150);
    lv_obj_set_flex_grow(s_progBtn, 1);
    lv_obj_add_event_cb(s_progBtn, [](lv_event_t*) { navigate(Screen::Programs); }, LV_EVENT_CLICKED, nullptr);

    refresh();
}

void refresh() {
    const LampGroup& lg = g.m->lamps;
    int online = lampsOnline();
    lv_obj_t* head = lv_obj_get_child(s_summary, 0);
    if (online == 0) lv_label_set_text(head, lg.count ? "Brak łączności z lampami" : "Brak lamp");
    else lv_label_set_text_fmt(head, "%s · %s", modeName(lg.mode), lg.program);

    bool pending = lg.pending != Pending::None;
    if (pending) {
        lv_label_set_text(s_state, "wysyłanie…");
        lv_obj_set_style_text_color(s_state, color(C_MUTED), 0);
        lv_obj_remove_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        if (lg.mismatch) {
            lv_label_set_text(s_state, LV_SYMBOL_WARNING " rozjazd");
            lv_obj_set_style_text_color(s_state, color(C_P3), 0);
        } else {
            lv_label_set_text_fmt(s_state, "%d/%d online", online, lg.count);
            lv_obj_set_style_text_color(s_state, color(online == lg.count && online ? C_OK : C_P2), 0);
        }
    }
    lv_obj_align_to(s_spinner, s_state, LV_ALIGN_OUT_LEFT_MID, -12, 0);

    if (s_rowCount != lg.count) buildRows();
    for (int i = 0; i < lg.count; i++) refreshRow(s_rows[i], lg.lamps[i], lg);

    bool enabled = online > 0 && !pending;
    for (int i = 0; i < 3; i++) {
        bool active = online > 0 && lg.mode == MODES[i];
        if (active) lv_obj_add_state(s_modeBtn[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(s_modeBtn[i], LV_STATE_CHECKED);
        if (enabled) lv_obj_remove_state(s_modeBtn[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_modeBtn[i], LV_STATE_DISABLED);
    }
    if (enabled) lv_obj_remove_state(s_progBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_progBtn, LV_STATE_DISABLED);
}

}  // namespace lamps
}  // namespace gui

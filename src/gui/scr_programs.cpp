// Program list of the lamp group: large rows, active marked; a tap opens the "set on all lamps"
// confirmation (safety 1), then back to the group screen where the pending state is shown.

#include <stdio.h>

#include "gui_internal.h"

namespace gui {
namespace programs {

namespace {
lv_obj_t* s_list = nullptr;
uint32_t s_sig = 0;
uint64_t s_chosenId = 0;
char s_chosenName[24];
bool s_scrollToActive = false;  // first list after opening: bring the active program into view

uint32_t signature() {
    const LampGroup& lg = g.m->lamps;
    uint32_t h = (uint32_t)g.m->programCount * 31u + (g.m->programsLoading ? 7u : 0u);
    h = h * 131u + (uint32_t)lg.programId + (uint32_t)(lg.programId >> 32);
    h = h * 131u + (uint32_t)lg.pending + (uint32_t)lg.pendingProgram;
    for (int i = 0; i < g.m->programCount; i++) h = h * 33u + (uint32_t)g.m->programs[i].id;
    return h;
}

void onConfirmed() {
    g.cmd->activateProgram(s_chosenId);
    navigate(Screen::Lamps);
}

void onRow(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= g.m->programCount) return;
    const Program& p = g.m->programs[i];
    if (p.id == g.m->lamps.programId || g.m->lamps.pending != Pending::None) return;
    s_chosenId = p.id;
    snprintf(s_chosenName, sizeof(s_chosenName), "%s", p.name);
    char text[160];
    snprintf(text, sizeof(text), "„%s” zostanie ustawiony na wszystkich lampach online (%d).", p.name, lampsOnline());
    confirm("Zmiana programu", text, "Ustaw", onConfirmed);
}

void buildList() {
    int32_t scrollY = lv_obj_get_scroll_y(s_list);
    lv_obj_clean(s_list);
    const LampGroup& lg = g.m->lamps;

    if (g.m->programCount == 0) {
        lv_obj_t* b = box(s_list);
        lv_obj_set_size(b, lv_pct(100), 200);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(b, 16, 0);
        if (g.m->programsLoading) {
            lv_obj_t* sp = lv_spinner_create(b);
            lv_obj_set_size(sp, 48, 48);
            label(b, fonts().body, C_MUTED, "Pobieranie listy programów z lampy…");
        } else {
            label(b, fonts().body, C_MUTED, "Brak programów (żadna lampa nie odpowiedziała).");
        }
        return;
    }

    lv_obj_t* activeRow = nullptr;
    for (int i = 0; i < g.m->programCount; i++) {
        const Program& p = g.m->programs[i];
        bool active = p.id == lg.programId;
        bool pending = lg.pending == Pending::Program && p.id == lg.pendingProgram;
        lv_obj_t* r = lv_button_create(s_list);
        lv_obj_set_size(r, lv_pct(100), ROW_H);
        lv_obj_set_style_bg_color(r, color(active ? C_SURFACE2 : C_SURFACE), 0);
        lv_obj_set_style_shadow_width(r, 0, 0);
        lv_obj_set_style_pad_hor(r, 16, 0);
        lv_obj_add_event_cb(r, onRow, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        if (active) activeRow = r;
        lv_obj_t* name = label(r, fonts().title, C_TEXT, "");
        lv_label_set_text_fmt(name, "%s  %s", active ? LV_SYMBOL_OK : "   ", p.name);
        if (active) lv_obj_set_style_text_color(name, color(C_OK), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        if (active) {
            lv_obj_t* t = label(r, fonts().body, C_MUTED, "aktywny");
            lv_obj_align(t, LV_ALIGN_RIGHT_MID, 0, 0);
        } else if (pending) {
            lv_obj_t* sp = lv_spinner_create(r);
            lv_obj_set_size(sp, 36, 36);
            lv_obj_align(sp, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    lv_obj_update_layout(s_list);
    if (s_scrollToActive) {
        s_scrollToActive = false;
        if (activeRow) lv_obj_scroll_to_view(activeRow, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(s_list, scrollY, LV_ANIM_OFF);
    }
}
}  // namespace

void build() {
    s_list = box(g.content);
    lv_obj_set_size(s_list, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    g.cmd->refreshPrograms();
    s_sig = signature();
    s_scrollToActive = true;
    buildList();
}

void refresh() {
    uint32_t sig = signature();
    if (sig == s_sig) return;
    s_sig = sig;
    buildList();
}

}  // namespace programs
}  // namespace gui

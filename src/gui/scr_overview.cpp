// L1 overview: one tile per node or group, 3 x 2 grid. Lamps tile from LampGroup, the rest from
// Model::tiles. Tile = status colour (left edge + icon), title, two short lines.

#include <stdio.h>

#include "gui_internal.h"

namespace gui {
namespace overview {

namespace {
struct Tile {
    lv_obj_t* obj;
    lv_obj_t* icon;
    lv_obj_t* line1;
    lv_obj_t* line2;
};
Tile s_tiles[1 + MAX_TILES];
int s_count = 0;

void setTile(Tile& t, Sev sev, const char* l1, const char* l2) {
    lv_obj_set_style_border_color(t.obj, color(sevColor(sev)), 0);
    lv_label_set_text(t.icon, sevSymbol(sev));
    lv_obj_set_style_text_color(t.icon, color(sevColor(sev)), 0);
    lv_label_set_text(t.line1, l1);
    lv_label_set_text(t.line2, l2);
}

void onTile(lv_event_t* e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i == 0) navigate(Screen::Lamps);
    else toast("Ekran tego węzła powstanie w kolejnych etapach");
}

Tile makeTile(lv_obj_t* grid, int index, const char* title) {
    Tile t = {};
    t.obj = lv_obj_create(grid);
    lv_obj_set_grid_cell(t.obj, LV_GRID_ALIGN_STRETCH, index % 3, 1, LV_GRID_ALIGN_STRETCH, index / 3, 1);
    lv_obj_set_style_bg_color(t.obj, color(C_SURFACE), 0);
    lv_obj_set_style_border_side(t.obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(t.obj, 8, 0);
    lv_obj_set_style_pad_all(t.obj, 14, 0);
    lv_obj_set_style_pad_row(t.obj, 6, 0);
    lv_obj_set_flex_flow(t.obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(t.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(t.obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(t.obj, color(C_SURFACE2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(t.obj, onTile, LV_EVENT_CLICKED, (void*)(intptr_t)index);

    lv_obj_t* head = box(t.obj);
    lv_obj_set_size(head, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_remove_flag(head, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* tl = label(head, fonts().title, C_TEXT, title);
    lv_obj_align(tl, LV_ALIGN_LEFT_MID, 0, 0);
    t.icon = label(head, fonts().title, C_OK, "");
    lv_obj_align(t.icon, LV_ALIGN_RIGHT_MID, 0, 0);

    t.line1 = label(t.obj, fonts().body, C_TEXT, "");
    lv_obj_set_width(t.line1, lv_pct(100));
    lv_label_set_long_mode(t.line1, LV_LABEL_LONG_DOT);
    t.line2 = label(t.obj, fonts().small, C_MUTED, "");
    lv_obj_set_width(t.line2, lv_pct(100));
    lv_label_set_long_mode(t.line2, LV_LABEL_LONG_DOT);
    return t;
}
}  // namespace

void build() {
    static const int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_t* grid = box(g.content);
    lv_obj_set_size(grid, lv_pct(100), lv_pct(100));
    lv_obj_set_grid_dsc_array(grid, cols, rows);
    lv_obj_set_style_pad_column(grid, PAD, 0);
    lv_obj_set_style_pad_row(grid, PAD, 0);

    s_count = 1 + (g.m->tileCount < 5 ? g.m->tileCount : 5);
    s_tiles[0] = makeTile(grid, 0, "Lampy");
    for (int i = 1; i < s_count; i++) s_tiles[i] = makeTile(grid, i, g.m->tiles[i - 1].title);
    refresh();
}

void refresh() {
    if (s_count != 1 + (g.m->tileCount < 5 ? g.m->tileCount : 5)) {  // node list changed
        lv_obj_clean(g.content);
        build();
        return;
    }
    const LampGroup& lg = g.m->lamps;
    char l1[64], l2[64];
    int online = lampsOnline();
    if (lg.count == 0) {
        snprintf(l1, sizeof(l1), "Brak lamp");
        snprintf(l2, sizeof(l2), "czekam na heartbeat");
    } else if (online == 0) {
        snprintf(l1, sizeof(l1), "Brak łączności");
        snprintf(l2, sizeof(l2), "0/%d online", lg.count);
    } else {
        snprintf(l1, sizeof(l1), "%s · %s", modeName(lg.mode), lg.program);
        if (lg.pending != Pending::None) snprintf(l2, sizeof(l2), "wysyłanie polecenia…");
        else if (lg.mismatch) snprintf(l2, sizeof(l2), "%d/%d online · rozjazd", online, lg.count);
        else snprintf(l2, sizeof(l2), "%d/%d online", online, lg.count);
    }
    setTile(s_tiles[0], lampGroupSev(), l1, l2);
    for (int i = 1; i < s_count; i++) {
        const NodeTile& n = g.m->tiles[i - 1];
        setTile(s_tiles[i], n.status, n.line1, n.line2);
    }
}

}  // namespace overview
}  // namespace gui

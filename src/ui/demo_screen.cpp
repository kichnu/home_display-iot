#include "demo_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "lvgl_port.h"

namespace ui {

namespace {
constexpr int TOUCH_H = 80;  // minimum touch target height (wet fingers), see the decisions doc

const char* const MODE_MAP[] = {"Auto", "Noc", "Test", ""};
const char* const MODE_NAMES[] = {"Auto", "Noc", "Test"};
const char* const DEMO_PROGRAMS = "Rafa dzień\nRafa zima\nAklimatyzacja\nFabryczny";

lv_obj_t* s_uptime = nullptr;
lv_obj_t* s_status = nullptr;
lv_obj_t* s_modes = nullptr;
lv_obj_t* s_programRoller = nullptr;
lv_obj_t* s_programDialog = nullptr;
char s_program[32] = "Rafa dzień";
int s_mode = 0;
bool s_perfShown = false;

void refreshStatus() {
    lv_label_set_text_fmt(s_status, "Tryb: %s · Program: %s", MODE_NAMES[s_mode], s_program);
}

lv_obj_t* makeCard(lv_obj_t* parent, const char* title) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* t = lv_label_create(card);
    lv_obj_set_style_text_font(t, fonts().title, 0);
    lv_label_set_text(t, title);
    return card;
}

void onMode(lv_event_t* e) {
    lv_obj_t* m = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint32_t idx = lv_buttonmatrix_get_selected_button(m);
    if (idx > 2) return;
    s_mode = idx;
    refreshStatus();
    Serial.printf("Demo: mode -> %s\n", MODE_NAMES[s_mode]);
}

void onProgramDialogButton(lv_event_t* e) {
    bool apply = reinterpret_cast<intptr_t>(lv_event_get_user_data(e)) != 0;
    if (apply) {
        lv_roller_get_selected_str(s_programRoller, s_program, sizeof(s_program));
        refreshStatus();
        Serial.printf("Demo: program -> %s\n", s_program);
    }
    lv_msgbox_close(s_programDialog);
    s_programDialog = nullptr;
}

void onProgram(lv_event_t*) {
    // Safety 1 (setting change): explicit confirmation in a panel-style modal.
    s_programDialog = lv_msgbox_create(nullptr);
    lv_obj_set_width(s_programDialog, 520);
    lv_obj_t* title = lv_msgbox_add_title(s_programDialog, "Program na wszystkich lampach");
    lv_obj_set_style_text_font(title, fonts().title, 0);
    lv_obj_t* content = lv_msgbox_get_content(s_programDialog);
    s_programRoller = lv_roller_create(content);
    lv_roller_set_options(s_programRoller, DEMO_PROGRAMS, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_programRoller, 3);
    lv_obj_set_width(s_programRoller, lv_pct(100));

    lv_obj_t* cancel = lv_msgbox_add_footer_button(s_programDialog, "Anuluj");
    lv_obj_t* ok = lv_msgbox_add_footer_button(s_programDialog, "Ustaw");
    for (lv_obj_t* b : {cancel, ok}) {
        lv_obj_set_height(b, TOUCH_H);
        lv_obj_set_flex_grow(b, 1);
    }
    lv_obj_add_event_cb(cancel, onProgramDialogButton, LV_EVENT_CLICKED, reinterpret_cast<void*>(0));
    lv_obj_add_event_cb(ok, onProgramDialogButton, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
}

void onPerf(lv_event_t* e) {
    lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
    s_perfShown = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_perfShown) lv_sysmon_show_performance(nullptr);
    else lv_sysmon_hide_performance(nullptr);
}

void onBlank(lv_event_t*) {
    Serial.println("Demo: backlight off, touch to wake");
    setBacklight(false);
}
}  // namespace

void createDemoScreen() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_set_style_pad_row(scr, 12, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t* hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, lv_pct(100), 64);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_t* name = lv_label_create(hdr);
    lv_obj_set_style_text_font(name, fonts().title, 0);
    lv_label_set_text(name, "Panel akwarium");
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
    s_uptime = lv_label_create(hdr);
    lv_obj_set_style_text_font(s_uptime, fonts().small, 0);
    lv_obj_align(s_uptime, LV_ALIGN_RIGHT_MID, 0, 0);

    // Content row
    lv_obj_t* row = lv_obj_create(scr);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_flex_grow(row, 1);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Lamp group card (mock): status zone + control zone
    lv_obj_t* lamps = makeCard(row, LV_SYMBOL_IMAGE " Lampy (demo)");
    lv_obj_set_height(lamps, lv_pct(100));
    lv_obj_set_flex_grow(lamps, 3);
    s_status = lv_label_create(lamps);
    lv_obj_set_width(s_status, lv_pct(100));
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
    refreshStatus();

    s_modes = lv_buttonmatrix_create(lamps);
    lv_buttonmatrix_set_map(s_modes, MODE_MAP);
    lv_buttonmatrix_set_button_ctrl_all(s_modes, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(s_modes, true);
    lv_buttonmatrix_set_button_ctrl(s_modes, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(s_modes, lv_pct(100), TOUCH_H + 16);
    lv_obj_set_style_text_font(s_modes, fonts().body, 0);
    lv_obj_add_event_cb(s_modes, onMode, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* prog = lv_button_create(lamps);
    lv_obj_set_size(prog, lv_pct(100), TOUCH_H);
    lv_obj_t* progLabel = lv_label_create(prog);
    lv_label_set_text(progLabel, LV_SYMBOL_LIST "  Program…");
    lv_obj_center(progLabel);
    lv_obj_add_event_cb(prog, onProgram, LV_EVENT_CLICKED, nullptr);

    // Glyph / rendering test card
    lv_obj_t* glyphs = makeCard(row, "Test fontów");
    lv_obj_set_height(glyphs, lv_pct(100));
    lv_obj_set_flex_grow(glyphs, 2);
    lv_obj_t* l1 = lv_label_create(glyphs);
    lv_label_set_text(l1, "Zażółć gęślą jaźń");
    lv_obj_t* l2 = lv_label_create(glyphs);
    lv_obj_set_style_text_font(l2, fonts().small, 0);
    lv_label_set_text(l2, "ZAŻÓŁĆ GĘŚLĄ JAŹŃ · µS/cm");
    lv_obj_t* l3 = lv_label_create(glyphs);
    lv_obj_set_style_text_font(l3, fonts().value, 0);
    lv_label_set_text(l3, "25,4 °C");
    lv_obj_t* spin = lv_spinner_create(glyphs);
    lv_obj_set_size(spin, 56, 56);

    // Bottom bar: FPS monitor, backlight wake test
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, lv_pct(100), TOUCH_H + 16);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(bar, 8, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* perfLabel = lv_label_create(bar);
    lv_label_set_text(perfLabel, "FPS / CPU");
    lv_obj_t* perf = lv_switch_create(bar);
    lv_obj_set_size(perf, 96, 48);
    lv_obj_add_event_cb(perf, onPerf, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* spacer = lv_obj_create(bar);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    lv_obj_t* blank = lv_button_create(bar);
    lv_obj_set_size(blank, 240, TOUCH_H - 8);
    lv_obj_t* blankLabel = lv_label_create(blank);
    lv_label_set_text(blankLabel, LV_SYMBOL_EYE_CLOSE "  Wygaś ekran");
    lv_obj_center(blankLabel);
    lv_obj_add_event_cb(blank, onBlank, LV_EVENT_CLICKED, nullptr);

    demoTick();
}

void demoTick() {
    uint32_t s = millis() / 1000;
    lv_label_set_text_fmt(s_uptime, "LVGL %d.%d.%d · czas pracy %02lu:%02lu:%02lu", LVGL_VERSION_MAJOR,
                          LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (unsigned long)(s / 3600),
                          (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
}

}  // namespace ui

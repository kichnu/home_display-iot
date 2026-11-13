#include "demo_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

#include "../lamps/lamp_listener.h"
#include "../net/network.h"
#include "lvgl_port.h"

namespace ui {

namespace {
constexpr int TOUCH_H = 80;  // minimum touch target height (wet fingers), see the decisions doc
constexpr int COLS = 7;
const char* const HEADERS[COLS] = {"Lampa", "MAC", "Stan", "Tryb", "Program", "Czas", "RSSI"};
const int32_t WIDTHS[COLS] = {150, 100, 130, 70, 170, 60, 70};

lv_obj_t* s_uptime = nullptr;
lv_obj_t* s_table = nullptr;
lv_obj_t* s_stats = nullptr;
bool s_perfShown = false;

void onPerf(lv_event_t* e) {
    lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
    s_perfShown = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (s_perfShown) lv_sysmon_show_performance(nullptr);
    else lv_sysmon_hide_performance(nullptr);
}

void onBlank(lv_event_t*) {
    Serial.println("UI: backlight off, touch to wake");
    setBacklight(false);
}

void refreshLamps() {
    uint32_t now = millis();
    int row = 1;
    for (int i = 0; i < lamps::MAX_LAMPS; i++) {
        const lamps::Lamp* l = lamps::lamp(i);
        if (!l) continue;
        char buf[40];
        lv_table_set_cell_value(s_table, row, 0, l->haveHb ? l->hb.name : "?");
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X", l->mac[3], l->mac[4], l->mac[5]);
        lv_table_set_cell_value(s_table, row, 1, buf);
        if (!l->haveHb) {
            lv_table_set_cell_value(s_table, row, 2, "bez HB");
            for (int c = 3; c < 6; c++) lv_table_set_cell_value(s_table, row, c, "");
        } else {
            snprintf(buf, sizeof(buf), "%s %lus", l->online ? "online" : "OFFLINE",
                     (unsigned long)((now - l->lastHbMs) / 1000));
            lv_table_set_cell_value(s_table, row, 2, buf);
            // * orphaned program, F FRAM error, R RTC error (heartbeat flags)
            snprintf(buf, sizeof(buf), "%s%s%s%s", lamps::modeName(l->hb.mode),
                     (l->hb.flags & lamps::HB_FLAG_ORPHAN) ? "*" : "",
                     (l->hb.flags & lamps::HB_FLAG_FRAM_FAIL) ? " F" : "",
                     (l->hb.flags & lamps::HB_FLAG_RTC_FAIL) ? " R" : "");
            lv_table_set_cell_value(s_table, row, 3, buf);
            lv_table_set_cell_value(s_table, row, 4, l->hb.active_name);
            lv_table_set_cell_value(s_table, row, 5, lamps::timeQualityName(l->hb.time_q));
        }
        snprintf(buf, sizeof(buf), "%d", l->rssi);
        lv_table_set_cell_value(s_table, row, 6, buf);
        row++;
    }
    lv_table_set_row_count(s_table, row);

    const lamps::Stats& st = lamps::stats();
    if (!st.enabled) {
        lv_label_set_text(s_stats, LV_SYMBOL_WARNING " ESP-NOW wyłączone (brak hasła Wi-Fi w buildzie)");
        return;
    }
    lv_label_set_text_fmt(s_stats, "rx ok %lu · zły HMAC %lu · powtórki %lu · format %lu · zgubione %lu · tx HB %lu (błędy %lu)",
                          (unsigned long)st.rxOk, (unsigned long)st.rxBadTag, (unsigned long)st.rxReplay,
                          (unsigned long)st.rxBadFormat, (unsigned long)st.rxDropped, (unsigned long)st.txHb,
                          (unsigned long)st.txFail);
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

    // Raw lamp table (stage 3: passive ESP-NOW listener)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_t* t = lv_label_create(card);
    lv_obj_set_style_text_font(t, fonts().title, 0);
    lv_label_set_text(t, LV_SYMBOL_WIFI " Lampy — nasłuch ESP-NOW");

    s_table = lv_table_create(card);
    lv_obj_set_width(s_table, lv_pct(100));
    lv_obj_set_flex_grow(s_table, 1);
    lv_obj_set_style_text_font(s_table, fonts().small, 0);
    lv_obj_set_style_pad_ver(s_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_table, 6, LV_PART_ITEMS);
    lv_table_set_column_count(s_table, COLS);
    for (int c = 0; c < COLS; c++) {
        lv_table_set_column_width(s_table, c, WIDTHS[c]);
        lv_table_set_cell_value(s_table, 0, c, HEADERS[c]);
    }
    lv_table_set_row_count(s_table, 1);

    s_stats = lv_label_create(card);
    lv_obj_set_width(s_stats, lv_pct(100));
    lv_obj_set_style_text_font(s_stats, fonts().small, 0);
    lv_label_set_long_mode(s_stats, LV_LABEL_LONG_DOT);

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

    lv_obj_t* spin = lv_spinner_create(bar);  // animation: tearing / smoothness check while the radio runs
    lv_obj_set_size(spin, 48, 48);

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
    // Header: network state + clock (NTP), uptime until the clock is set
    char net[48];
    if (net::connected()) snprintf(net, sizeof(net), LV_SYMBOL_WIFI " %s (%d dBm)", net::ip().c_str(), net::rssi());
    else snprintf(net, sizeof(net), LV_SYMBOL_WARNING " brak Wi-Fi");

    char clock[32];
    if (net::timeValid()) {
        time_t now = time(nullptr);
        struct tm lt;
        localtime_r(&now, &lt);
        strftime(clock, sizeof(clock), "%d.%m %H:%M:%S", &lt);
    } else {
        uint32_t s = millis() / 1000;
        snprintf(clock, sizeof(clock), "czas pracy %02lu:%02lu:%02lu", (unsigned long)(s / 3600),
                 (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    }
    lv_label_set_text_fmt(s_uptime, "%s · %s", net, clock);
    refreshLamps();
}

}  // namespace ui

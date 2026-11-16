#pragma once
// Panel GUI: top bar, alarm strip, overview (L1), lamp group (L2), program list, alarm list,
// screensaver, toasts and confirmations. Platform independent (LVGL only).

#include <stddef.h>

#include "model.h"

namespace gui {

// After lv_init(), a display and an input device exist, initFonts() and applyTheme() were called.
void begin(Model* model, Commands* commands);

// Every loop iteration (after lv_timer_handler): rebuilds what changed (Model::version),
// refreshes the clock and "x min ago" texts once a second, handles the screensaver.
void update();

void toast(const char* text, Sev sev = Sev::Ok);

void showScreensaver();

// Texts of the "Termostat" tile (panel adapter and simulator): temperature / state or Service countdown
void thermoTileLines(const ThermoView& t, char* line1, size_t n1, char* line2, size_t n2);  // also after SCREENSAVER_S without touch

}  // namespace gui

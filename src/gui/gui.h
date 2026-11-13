#pragma once
// Panel GUI: top bar, alarm strip, overview (L1), lamp group (L2), program list, alarm list,
// screensaver, toasts and confirmations. Platform independent (LVGL only).

#include "model.h"

namespace gui {

// After lv_init(), a display and an input device exist, initFonts() and applyTheme() were called.
void begin(Model* model, Commands* commands);

// Every loop iteration (after lv_timer_handler): rebuilds what changed (Model::version),
// refreshes the clock and "x min ago" texts once a second, handles the screensaver.
void update();

void toast(const char* text, Sev sev = Sev::Ok);

void showScreensaver();  // also after SCREENSAVER_S without touch

}  // namespace gui

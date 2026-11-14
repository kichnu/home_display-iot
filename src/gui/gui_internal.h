#pragma once
// Shared between the GUI shell (gui.cpp) and the screens (scr_*.cpp).

#include <stddef.h>

#include "fonts.h"
#include "gui.h"
#include "theme.h"

namespace gui {

struct Ctx {
    Model* m = nullptr;
    Commands* cmd = nullptr;
    lv_obj_t* content = nullptr;  // screen area below the top bar / alarm strip
    Screen screen = Screen::Overview;
};
extern Ctx g;

void navigate(Screen s);

uint32_t sevColor(Sev s);
uint32_t sevTextColor(Sev s);  // readable text on a sevColor() background
const char* sevName(Sev s);    // "P1".."P3", "OK", "offline"
const char* sevSymbol(Sev s);
const char* modeName(uint8_t mode);
void formatAgo(time_t t, char* buf, size_t n);  // "45 s", "3 min", "2 h 5 min"
Sev lampGroupSev();                              // worst state of the lamp group (for its tile / header)
Sev topoffSev();                                 // worst alarm of the top-off node, Offline without status
int lampsOnline();

// Panel-style confirmation (safety 1): Cancel / OK, both TOUCH_H high
void confirm(const char* title, const char* text, const char* okLabel, void (*onOk)());
// The same modal with own content: returns the content container to fill (closed like confirm)
lv_obj_t* dialog(const char* title, const char* okLabel, void (*onOk)());

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t col, const char* text);
lv_obj_t* box(lv_obj_t* parent);  // plain container: no scroll, no border, transparent

// Screens: build() creates widgets in g.content, refresh() updates them in place (called on model
// changes and once a second) and rebuilds only parts whose structure changed.
namespace overview { void build(); void refresh(); }
namespace lamps { void build(); void refresh(); }
namespace programs { void build(); void refresh(); }
namespace alarms { void build(); void refresh(); }
namespace topoff { void build(); void refresh(); }

}  // namespace gui

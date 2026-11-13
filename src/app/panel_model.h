#pragma once
// Adapter between the panel's data sources and the GUI (stage 5): builds gui::Model from the lamp
// listener (lamp table, program catalog) and the network state, derives the alarms, and implements
// gui::Commands. Read only in stage 5: commands only show a toast (control comes in stage 6).

#include "gui/model.h"

namespace app {

void update();  // every loop() iteration, before gui::update()

gui::Model& model();
gui::Commands& commands();

}  // namespace app

#pragma once
// Adapter between the panel's data sources and the GUI: builds gui::Model from the lamp listener
// (lamp table, program catalog) and the network state, derives the alarms (stage 5), and implements
// gui::Commands over lamp_control (stage 6): "pending" until the lamps acknowledge and their heartbeats
// show the new state, then one toast with the result.

#include "gui/model.h"

namespace app {

void update();  // every loop() iteration, before gui::update()

gui::Model& model();
gui::Commands& commands();

}  // namespace app

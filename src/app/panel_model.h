#pragma once
// Adapter between the panel's data sources and the GUI: builds gui::Model from the lamp listener
// (lamp table, program catalog), the top-off listener (stages D3, D5), the thermostat listener
// (stages T3, T5) and the network state, derives the alarms (stage 5), and implements gui::Commands
// over lamp_control (stage 6) and the top-off / thermostat listeners (stages D5, T5): "pending" until
// the node(s) acknowledge and their status shows the new state, then one toast with the result.

#include "gui/model.h"

namespace app {

void update();  // every loop() iteration, before gui::update()

gui::Model& model();
gui::Commands& commands();

}  // namespace app

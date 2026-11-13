#pragma once
// Test screen: raw lamp table from the passive ESP-NOW listener (stage 3), network/clock header,
// spinner for tearing checks while the radio runs, FPS monitor and backlight wake test.

namespace ui {

void createDemoScreen();
void demoTick();  // ~1 s, from loop()

}  // namespace ui

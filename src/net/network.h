#pragma once
// Wi-Fi STA (credentials from the generated wifi_secret.h), NTP time, ArduinoOTA.
// Everything runs from loop(): update() also drives ArduinoOTA.handle().

#include <Arduino.h>

namespace net {

void begin();    // after ui::begin() — OTA progress is drawn on the LVGL top layer
void update();   // every loop() iteration

bool connected();
bool timeValid();        // system clock set by NTP (later also from node heartbeats)
String ip();             // "" while not connected
int rssi();              // dBm, 0 while not connected
void printInfo();        // Serial: MAC, SSID, IP, RSSI, channel, time

}  // namespace net

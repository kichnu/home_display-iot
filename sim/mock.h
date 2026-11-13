#pragma once
// Mock data for the GUI simulator: lamp group, other nodes, alarms derived like the panel will do,
// commands answered after a delay. State toggles from the keyboard (see Mock::help()).

#include <stdint.h>

#include "gui/model.h"

class Mock : public gui::Commands {
public:
    gui::Model model = {};

    void init();
    void tick(uint32_t nowMs);  // main loop
    void key(char k);
    static void help();

    void setMode(uint8_t mode) override;
    void activateProgram(uint64_t id) override;
    void refreshPrograms() override;

private:
    void derive();  // group state + alarms from the raw state, bumps model.version
    void fillPrograms();
    const char* programName(uint64_t id) const;

    // Raw state the keys toggle
    bool offline_[2] = {false, false};
    time_t offlineSince_[2] = {0, 0};
    time_t since_[8] = {};
    bool dosingP1_ = false;
    bool topoffP3_ = false;
    bool rejectNext_ = false;
    bool slowPrograms_ = false;

    // Command in flight
    uint32_t cmdDueMs_ = 0;
    uint32_t programsDueMs_ = 0;
    uint32_t lastHbMs_ = 0;
    uint32_t nowMs_ = 0;
};

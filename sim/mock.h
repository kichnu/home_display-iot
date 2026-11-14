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
    void topoffSetAuto(bool on) override;
    void topoffPortions(uint8_t n) override;
    void topoffStop() override;
    void topoffReset() override;
    void topoffSetLimit(uint16_t ml) override;

private:
    void derive();  // group state + alarms from the raw state, bumps model.version
    void fillPrograms();
    const char* programName(uint64_t id) const;
    void topoffInit();
    void topoffTick(uint32_t nowMs);  // pump cycle, manual session, command answers
    void topoffDerive();              // zone, portions, status, alarms, tile
    void topoffSend(gui::TopoffPending p);

    // Raw state the keys toggle
    bool offline_[2] = {false, false};
    time_t offlineSince_[2] = {0, 0};
    time_t since_[24] = {};
    bool dosingP1_ = false;
    // Top-off raw state
    bool topoffOffline_ = false;
    int topoffLevel_ = 0;          // index into the level steps (key 6)
    bool topoffEstimated_ = false;
    bool topoffDailyLimit_ = false;
    bool topoffLowLatched_ = false;
    uint32_t topoffCmdDueMs_ = 0;
    uint32_t topoffPhaseMs_ = 0;   // manual portion / pump cycle phase end
    bool topoffPause_ = false;     // manual session: pause between portions
    uint32_t topoffServiceMs_ = 0; // Service Mode start
    bool rejectNext_ = false;
    bool slowPrograms_ = false;

    // Command in flight
    uint32_t cmdDueMs_ = 0;
    uint32_t programsDueMs_ = 0;
    uint32_t lastHbMs_ = 0;
    uint32_t nowMs_ = 0;
};

#pragma once

#include <cstdint>
#include <string>

class Scheduler {
public:
    std::string start();
    std::string stop();
    bool isRunning() const { return running_; }

    void onCommandTick();
    uint32_t cpuCycles() const { return cpuCycles_; }

private:
    bool running_ = false;
    uint32_t cpuCycles_ = 0;
};

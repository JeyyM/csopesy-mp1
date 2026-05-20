#include "Process.h"

#include "TimeUtil.h"

#include <chrono>
#include <random>
#include <sstream>

namespace {

uint32_t randomInRange(uint32_t minValue, uint32_t maxValue) {
    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<uint32_t> dist(minValue, maxValue);
    return dist(rng);
}

}  // namespace

void Process::initializeDummy(const std::string& processName, int processId,
                              uint32_t minIns, uint32_t maxIns, int numCpu) {
    name = processName;
    id = processId;
    creationTimestamp = TimeUtil::formatNow();
    assignedCore = numCpu > 0 ? (processId % numCpu) : 0;
    totalLinesOfCode = randomInRange(minIns, maxIns);
    currentInstructionLine = randomInRange(1, std::min<uint32_t>(50, totalLinesOfCode));
    status = ProcessStatus::Running;
    hasExitedAfterFinish = false;
    logs.clear();

    ProcessLogEntry entry;
    entry.timestamp = TimeUtil::formatNow();
    entry.core = assignedCore;
    entry.message = "Hello world from " + name + "!";
    logs.push_back(entry);
}

void Process::advanceDummyState(int numCpu) {
    if (status == ProcessStatus::Finished) {
        return;
    }

    if (currentInstructionLine < totalLinesOfCode) {
        currentInstructionLine += randomInRange(1, 25);
        if (currentInstructionLine > totalLinesOfCode) {
            currentInstructionLine = totalLinesOfCode;
        }
    }

    if (logs.size() < 4 && randomInRange(0, 2) == 0) {
        ProcessLogEntry entry;
        entry.timestamp = TimeUtil::formatNow();
        entry.core = numCpu > 0 ? randomInRange(0, static_cast<uint32_t>(numCpu - 1)) : 0;
        entry.message = "Hello world from " + name + "!";
        logs.push_back(entry);
    }

    if (currentInstructionLine >= totalLinesOfCode) {
        status = ProcessStatus::Finished;
        currentInstructionLine = totalLinesOfCode;
    }
}

std::string Process::formatProcessSmi() const {
    std::ostringstream output;
    output << "Process name: " << name << "\n";
    output << "ID: " << id << "\n";
    output << "Logs:\n";
    for (const auto& log : logs) {
        output << "(" << log.timestamp << ") Core:" << log.core
               << " \"" << log.message << "\"\n";
    }

    if (status == ProcessStatus::Finished) {
        output << "\nFinished!\n";
        return output.str();
    }

    output << "\nCurrent instruction line: " << currentInstructionLine << "\n";
    output << "Lines of code: " << totalLinesOfCode << "\n";
    return output.str();
}

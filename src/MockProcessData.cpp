#include "MockProcessData.h"

#include <sstream>

namespace {

std::vector<MockProcessEntry>& processes() {
    static std::vector<MockProcessEntry> list;
    return list;
}

bool schedulerProcessesGenerated = false;

void addProcess(MockProcessEntry entry) {
    for (const MockProcessEntry& existing : processes()) {
        if (existing.name == entry.name) {
            return;
        }
    }
    processes().push_back(std::move(entry));
}

void loadInitialProcesses() {
    processes().clear();
    schedulerProcessesGenerated = false;
    processes().reserve(16);

    addProcess({
        "process01", 1, "01/18/2024 09:00:21AM", 0, 5876, 5876, true, {},
    });
    addProcess({
        "process02", 2, "01/18/2024 09:00:22AM", 0, 5876, 5876, true, {},
    });
    addProcess({
        "process03", 3, "01/18/2024 09:00:42AM", 0, 1000, 1000, true, {},
    });
    addProcess({
        "process04", 4, "01/18/2024 09:00:53AM", 0, 80, 80, true, {},
    });
    addProcess({
        "process05",
        5,
        "01/18/2024 09:15:22AM",
        0,
        1235,
        5876,
        false,
        {
            {"01/18/2024 09:15:22AM", 0, "Hello world from process05!"},
            {"01/18/2024 09:15:28AM", 0, "Hello world from process05!"},
        },
    });
    addProcess({
        "process06",
        6,
        "01/18/2024 09:17:22AM",
        1,
        3,
        5876,
        false,
        {{"01/18/2024 09:17:22AM", 1, "Hello world from process06!"}},
    });
    addProcess({
        "process07",
        7,
        "01/18/2024 09:17:43AM",
        2,
        9,
        1000,
        false,
        {{"01/18/2024 09:17:43AM", 2, "Hello world from process07!"}},
    });
    addProcess({
        "process08",
        8,
        "01/18/2024 09:18:58AM",
        3,
        12,
        80,
        false,
        {{"01/18/2024 09:18:58AM", 3, "Hello world from process08!"}},
    });
}

std::string padRight(const std::string& text, std::size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

MockLogEntry makeLog(const std::string& timestamp, int core, const std::string& processName) {
    return {timestamp, core, "Hello world from " + processName + "!"};
}

}  // namespace

void MockProcessData::ensureInitialized() {
    if (processes().empty()) {
        loadInitialProcesses();
    }
}

bool MockProcessData::hasSchedulerMockProcesses() {
    ensureInitialized();
    return schedulerProcessesGenerated;
}

bool MockProcessData::addSchedulerMockProcesses() {
    ensureInitialized();
    if (schedulerProcessesGenerated) {
        return false;
    }

    addProcess({
        "p01", 101, "01/18/2024 09:20:00AM", 0, 0, 1000, false,
        {makeLog("01/18/2024 09:20:00AM", 0, "p01")},
    });
    addProcess({
        "p02", 102, "01/18/2024 09:20:01AM", 1, 0, 1200, false,
        {makeLog("01/18/2024 09:20:01AM", 1, "p02")},
    });
    addProcess({
        "p03", 103, "01/18/2024 09:20:02AM", 2, 0, 1400, false,
        {makeLog("01/18/2024 09:20:02AM", 2, "p03")},
    });
    addProcess({
        "p04", 104, "01/18/2024 09:20:03AM", 3, 0, 1600, false,
        {makeLog("01/18/2024 09:20:03AM", 3, "p04")},
    });
    addProcess({
        "p05", 105, "01/18/2024 09:20:04AM", 0, 0, 1800, false,
        {makeLog("01/18/2024 09:20:04AM", 0, "p05")},
    });

    schedulerProcessesGenerated = true;
    return true;
}

const MockProcessEntry* MockProcessData::findByName(const std::string& name) {
    ensureInitialized();
    for (const MockProcessEntry& process : processes()) {
        if (process.name == name) {
            return &process;
        }
    }
    return nullptr;
}

const MockProcessEntry* MockProcessData::findRunningProcess(const std::string& name) {
    const MockProcessEntry* process = findByName(name);
    if (process != nullptr && !process->finished) {
        return process;
    }
    return nullptr;
}

bool MockProcessData::isAttachable(const std::string& name) {
    return findRunningProcess(name) != nullptr;
}

std::string MockProcessData::formatProcessSmi(const std::string& name) {
    const MockProcessEntry* process = findByName(name);
    if (!process) {
        return "";
    }

    std::ostringstream output;
    output << "Process name: " << process->name << "\n";
    output << "ID: " << process->id << "\n";
    output << "Logs:\n";
    for (const MockLogEntry& log : process->logs) {
        output << "(" << log.timestamp << ") Core:" << log.core
               << " \"" << log.message << "\"\n";
    }

    if (process->finished) {
        output << "\nFinished!\n";
        return output.str();
    }

    output << "\nCurrent instruction line: " << process->currentInstruction << "\n";
    output << "Lines of code: " << process->totalInstructions << "\n";
    return output.str();
}

std::string MockProcessData::generateLsReport() {
    ensureInitialized();

    std::ostringstream report;
    report << "CPU utilization: 100%\n";
    report << "Cores used: 4\n";
    report << "Cores available: 0\n";
    report << "\n";
    report << "---------------------------------------------\n";
    report << "Running processes:\n";

    for (const MockProcessEntry& process : processes()) {
        if (!process.finished) {
            report << padRight(process.name, 12) << " (" << process.timestamp << ")    "
                   << "Core: " << process.core << "    " << process.currentInstruction
                   << " / " << process.totalInstructions << "\n";
        }
    }

    report << "\nFinished processes:\n";
    for (const MockProcessEntry& process : processes()) {
        if (process.finished) {
            report << padRight(process.name, 12) << " (" << process.timestamp << ")    "
                   << "Finished    " << process.totalInstructions << " / "
                   << process.totalInstructions << "\n";
        }
    }

    report << "---------------------------------------------\n";
    return report.str();
}

void MockProcessData::registerProcess(const std::string& name, int id,
                                      const std::string& timestamp, int core,
                                      int currentInstruction, int totalInstructions) {
    ensureInitialized();
    MockProcessEntry entry;
    entry.name = name;
    entry.id = id;
    entry.timestamp = timestamp;
    entry.core = core;
    entry.currentInstruction = currentInstruction;
    entry.totalInstructions = totalInstructions;
    entry.finished = false;
    entry.logs.push_back({timestamp, core, "Hello world from " + name + "!"});
    addProcess(std::move(entry));
}

// Config.cpp is for general utility functions and initialization from config.txt

#include "Config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// General utility functions
namespace {

// removes leading and trailing whitespace from a string
std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// removes surrounding double quotes from a token
std::string stripQuotes(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

// converts a text number into an unsigned 32-bit integer
bool parseUint32(const std::string& text, uint32_t& out) {
    try {
        const unsigned long long value = std::stoull(text);
        if (value > 0xFFFFFFFFULL) {
            return false;
        }
        out = static_cast<uint32_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

// maps config.txt scheduler string to SchedulerType enum
bool parseSchedulerValue(const std::string& text, SchedulerType& out) {
    const std::string normalized = stripQuotes(trim(text));
    if (normalized == "fcfs") {
        out = SchedulerType::FCFS;
        return true;
    }
    if (normalized == "rr") {
        out = SchedulerType::RR;
        return true;
    }
    return false;
}

// reads the value half of a key value pair from the file like 
// num-cpu 4
bool readValueToken(std::istream& in, std::string& out) {
    std::string raw;
    if (!(in >> raw)) {
        return false;
    }
    // If the token starts with ", keep reading until we hit the closing quote.
    if (!raw.empty() && raw.front() == '"') {
        std::string combined = raw;
        while (combined.back() != '"' || combined.size() == 1) {
            std::string next;
            if (!(in >> next)) {
                return false;
            }
            combined += " " + next;
        }
        out = combined;
        return true;
    }
    out = raw;
    return true;
}

} 

//
//   1. Open config.txt
//   2. Loop: read KEY, then read VALUE, dispatch to the matching handler
//   3. After the loop, run cross-field validation (min-ins <= max-ins, etc.)
//   4. Verify every required key was actually present in the file
//   5. On success: copy parsed Config into `out`, set loaded = true
//
bool ConfigLoader::loadFromFile(const std::string& path, Config& out, std::string& errorMessage) {
    // --- Step 1: open the file ---
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Could not open config file: " + path;
        return false;
    }

    // Fresh Config object; fields start at 0/false until each key sets them.
    Config parsed;
    std::string key;

    // --- Step 2: read KEY VALUE pairs until end of file ---
    // operator>> skips whitespace, so "num-cpu 4\nscheduler \"fcfs\"" works.
    while (file >> key) {
        std::string value;
        if (!readValueToken(file, value)) {
            errorMessage = "Missing value for config parameter: " + key;
            return false;
        }

        // --- Dispatch each known config key ---
        if (key == "num-cpu") {
            // How many CPU cores / threads the scheduler will use.
            int num = 0;
            try {
                num = std::stoi(value);
            } catch (...) {
                errorMessage = "Invalid num-cpu value.";
                return false;
            }
            if (num < 1 || num > 128) {
                errorMessage = "num-cpu must be in range [1, 128].";
                return false;
            }
            parsed.numCpu = num;

        } else if (key == "scheduler") {
            // Scheduling algorithm: "fcfs" or "rr"
            if (!parseSchedulerValue(value, parsed.scheduler)) {
                errorMessage = "Scheduler must be \"fcfs\" or \"rr\".";
                return false;
            }

        } else if (key == "quantum-cycles") {
            if (!parseUint32(value, parsed.quantumCycles) || parsed.quantumCycles < 1) {
                errorMessage = "Invalid quantum-cycles value.";
                return false;
            }

        } else if (key == "batch-process-freq") {
            // How often (in CPU cycles) scheduler-start auto-generates processes.
            if (!parseUint32(value, parsed.batchProcessFreq) || parsed.batchProcessFreq < 1) {
                errorMessage = "Invalid batch-process-freq value.";
                return false;
            }

        } else if (key == "min-ins") {
            // Minimum PRINT instructions per process.
            // Used by screen -s (random range) and by initialize when min == max.
            if (!parseUint32(value, parsed.minIns) || parsed.minIns < 1) {
                errorMessage = "Invalid min-ins value.";
                return false;
            }

        } else if (key == "max-ins") {
            // Maximum PRINT instructions per process. Must be >= min-ins.
            if (!parseUint32(value, parsed.maxIns) || parsed.maxIns < 1) {
                errorMessage = "Invalid max-ins value.";
                return false;
            }

        } else if (key == "delay-per-exec") {
            // Extra CPU cycles to sleep between executing consecutive instructions.
            // 0 means one instruction per cycle (fastest)
            if (!parseUint32(value, parsed.delayPerExec)) {
                errorMessage = "Invalid delay-per-exec value.";
                return false;
            }

        } else if (key == "initial-process-count") {
            // Optional batch created immediately on scheduler-start (0 = batch-only).
            if (!parseUint32(value, parsed.initialProcessCount) ||
                parsed.initialProcessCount < 1 || parsed.initialProcessCount > 128) {
                errorMessage = "initial-process-count must be in range [1, 128].";
                return false;
            }

        } else {
            // Any key not in the list above is rejected immediately.
            errorMessage = "Unknown config parameter: " + key;
            return false;
        }
    }

    // --- Step 3: cross-field validation ---
    // min-ins cannot be larger than max-ins (would break random instruction count).
    if (parsed.minIns > parsed.maxIns) {
        errorMessage = "min-ins cannot be greater than max-ins.";
        return false;
    }

    // --- Step 4: ensure every required key appeared in config.txt ---
    // If a key was never read, its field is still 0 and we fail with a
    // specific "missing" message so the user knows what to add to the file.
    if (parsed.numCpu < 1) {
        errorMessage = "config.txt is missing num-cpu.";
        return false;
    }
    if (parsed.quantumCycles < 1) {
        errorMessage = "config.txt is missing quantum-cycles.";
        return false;
    }
    if (parsed.batchProcessFreq < 1) {
        errorMessage = "config.txt is missing batch-process-freq.";
        return false;
    }
    if (parsed.minIns < 1) {
        errorMessage = "config.txt is missing min-ins.";
        return false;
    }
    if (parsed.maxIns < 1) {
        errorMessage = "config.txt is missing max-ins.";
        return false;
    }
    // initial-process-count is optional (0 = rely on batch-process-freq only).

    // --- Step 5: success — hand the parsed config back to main.cpp ---
    parsed.loaded = true;
    out = parsed;
    errorMessage.clear();
    return true;
}

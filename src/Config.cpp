#include "Config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string stripQuotes(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

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

bool readValueToken(std::istream& in, std::string& out) {
    std::string raw;
    if (!(in >> raw)) {
        return false;
    }
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

}  // namespace

bool ConfigLoader::loadFromFile(const std::string& path, Config& out, std::string& errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Could not open config file: " + path;
        return false;
    }

    Config parsed;
    std::string key;
    while (file >> key) {
        std::string value;
        if (!readValueToken(file, value)) {
            errorMessage = "Missing value for config parameter: " + key;
            return false;
        }

        if (key == "num-cpu") {
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
            if (!parseUint32(value, parsed.batchProcessFreq) || parsed.batchProcessFreq < 1) {
                errorMessage = "Invalid batch-process-freq value.";
                return false;
            }
        } else if (key == "min-ins") {
            if (!parseUint32(value, parsed.minIns) || parsed.minIns < 1) {
                errorMessage = "Invalid min-ins value.";
                return false;
            }
        } else if (key == "max-ins") {
            if (!parseUint32(value, parsed.maxIns) || parsed.maxIns < 1) {
                errorMessage = "Invalid max-ins value.";
                return false;
            }
        } else if (key == "delay-per-exec") {
            if (!parseUint32(value, parsed.delayPerExec)) {
                errorMessage = "Invalid delay-per-exec value.";
                return false;
            }
        } else {
            errorMessage = "Unknown config parameter: " + key;
            return false;
        }
    }

    if (parsed.minIns > parsed.maxIns) {
        errorMessage = "min-ins cannot be greater than max-ins.";
        return false;
    }

    parsed.loaded = true;
    out = parsed;
    errorMessage.clear();
    return true;
}

// Implements ConfigLoader::loadFromFile — the parser behind the "initialize" command.

//   When the user types "initialize", main.cpp calls loadFromFile("config.txt", ...).
//   This file is entirely responsible for turning that text file into a filled
//   Config struct that the rest of the program can use.
//
//   Nothing else in this file is public. All the helper functions (trim,
//   stripQuotes, parseUint32, etc.) are private utilities that only
//   loadFromFile calls.

// how loadFromFile works:
//
//   1. Open config.txt — if missing, return error immediately
//   2. Loop: read one KEY, then read its VALUE
//        match KEY against every known config parameter
//        validate VALUE for that parameter (range, format)
//        store result in a temporary Config object
//        if KEY is unknown -> return error
//   3. Cross-field check: min-ins must be <= max-ins
//   4. Presence check: every required key must have been seen
//        (if a key was never read, its field is still 0 -> return "missing X" error)
//   5. Success: copy the temporary Config into the output, set loaded = true

// EXAMPLE — parsing this config.txt:
//
//   num-cpu 4
//   scheduler "rr"
//   quantum-cycles 5
//   batch-process-freq 1
//   min-ins 100
//   max-ins 2000
//   delay-per-exec 0
//   initial-process-count 10
//
//   Step 1: file opens fine
//   Step 2: reads "num-cpu" -> "4"  -> parsed.numCpu = 4
//           reads "scheduler" -> "\"rr\"" -> parsed.scheduler = RR
//           reads "quantum-cycles" -> "5" -> parsed.quantumCycles = 5
//           ... and so on for every line
//   Step 3: minIns(100) <= maxIns(2000) -> OK
//   Step 4: all required fields > 0 -> OK
//   Step 5: out = parsed; loaded = true; return true
//
// EXAMPLE — what happens with a bad config.txt:
//
//   num-cpu 200          <- out of range (max is 128)
//
//   Step 2: reads "num-cpu" -> "200"
//           200 > 128 -> errorMessage = "num-cpu must be in range [1, 128]."
//           return false
//   main.cpp prints the error; "initialize" fails; system stays uninitialized.

#include "Config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

// General utility functions
namespace {

// Removes leading and trailing whitespace (spaces, tabs, newlines) from a string.
// Used to clean up both keys and values read from config.txt.
//
// Example: IN "  num-cpu  " -> OUT "num-cpu"
// Example: IN "\t4\r\n"     -> OUT "4"
std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

// Removes surrounding double-quote characters from a string.
// config.txt uses quotes for the scheduler value: scheduler "rr"
// After reading the token "rr" (with quotes), this strips them to get rr.
//
// Example: IN "\"rr\""  -> OUT "rr"
// Example: IN "rr"      -> OUT "rr"  (no quotes, returns as-is)
std::string stripQuotes(const std::string& value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

// Converts a text token into an unsigned 32-bit integer (0 to 4,294,967,295).
// Returns true and writes the result into `out` on success.
// Returns false if the text is not a valid number or is too large.
//
// Example: IN "100"        -> out=100, true
// Example: IN "0"          -> out=0,   true
// Example: IN "abc"        -> out=0,   false  (not a number)
// Example: IN "9999999999" -> out=0,   false  (too large for uint32)
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

// Converts the scheduler string from config.txt into a SchedulerType enum value.
// Strips quotes and normalizes case so both "fcfs" and "rr" are accepted.
// Returns false if the value is anything else.
//
// Example: IN "\"fcfs\""  -> out=FCFS, true
// Example: IN "\"rr\""    -> out=RR,   true
// Example: IN "\"sjf\""   -> out=?,    false  (not a supported algorithm)
// Returns true if the value is a power of two in [64, 65536].
bool isPowerOfTwoInRange(uint32_t value) {
    return value >= 64 && value <= 65536 && (value & (value - 1)) == 0;
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

// Reads the VALUE token that follows a KEY in config.txt.
// Normally one whitespace-delimited word (e.g. "4", "0").
// For quoted strings (e.g. "rr"), keeps reading until the closing quote is found.
//
// This handles the case where a quoted value has spaces inside it,
// e.g.  scheduler "round robin"  would be read as one token "round robin".
// (No current config key uses spaces in its value, but the parser supports it.)
//
// Example: stream contains '4\n'        -> out="4",    true
// Example: stream contains '"rr"\n'     -> out="\"rr\"", true
// Example: stream is at end-of-file     -> out unchanged, false
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

// ConfigLoader::loadFromFile
//
// The only public function in this file. Called directly by the "initialize"
// command handler in main.cpp:
//
//   if (!ConfigLoader::loadFromFile("config.txt", config, error)) {
//       ConsoleManager::printLine(error);   // show what went wrong
//       continue;                           // stay uninitialized
//   }
//   initialized = true;                    // unlock other commands
//
// Parameters:
//   path         — always "config.txt" (relative to the working directory)
//   out          — filled with parsed settings on success; untouched on failure
//   errorMessage — human-readable failure reason on error; cleared on success
//
// Returns true on success, false on any error.
bool ConfigLoader::loadFromFile(const std::string& path, Config& out, std::string& errorMessage) {
    
    // Step 1: Open the file
    // If config.txt doesn't exist or can't be read, fail immediately.
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMessage = "Could not open config file: " + path;
        return false;
    }

    // Use a temporary Config so we only overwrite `out` on full success.
    // If anything fails partway through, `out` stays untouched.
    Config parsed;
    std::string key;

    // Step 2: Read KEY VALUE pairs until end of file
    //
    // operator>> skips all whitespace between tokens, so blank lines and
    // extra spaces between key and value are handled automatically.
    //
    // Each iteration: read one key word, then read its value token,
    // then dispatch to the matching validation block below.
    while (file >> key) {
        std::string value;
        if (!readValueToken(file, value)) {
            errorMessage = "Missing value for config parameter: " + key;
            return false;
        }

        // Dispatch: match key to its config field, validate, and store.
        // Any key not in this list is rejected — no silent ignoring of typos.
        if (key == "num-cpu") {
            // Number of CPU cores to simulate. Must be between 1 and 128.
            // Stored as int because Scheduler uses it in signed arithmetic.
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
            // Scheduling algorithm: must be "fcfs" (First-Come First-Served)
            // or "rr" (Round Robin). Any other string is rejected.
            if (!parseSchedulerValue(value, parsed.scheduler)) {
                errorMessage = "Scheduler must be \"fcfs\" or \"rr\".";
                return false;
            }

        } else if (key == "quantum-cycles") {
            // Time slice size for Round Robin: how many CPU ticks a process
            // gets before being preempted. Must be at least 1.
            // (This field is required even when using FCFS, for simplicity.)
            if (!parseUint32(value, parsed.quantumCycles) || parsed.quantumCycles < 1) {
                errorMessage = "Invalid quantum-cycles value.";
                return false;
            }

        } else if (key == "batch-process-freq") {
            // How many global CPU ticks pass between auto-spawning a new process
            // once "scheduler-start" is running. Must be at least 1.
            // Example: 5 means one new process every 5 ticks.
            if (!parseUint32(value, parsed.batchProcessFreq) || parsed.batchProcessFreq < 1) {
                errorMessage = "Invalid batch-process-freq value.";
                return false;
            }

        } else if (key == "min-ins") {
            // Minimum number of PRINT instructions assigned to each generated process.
            // The actual count is a random number between min-ins and max-ins.
            // Must be at least 1 (a process with 0 instructions would finish instantly).
            if (!parseUint32(value, parsed.minIns) || parsed.minIns < 1) {
                errorMessage = "Invalid min-ins value.";
                return false;
            }

        } else if (key == "max-ins") {
            // Maximum number of PRINT instructions assigned to each generated process.
            // Must be at least 1. Cross-field check (min <= max) happens after the loop.
            if (!parseUint32(value, parsed.maxIns) || parsed.maxIns < 1) {
                errorMessage = "Invalid max-ins value.";
                return false;
            }

        } else if (key == "delay-per-exec" || key == "delays-per-exec") {
            // Extra CPU ticks to idle between executing consecutive instructions.
            // 0 is valid and means no artificial delay (run as fast as possible).
            // Higher values slow execution and make timing easier to observe.
            // Both "delay-per-exec" and "delays-per-exec" spellings are accepted.
            if (!parseUint32(value, parsed.delayPerExec)) {
                errorMessage = "Invalid delay-per-exec value.";
                return false;
            }

        } else if (key == "initial-process-count") {
            // OPTIONAL: number of processes to spawn immediately when
            // "scheduler-start" is typed, before batch generation kicks in.
            // If this key is absent, the scheduler starts with an empty queue
            // and relies on batch-process-freq to populate it over time.
            // Range 1-128 if present.
            if (!parseUint32(value, parsed.initialProcessCount) ||
                parsed.initialProcessCount < 1 || parsed.initialProcessCount > 128) {
                errorMessage = "initial-process-count must be in range [1, 128].";
                return false;
            }

        } else if (key == "max-overall-mem") {
            // Total physical memory in bytes. Power of two in [64, 65536].
            if (!parseUint32(value, parsed.maxOverallMem) ||
                !isPowerOfTwoInRange(parsed.maxOverallMem)) {
                errorMessage = "max-overall-mem must be a power of two in [64, 65536].";
                return false;
            }

        } else if (key == "mem-per-frame") {
            // Frame size (= page size) in bytes. Power of two in [64, 65536].
            if (!parseUint32(value, parsed.memPerFrame) ||
                !isPowerOfTwoInRange(parsed.memPerFrame)) {
                errorMessage = "mem-per-frame must be a power of two in [64, 65536].";
                return false;
            }

        } else if (key == "min-mem-per-proc") {
            // Lower bound of per-process memory. Power of two in [64, 65536].
            if (!parseUint32(value, parsed.minMemPerProc) ||
                !isPowerOfTwoInRange(parsed.minMemPerProc)) {
                errorMessage = "min-mem-per-proc must be a power of two in [64, 65536].";
                return false;
            }

        } else if (key == "max-mem-per-proc") {
            // Upper bound of per-process memory. Power of two in [64, 65536].
            if (!parseUint32(value, parsed.maxMemPerProc) ||
                !isPowerOfTwoInRange(parsed.maxMemPerProc)) {
                errorMessage = "max-mem-per-proc must be a power of two in [64, 65536].";
                return false;
            }

        } else {
            // Unknown key — reject immediately so the user knows about typos.
            // Example: "nmu-cpu 4" would trigger this with "Unknown config parameter: nmu-cpu".
            errorMessage = "Unknown config parameter: " + key;
            return false;
        }
    }

    // Step 3: Cross-field validation
    // Rules that involve two fields and can only be checked after both are read.
    if (parsed.minIns > parsed.maxIns) {
        errorMessage = "min-ins cannot be greater than max-ins.";
        return false;
    }
    if (parsed.minMemPerProc > parsed.maxMemPerProc) {
        errorMessage = "min-mem-per-proc cannot be greater than max-mem-per-proc.";
        return false;
    }
    if (parsed.memPerFrame > parsed.maxOverallMem) {
        errorMessage = "mem-per-frame cannot be greater than max-overall-mem.";
        return false;
    }

    // Step 4: Presence check — every required key must have appeared in the file
    //
    // If a required key was never read, its field is still 0 (the default).
    // We detect this and report the specific missing key so the user knows
    // exactly what to add rather than getting a cryptic error later.
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
    if (parsed.maxOverallMem < 1) {
        errorMessage = "config.txt is missing max-overall-mem.";
        return false;
    }
    if (parsed.memPerFrame < 1) {
        errorMessage = "config.txt is missing mem-per-frame.";
        return false;
    }
    if (parsed.minMemPerProc < 1) {
        errorMessage = "config.txt is missing min-mem-per-proc.";
        return false;
    }
    if (parsed.maxMemPerProc < 1) {
        errorMessage = "config.txt is missing max-mem-per-proc.";
        return false;
    }
    // Note: initial-process-count is intentionally NOT checked here because
    // it is optional. A value of 0 simply means "no initial burst of processes".

    // Step 5: Success — hand the fully validated Config back to main.cpp
    parsed.loaded = true;
    out = parsed;
    errorMessage.clear();
    return true;
}

// Power-of-two check used by process-memory validation. Zero returns false.
bool isPowerOfTwo(uint32_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

// A per-process memory size is valid when it is a power of two within
// [2^6, 2^16] = [64, 65536] bytes (MCO2 spec). Used by screen -s / screen -c.
bool isValidProcessMemorySize(uint32_t bytes) {
    return bytes >= 64 && bytes <= 65536 && isPowerOfTwo(bytes);
}

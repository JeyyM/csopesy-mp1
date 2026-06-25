// TimeUtil is for time formatting

#include "TimeUtil.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace TimeUtil {

// formatNow — get current local time as a formatted string
//
// Steps:
//   1. Ask the OS for the current moment (system_clock)
//   2. Convert to local calendar time (year, month, day, hour, minute, second)
//   3. Format with put_time
//   4. Normalize AM/PM capitalization if the platform prints lowercase
std::string formatNow() {
    // Get the current time point from the system clock (wall-clock time).
    const auto now = std::chrono::system_clock::now();

    // Convert the time_point to a classic C time_t (seconds since epoch).
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    // Break time_t into local calendar fields: year, month, day, hour, etc.
    std::tm localTime{};

#if defined(_WIN32)
    // Windows-safe version: writes into localTime, does not use global state.
    localtime_s(&localTime, &time);
#else
    // POSIX version for Linux/Mac.
    localtime_r(&time, &localTime);
#endif

    // Build the formatted string using C++ streams.
    // Expected output examples:
    //   06/19/2026 07:14:47PM returned by formatNow()
    //   (06/19/2026 07:14:47PM) Core:0 "Hello world from process01!" in processXX.txt
    //   process01  (06/19/2026 07:14:47PM)  Finished  100 / 100 in screen -ls
    std::ostringstream stream;
    stream << std::put_time(&localTime, "%m/%d/%Y %I:%M:%S%p");
    std::string formatted = stream.str();

    // Uppercase AM/PM, so we normalize the last letter.
    if (!formatted.empty() && formatted.back() == 'M' && formatted.size() > 2 &&
        formatted[formatted.size() - 3] != 'A' && formatted[formatted.size() - 3] != 'P') {
        if (formatted[formatted.size() - 2] == 'm') {
            formatted[formatted.size() - 2] = 'M';
        }
    }

    return formatted;
}

}  // namespace TimeUtil

#include "TimeUtil.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace TimeUtil {

std::string formatNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%m/%d/%Y %I:%M:%S%p");
    std::string formatted = stream.str();
    if (!formatted.empty() && formatted.back() == 'M' && formatted.size() > 2 &&
        formatted[formatted.size() - 3] != 'A' && formatted[formatted.size() - 3] != 'P') {
        // Some platforms emit am/pm lowercase; normalize to AM/PM style from mockups.
        if (formatted[formatted.size() - 2] == 'm') {
            formatted[formatted.size() - 2] = 'M';
        }
    }
    return formatted;
}

}  // namespace

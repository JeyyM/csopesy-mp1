// TimeUtil provides a single function for getting the current local time
// as a formatted string used in process logs and screen -ls output.
// Example output: "06/19/2026 07:14:47PM"

#pragma once

#include <string>

namespace TimeUtil {

    // Returns the current local time formatted as MM/DD/YYYY HH:MM:SSAM/PM.
    std::string formatNow();

}

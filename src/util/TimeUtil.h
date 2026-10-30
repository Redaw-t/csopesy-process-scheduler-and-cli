#pragma once

#include <ctime>
#include <string>

namespace timeutil {

std::time_t nowTime();

// Local time formatted as "MM/DD/YYYY HH:MM:SSAM", e.g. "01/18/2024 09:15:22AM".
std::string formatTimestamp(std::time_t when);
std::string nowTimestamp();

// Current local date formatted as "MM-DD-YYYY".
std::string todayDate();

}  // namespace timeutil

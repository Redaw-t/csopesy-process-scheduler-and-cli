#include "util/TimeUtil.h"

#include <chrono>

namespace timeutil {

namespace {

std::tm toLocal(std::time_t when) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &when);
#else
    localtime_r(&when, &result);
#endif
    return result;
}

std::string format(std::time_t when, const char* pattern) {
    const std::tm t = toLocal(when);
    char buffer[64];
    const std::size_t written = std::strftime(buffer, sizeof(buffer), pattern, &t);
    return std::string(buffer, written);
}

}  // namespace

std::time_t nowTime() { return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()); }

std::string formatTimestamp(std::time_t when) { return format(when, "%m/%d/%Y %I:%M:%S%p"); }

std::string nowTimestamp() { return formatTimestamp(nowTime()); }

std::string todayDate() { return format(nowTime(), "%m-%d-%Y"); }

}  // namespace timeutil

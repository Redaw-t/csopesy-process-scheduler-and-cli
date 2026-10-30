#include "util/PathUtil.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <system_error>

namespace pathutil {

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
#else
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        return {};
    }
    return exe.parent_path();
#endif
}

}  // namespace pathutil

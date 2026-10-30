#pragma once

#include <filesystem>

namespace pathutil {

// Directory containing the running executable (empty path if it cannot be determined).
std::filesystem::path executableDirectory();

}  // namespace pathutil

#pragma once

#include <string>
#include <vector>

// Details shown in the console header. Edit these to match your group.
namespace appinfo {

inline const std::vector<std::string>& developers() {
    static const std::vector<std::string> names = {
        "Jann Miro Quilantang",
        "VL Kirsten Camille Saguin",
        "Nathaniel Singh",
        "Nathan Trinidad",
    };
    return names;
}

inline const char* lastUpdated() { return "10-29-2026"; }

inline const char* banner() {
    return R"(  ____ ____   ___  ____  _____ ______   __
 / ___/ ___| / _ \|  _ \| ____/ ___\ \ / /
| |   \___ \| | | | |_) |  _| \___ \\ V /
| |___ ___) | |_| |  __/| |___ ___) || |
 \____|____/ \___/|_|   |_____|____/ |_|
)";
}

}  // namespace appinfo

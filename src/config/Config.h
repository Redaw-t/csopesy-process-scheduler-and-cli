#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SchedulerType { FCFS, RR };

// Emulator parameters read from config.txt. Defaults match the reference config.
struct Config {
    uint32_t numCpu = 4;                         // [1, 128]
    SchedulerType scheduler = SchedulerType::RR;  // "fcfs" or "rr"
    uint64_t quantumCycles = 5;                  // [1, 2^32]
    uint64_t batchProcessFreq = 1;               // [1, 2^32]
    uint64_t minIns = 1000;                      // [1, 2^32]
    uint64_t maxIns = 2000;                      // [1, 2^32]
    uint64_t delayPerExec = 0;                   // [0, 2^32]

    // Optional: real time per CPU tick in milliseconds, [0, 1000]. 0 runs ticks as fast as possible.
    uint64_t tickMs = 10;
};

struct ConfigLoadResult {
    bool ok = false;
    Config config;
    std::string sourcePath;             // file that was read (absolute)
    std::vector<std::string> warnings;  // non-fatal issues, e.g. missing keys
    std::vector<std::string> errors;    // fatal issues; config must not be used
};

class ConfigLoader {
public:
    static constexpr const char* kFileName = "config.txt";

    // Parses a config file at an explicit path.
    static ConfigLoadResult loadFromFile(const std::string& path);

    // Parses config text directly (used by loadFromFile and by tests).
    static ConfigLoadResult loadFromString(const std::string& text);

    // Looks for config.txt in the working directory, then next to the executable,
    // then in the executable's parent directory (the project root for CMake builds).
    static ConfigLoadResult loadFromDefaultLocations();

    static std::string schedulerName(SchedulerType type);
};

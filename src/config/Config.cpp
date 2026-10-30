#include "config/Config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "util/PathUtil.h"

namespace fs = std::filesystem;

namespace {

constexpr uint64_t kMaxValue = 4294967296ULL;  // 2^32
constexpr uint64_t kMaxCpu = 128;
constexpr uint64_t kMaxTickMs = 1000;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string stripQuotes(const std::string& s) {
    if (s.size() >= 2) {
        const char first = s.front();
        const char last = s.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

// Parses a non-negative integer, rejecting signs, decimals and trailing junk.
bool parseUnsigned(const std::string& text, uint64_t& out) {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

// Accepted spellings for each parameter, mapped to a canonical key.
const std::map<std::string, std::string>& keyAliases() {
    static const std::map<std::string, std::string> aliases = {
        {"num-cpu", "num-cpu"},
        {"scheduler", "scheduler"},
        {"quantum-cycles", "quantum-cycles"},
        {"batch-process-freq", "batch-process-freq"},
        {"min-ins", "min-ins"},
        {"max-ins", "max-ins"},
        {"delay-per-exec", "delay-per-exec"},
        {"delays-per-exec", "delay-per-exec"},
        {"tick-ms", "tick-ms"},
    };
    return aliases;
}

const std::vector<std::string>& canonicalKeys() {
    static const std::vector<std::string> keys = {"num-cpu",        "scheduler", "quantum-cycles",
                                                  "batch-process-freq", "min-ins", "max-ins",
                                                  "delay-per-exec"};
    return keys;
}

}  // namespace

ConfigLoadResult ConfigLoader::loadFromString(const std::string& text) {
    ConfigLoadResult result;
    Config& cfg = result.config;

    // Collect whitespace-separated tokens, ignoring anything after '#' on a line.
    std::vector<std::string> tokens;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream words(line);
        std::string word;
        while (words >> word) {
            tokens.push_back(word);
        }
    }

    if (tokens.size() % 2 != 0) {
        result.errors.push_back("Parameter \"" + tokens.back() + "\" has no value.");
        tokens.pop_back();
    }

    std::set<std::string> seen;
    for (std::size_t i = 0; i + 1 < tokens.size(); i += 2) {
        const std::string rawKey = toLower(tokens[i]);
        const std::string rawValue = stripQuotes(tokens[i + 1]);

        const auto alias = keyAliases().find(rawKey);
        if (alias == keyAliases().end()) {
            result.warnings.push_back("Unknown parameter \"" + tokens[i] + "\" ignored.");
            continue;
        }
        const std::string& key = alias->second;
        if (!seen.insert(key).second) {
            result.warnings.push_back("Parameter \"" + key + "\" set more than once; last value used.");
        }

        if (key == "scheduler") {
            const std::string name = toLower(rawValue);
            if (name == "fcfs") {
                cfg.scheduler = SchedulerType::FCFS;
            } else if (name == "rr") {
                cfg.scheduler = SchedulerType::RR;
            } else {
                result.errors.push_back("scheduler must be \"fcfs\" or \"rr\" (got \"" + rawValue + "\").");
            }
            continue;
        }

        uint64_t value = 0;
        if (!parseUnsigned(rawValue, value)) {
            result.errors.push_back(key + " must be a non-negative integer (got \"" + rawValue + "\").");
            continue;
        }

        const uint64_t minAllowed = (key == "delay-per-exec" || key == "tick-ms") ? 0 : 1;
        uint64_t maxAllowed = kMaxValue;
        if (key == "num-cpu") {
            maxAllowed = kMaxCpu;
        } else if (key == "tick-ms") {
            maxAllowed = kMaxTickMs;
        }
        if (value < minAllowed || value > maxAllowed) {
            result.errors.push_back(key + " must be in the range [" + std::to_string(minAllowed) + ", " +
                                    std::to_string(maxAllowed) + "] (got " + std::to_string(value) + ").");
            continue;
        }

        if (key == "num-cpu") {
            cfg.numCpu = static_cast<uint32_t>(value);
        } else if (key == "quantum-cycles") {
            cfg.quantumCycles = value;
        } else if (key == "batch-process-freq") {
            cfg.batchProcessFreq = value;
        } else if (key == "min-ins") {
            cfg.minIns = value;
        } else if (key == "max-ins") {
            cfg.maxIns = value;
        } else if (key == "delay-per-exec") {
            cfg.delayPerExec = value;
        } else if (key == "tick-ms") {
            cfg.tickMs = value;
        }
    }

    for (const auto& key : canonicalKeys()) {
        if (seen.count(key) == 0) {
            result.warnings.push_back("Parameter \"" + key + "\" not found; using default value.");
        }
    }

    if (cfg.minIns > cfg.maxIns) {
        result.errors.push_back("min-ins (" + std::to_string(cfg.minIns) + ") cannot be greater than max-ins (" +
                                std::to_string(cfg.maxIns) + ").");
    }

    result.ok = result.errors.empty();
    return result;
}

ConfigLoadResult ConfigLoader::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        ConfigLoadResult result;
        result.sourcePath = path;
        result.errors.push_back("Could not open \"" + path + "\".");
        return result;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();

    ConfigLoadResult result = loadFromString(buffer.str());
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    result.sourcePath = ec ? path : absolute.lexically_normal().string();
    return result;
}

ConfigLoadResult ConfigLoader::loadFromDefaultLocations() {
    std::vector<fs::path> candidates;
    std::error_code ec;
    candidates.push_back(fs::current_path(ec) / kFileName);
    const fs::path exeDir = pathutil::executableDirectory();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / kFileName);
        candidates.push_back(exeDir.parent_path() / kFileName);
    }

    for (const auto& candidate : candidates) {
        if (fs::is_regular_file(candidate, ec)) {
            return loadFromFile(candidate.string());
        }
    }

    ConfigLoadResult result;
    std::string searched;
    for (const auto& candidate : candidates) {
        searched += "\n  " + candidate.lexically_normal().string();
    }
    result.errors.push_back(std::string("Could not find ") + kFileName + ". Looked in:" + searched);
    return result;
}

std::string ConfigLoader::schedulerName(SchedulerType type) {
    return type == SchedulerType::FCFS ? "fcfs" : "rr";
}

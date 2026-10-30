#pragma once

#include <string>
#include <vector>

// A command line split into a command name and its arguments.
struct ParsedCommand {
    std::string name;               // lower-cased command name, e.g. "screen"
    std::vector<std::string> args;  // remaining tokens, case preserved
    bool empty() const { return name.empty(); }
};

class CommandParser {
public:
    // Splits on whitespace. Double quotes group words into a single argument.
    static std::vector<std::string> tokenize(const std::string& line);
    static ParsedCommand parse(const std::string& line);
};

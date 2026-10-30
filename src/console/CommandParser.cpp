#include "console/CommandParser.h"

#include <algorithm>
#include <cctype>

std::vector<std::string> CommandParser::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    bool hasToken = false;

    for (const char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
            hasToken = true;
        } else if (!inQuotes && std::isspace(static_cast<unsigned char>(c))) {
            if (hasToken) {
                tokens.push_back(current);
                current.clear();
                hasToken = false;
            }
        } else {
            current += c;
            hasToken = true;
        }
    }
    if (hasToken) {
        tokens.push_back(current);
    }
    return tokens;
}

ParsedCommand CommandParser::parse(const std::string& line) {
    ParsedCommand cmd;
    std::vector<std::string> tokens = tokenize(line);
    if (tokens.empty()) {
        return cmd;
    }
    cmd.name = tokens.front();
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    cmd.args.assign(tokens.begin() + 1, tokens.end());
    return cmd;
}

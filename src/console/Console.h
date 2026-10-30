#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "console/CommandParser.h"

class Process;
class Scheduler;

// Main menu console: prints the header, reads commands and dispatches them.
// "screen -s" / "screen -r" switch to a per-process screen until "exit".
class Console {
public:
    Console(std::istream& in, std::ostream& out);
    ~Console();

    // Runs the read-eval loop until "exit" or end of input. Returns the process exit code.
    int run();

    bool isInitialized() const { return initialized_; }
    const Config& config() const { return config_; }

private:
    using Handler = std::function<void(const std::vector<std::string>& args)>;

    struct CommandInfo {
        Handler handler;
        bool requiresInitialize;
        std::string usage;
    };

    void registerCommands();
    void dispatch(const ParsedCommand& cmd);

    void printHeader();
    void clearScreen();
    void printPrompt();

    void cmdInitialize(const std::vector<std::string>& args);
    void cmdExit(const std::vector<std::string>& args);
    void cmdClear(const std::vector<std::string>& args);
    void cmdHelp(const std::vector<std::string>& args);
    void cmdScreen(const std::vector<std::string>& args);
    void cmdSchedulerStart(const std::vector<std::string>& args);
    void cmdSchedulerStop(const std::vector<std::string>& args);
    void cmdReportUtil(const std::vector<std::string>& args);

    void printConfig() const;

    // Process screen: runs until the user types "exit" (or input ends).
    void attachScreen(const std::shared_ptr<Process>& process);
    void printProcessSmi(const Process& process);

    std::istream& in_;
    std::ostream& out_;
    std::map<std::string, CommandInfo> commands_;
    bool running_ = true;
    bool initialized_ = false;
    Config config_;
    std::filesystem::path reportDirectory_;  // csopesy-log.txt is saved next to the loaded config.txt
    std::unique_ptr<Scheduler> scheduler_;
};

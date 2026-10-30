#include "console/Console.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

#include "console/AppInfo.h"
#include "process/Process.h"
#include "report/UtilizationReport.h"
#include "scheduler/Scheduler.h"

namespace fs = std::filesystem;

namespace {

constexpr const char* kReportFileName = "csopesy-log.txt";
constexpr const char* kPrompt = "root:\\> ";
constexpr const char* kDivider = "------------------------------------------";

}  // namespace

Console::Console(std::istream& in, std::ostream& out) : in_(in), out_(out) { registerCommands(); }

Console::~Console() {
    if (scheduler_) {
        scheduler_->shutdown();
    }
}

void Console::registerCommands() {
    using Args = const std::vector<std::string>&;
    commands_["initialize"] = {[this](Args a) { cmdInitialize(a); }, false, "initialize"};
    commands_["exit"] = {[this](Args a) { cmdExit(a); }, false, "exit"};
    commands_["clear"] = {[this](Args a) { cmdClear(a); }, true, "clear"};
    commands_["cls"] = {[this](Args a) { cmdClear(a); }, true, "cls"};
    commands_["help"] = {[this](Args a) { cmdHelp(a); }, false, "help"};
    commands_["screen"] = {[this](Args a) { cmdScreen(a); }, true,
                           "screen -s <process name> | screen -r <process name> | screen -ls"};
    commands_["scheduler-start"] = {[this](Args a) { cmdSchedulerStart(a); }, true, "scheduler-start"};
    commands_["scheduler-stop"] = {[this](Args a) { cmdSchedulerStop(a); }, true, "scheduler-stop"};
    commands_["report-util"] = {[this](Args a) { cmdReportUtil(a); }, true, "report-util"};
}

int Console::run() {
    printHeader();
    std::string line;
    while (running_) {
        printPrompt();
        if (!std::getline(in_, line)) {
            out_ << "\n";
            break;
        }
        const ParsedCommand cmd = CommandParser::parse(line);
        if (cmd.empty()) {
            continue;
        }
        dispatch(cmd);
    }
    if (scheduler_) {
        scheduler_->shutdown();
    }
    return 0;
}

void Console::dispatch(const ParsedCommand& cmd) {
    const auto it = commands_.find(cmd.name);
    if (it == commands_.end()) {
        out_ << "'" << cmd.name << "' is not recognized as a command. Type \"help\" to see available commands.\n";
        return;
    }
    if (it->second.requiresInitialize && !initialized_) {
        out_ << "Please run \"initialize\" first.\n";
        return;
    }
    it->second.handler(cmd.args);
}

void Console::printHeader() {
    out_ << appinfo::banner();
    out_ << kDivider << "\n";
    out_ << "Welcome to CSOPESY Emulator!\n\n";
    out_ << "Developers:\n";
    for (const auto& name : appinfo::developers()) {
        out_ << "  " << name << "\n";
    }
    out_ << "\nLast updated: " << appinfo::lastUpdated() << "\n";
    out_ << kDivider << "\n";
    if (!initialized_) {
        out_ << "Type \"initialize\" to load config.txt, or \"exit\" to quit.\n";
    }
    out_ << "\n";
}

void Console::clearScreen() {
    out_.flush();
#ifdef _WIN32
    std::system("cls");
#else
    out_ << "\033[2J\033[H" << std::flush;
#endif
}

void Console::printPrompt() { out_ << kPrompt << std::flush; }

void Console::cmdInitialize(const std::vector<std::string>& args) {
    if (!args.empty()) {
        out_ << "Usage: initialize\n";
        return;
    }
    if (initialized_) {
        out_ << "Emulator is already initialized. Restart the emulator to load a new config.txt.\n";
        return;
    }

    const ConfigLoadResult result = ConfigLoader::loadFromDefaultLocations();
    for (const auto& warning : result.warnings) {
        out_ << "Warning: " << warning << "\n";
    }
    if (!result.ok) {
        for (const auto& error : result.errors) {
            out_ << "Error: " << error << "\n";
        }
        out_ << "Initialization failed. Fix config.txt and run \"initialize\" again.\n";
        return;
    }

    config_ = result.config;
    reportDirectory_ = fs::path(result.sourcePath).parent_path();
    scheduler_ = std::make_unique<Scheduler>(config_);
    scheduler_->start();
    initialized_ = true;
    out_ << "Loaded configuration from " << result.sourcePath << "\n";
    printConfig();
    out_ << "Emulator initialized. CPU scheduler is running.\n";
}

void Console::printConfig() const {
    out_ << "  num-cpu            " << config_.numCpu << "\n";
    out_ << "  scheduler          " << ConfigLoader::schedulerName(config_.scheduler) << "\n";
    out_ << "  quantum-cycles     " << config_.quantumCycles << "\n";
    out_ << "  batch-process-freq " << config_.batchProcessFreq << "\n";
    out_ << "  min-ins            " << config_.minIns << "\n";
    out_ << "  max-ins            " << config_.maxIns << "\n";
    out_ << "  delay-per-exec     " << config_.delayPerExec << "\n";
    out_ << "  tick-ms            " << config_.tickMs << "\n";
}

void Console::cmdExit(const std::vector<std::string>& /*args*/) { running_ = false; }

void Console::cmdClear(const std::vector<std::string>& /*args*/) {
    clearScreen();
    printHeader();
}

void Console::cmdHelp(const std::vector<std::string>& /*args*/) {
    out_ << "Available commands:\n";
    out_ << "  initialize                 Load config.txt (required before other commands)\n";
    out_ << "  screen -s <process name>   Create a process and attach to its screen\n";
    out_ << "  screen -r <process name>   Re-attach to a running process\n";
    out_ << "  screen -ls                 Show CPU utilization and process list\n";
    out_ << "  scheduler-start            Start generating dummy processes\n";
    out_ << "  scheduler-stop             Stop generating dummy processes\n";
    out_ << "  report-util                Save CPU utilization report to csopesy-log.txt\n";
    out_ << "  clear                      Clear the screen\n";
    out_ << "  exit                       Quit the emulator\n";
}

void Console::cmdScreen(const std::vector<std::string>& args) {
    const std::string usage = "Usage: " + commands_.at("screen").usage + "\n";
    if (args.empty()) {
        out_ << usage;
        return;
    }
    const std::string& flag = args[0];
    if (flag == "-ls") {
        if (args.size() != 1) {
            out_ << usage;
            return;
        }
        out_ << report::buildUtilizationReport(scheduler_->snapshot());
        return;
    }
    if (flag == "-s" || flag == "-r") {
        if (args.size() != 2) {
            out_ << usage;
            return;
        }
        const std::string& name = args[1];
        if (flag == "-s") {
            const auto process = scheduler_->createProcess(name);
            if (!process) {
                out_ << "Process " << name << " already exists. Use \"screen -r " << name
                     << "\" to attach to it.\n";
                return;
            }
            attachScreen(process);
        } else {
            const auto process = scheduler_->findProcess(name);
            if (!process || process->isFinished()) {
                out_ << "Process " << name << " not found.\n";
                return;
            }
            attachScreen(process);
        }
        return;
    }
    out_ << usage;
}

void Console::attachScreen(const std::shared_ptr<Process>& process) {
    clearScreen();
    printProcessSmi(*process);

    std::string line;
    for (;;) {
        out_ << "\n" << kPrompt << std::flush;
        if (!std::getline(in_, line)) {
            out_ << "\n";
            running_ = false;
            return;
        }
        const ParsedCommand cmd = CommandParser::parse(line);
        if (cmd.empty()) {
            continue;
        }
        if (cmd.name == "process-smi") {
            out_ << "\n";
            printProcessSmi(*process);
        } else if (cmd.name == "exit") {
            clearScreen();
            printHeader();
            return;
        } else if (cmd.name == "clear" || cmd.name == "cls") {
            clearScreen();
        } else {
            out_ << "'" << cmd.name << "' is not recognized inside a process screen. "
                 << "Available commands: process-smi, exit\n";
        }
    }
}

void Console::printProcessSmi(const Process& process) {
    const ProcessSnapshot snap = process.snapshot();
    const std::vector<LogEntry> logs = process.logs();

    out_ << "Process name: " << snap.name << "\n";
    out_ << "ID: " << snap.id << "\n";
    out_ << "Logs:\n";
    for (const auto& entry : logs) {
        out_ << "(" << entry.timestamp << ") Core:" << entry.coreId << " \"" << entry.message << "\"\n";
    }
    out_ << "\n";
    if (snap.state == ProcessState::Finished) {
        out_ << "Finished!\n";
    } else {
        out_ << "Current instruction line: " << snap.executed << "\n";
        out_ << "Lines of code: " << snap.total << "\n";
    }
}

void Console::cmdSchedulerStart(const std::vector<std::string>& /*args*/) {
    if (!scheduler_->startGenerating()) {
        out_ << "Scheduler is already generating processes.\n";
        return;
    }
    out_ << "Scheduler started: generating a new process every " << config_.batchProcessFreq
         << " CPU tick(s).\n";
}

void Console::cmdSchedulerStop(const std::vector<std::string>& /*args*/) {
    if (!scheduler_->stopGenerating()) {
        out_ << "Scheduler is not generating processes.\n";
        return;
    }
    out_ << "Scheduler stopped generating processes.\n";
}

void Console::cmdReportUtil(const std::vector<std::string>& args) {
    if (!args.empty()) {
        out_ << "Usage: report-util\n";
        return;
    }
    const fs::path path = (reportDirectory_.empty() ? fs::path(".") : reportDirectory_) / kReportFileName;
    const std::string text = report::buildUtilizationReport(scheduler_->snapshot());

    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file) {
        out_ << "Error: could not write " << path.string() << "\n";
        return;
    }
    file << text;
    file.close();
    if (!file) {
        out_ << "Error: could not write " << path.string() << "\n";
        return;
    }

    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    out_ << "Report generated at " << (ec ? path : absolute.lexically_normal()).string() << "!\n";
}

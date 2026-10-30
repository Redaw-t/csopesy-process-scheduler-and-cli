#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "process/Instruction.h"

enum class ProcessState { Ready, Running, Sleeping, Finished };

// A line produced by a PRINT instruction.
struct LogEntry {
    std::string timestamp;  // "MM/DD/YYYY HH:MM:SSAM"
    int coreId = -1;
    std::string message;
};

// What happened when a process executed one instruction.
struct StepOutcome {
    enum class Kind {
        Executed,  // an instruction ran; the process can keep its core
        Slept,     // a SLEEP ran; the process must give up its core for sleepTicks ticks
        Finished,  // the last instruction ran (or nothing was left); the process is done
    };
    Kind kind = Kind::Executed;
    uint8_t sleepTicks = 0;
};

// Point-in-time copy of a process's details, safe to read while the process keeps running.
struct ProcessSnapshot {
    uint32_t id = 0;
    std::string name;
    ProcessState state = ProcessState::Ready;
    std::string createdAt;
    std::string finishedAt;
    int coreId = -1;
    uint64_t executed = 0;
    uint64_t total = 0;
};

// A process: its program, variables, execution position and PRINT logs.
// All public methods are thread-safe.
class Process {
public:
    static constexpr int kMaxLoopNesting = 3;

    using ProgramLoader = std::function<std::vector<Instruction>()>;

    // Process with its program already built.
    Process(uint32_t id, std::string name, std::vector<Instruction> program);

    // Process whose program is built by 'loader' the first time it executes. The loader
    // must produce a program that executes exactly 'totalInstructions' instructions.
    Process(uint32_t id, std::string name, uint64_t totalInstructions, ProgramLoader loader);

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // Executes exactly one instruction on the given core. FOR loops are entered and
    // exited transparently, so every call runs one PRINT/DECLARE/ADD/SUBTRACT/SLEEP.
    StepOutcome executeNext(int coreId);

    uint32_t id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& createdAt() const { return createdAt_; }

    ProcessState state() const;
    void setState(ProcessState state);
    bool isFinished() const;

    int coreId() const;  // core it is running on / last ran on, -1 if never ran
    void setCoreId(int coreId);

    // Busy-wait ticks still owed before the next instruction (kept across preemption).
    uint64_t pendingDelay() const;
    void setPendingDelay(uint64_t ticks);

    uint64_t executedInstructions() const;                             // "current instruction line"
    uint64_t totalInstructions() const { return totalInstructions_; }  // "lines of code"

    // Frees the program while the process is off the CPU (only for processes created with a
    // loader). The execution position and variables are kept; the program is rebuilt from the
    // loader the next time the process executes, and continues where it stopped.
    void unloadProgram();
    bool isProgramLoaded() const;

    std::vector<LogEntry> logs() const;
    std::size_t logCount() const;
    std::optional<uint16_t> variable(const std::string& name) const;
    std::string finishedAt() const;

    ProcessSnapshot snapshot() const;

private:
    struct Frame {
        const std::vector<Instruction>* block;
        std::size_t pc;
        uint64_t remaining;  // passes left through this block, including the current one
    };

    void loadProgramLocked();
    // Moves the execution position onto the next non-FOR instruction.
    // Returns nullptr when the program has no instructions left. Caller holds mutex_.
    const Instruction* seekNextLocked();
    void finishLocked();

    uint16_t readOperandLocked(const Operand& operand);
    uint16_t& variableLocked(const std::string& name);
    void execute(const Instruction& ins, int coreId, StepOutcome& outcome);

    const uint32_t id_;
    const std::string name_;
    const std::string createdAt_;
    const uint64_t totalInstructions_;

    // Compact log record; text is empty for the default "Hello world from <name>!" message.
    struct RawLog {
        std::time_t when;
        int coreId;
        std::string text;
    };

    mutable std::mutex mutex_;
    ProgramLoader loader_;
    bool loaded_ = false;   // program_ currently holds the program
    bool started_ = false;  // stack_ holds a valid execution position
    std::vector<Instruction> program_;
    std::vector<Frame> stack_;
    std::unordered_map<std::string, uint16_t> variables_;
    std::vector<RawLog> logs_;
    ProcessState state_ = ProcessState::Ready;
    int coreId_ = -1;
    uint64_t executed_ = 0;
    uint64_t pendingDelay_ = 0;
    std::string finishedAt_;
};

std::string processStateName(ProcessState state);

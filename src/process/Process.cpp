#include "process/Process.h"

#include <cstdint>
#include <utility>

#include "process/InstructionGenerator.h"
#include "util/TimeUtil.h"

namespace {

constexpr int32_t kUint16Max = UINT16_MAX;

uint16_t clampToUint16(int64_t value) {
    if (value < 0) {
        return 0;
    }
    if (value > kUint16Max) {
        return static_cast<uint16_t>(kUint16Max);
    }
    return static_cast<uint16_t>(value);
}

}  // namespace

Process::Process(uint32_t id, std::string name, std::vector<Instruction> program)
    : id_(id),
      name_(std::move(name)),
      createdAt_(timeutil::nowTimestamp()),
      totalInstructions_(executedInstructionCount(program)),
      loaded_(true),
      started_(true),
      program_(std::move(program)) {
    stack_.push_back(Frame{&program_, 0, 1});
}

Process::Process(uint32_t id, std::string name, uint64_t totalInstructions, ProgramLoader loader)
    : id_(id),
      name_(std::move(name)),
      createdAt_(timeutil::nowTimestamp()),
      totalInstructions_(totalInstructions),
      loader_(std::move(loader)) {}

void Process::loadProgramLocked() {
    if (loaded_) {
        return;
    }
    if (loader_) {
        program_ = loader_();
    }
    if (!started_) {
        stack_.push_back(Frame{&program_, 0, 1});
        started_ = true;
    } else {
        // Rebuilt program: point each frame at its block again. A nested frame's block is the
        // body of the FOR its parent just passed (the parent's pc was advanced past it).
        stack_.front().block = &program_;
        for (std::size_t i = 1; i < stack_.size(); ++i) {
            const Frame& parent = stack_[i - 1];
            stack_[i].block = &(*parent.block)[parent.pc - 1].body;
        }
    }
    loaded_ = true;
}

void Process::unloadProgram() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loader_ || !loaded_ || state_ == ProcessState::Finished) {
        return;
    }
    std::vector<Instruction>().swap(program_);
    loaded_ = false;
}

bool Process::isProgramLoaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return loaded_;
}

const Instruction* Process::seekNextLocked() {
    while (!stack_.empty()) {
        Frame& top = stack_.back();
        if (top.pc < top.block->size()) {
            const Instruction& ins = (*top.block)[top.pc];
            if (ins.op != OpCode::For) {
                return &ins;
            }
            ++top.pc;
            if (ins.repeats > 0 && !ins.body.empty()) {
                // 'top' may be invalidated by push_back, so it is not used after this.
                stack_.push_back(Frame{&ins.body, 0, ins.repeats});
            }
            continue;
        }
        // End of block: repeat it or return to the enclosing block.
        if (--top.remaining > 0) {
            top.pc = 0;
        } else {
            stack_.pop_back();
        }
    }
    return nullptr;
}

StepOutcome Process::executeNext(int coreId) {
    std::lock_guard<std::mutex> lock(mutex_);
    StepOutcome outcome;

    if (state_ == ProcessState::Finished) {
        outcome.kind = StepOutcome::Kind::Finished;
        return outcome;
    }

    loadProgramLocked();
    coreId_ = coreId;

    const Instruction* ins = seekNextLocked();
    if (ins == nullptr) {
        finishLocked();
        outcome.kind = StepOutcome::Kind::Finished;
        return outcome;
    }

    ++stack_.back().pc;
    ++executed_;
    execute(*ins, coreId, outcome);

    if (seekNextLocked() == nullptr) {
        finishLocked();
        outcome.kind = StepOutcome::Kind::Finished;
    }
    return outcome;
}

void Process::execute(const Instruction& ins, int coreId, StepOutcome& outcome) {
    switch (ins.op) {
        case OpCode::Print: {
            // An empty text stands for the default message, which is only built when displayed.
            std::string text = ins.message;
            if (!ins.printVariable.empty()) {
                if (text.empty()) {
                    text = InstructionGenerator::defaultPrintMessage(name_);
                }
                text += std::to_string(variableLocked(ins.printVariable));
            }
            logs_.push_back(RawLog{timeutil::nowTime(), coreId, std::move(text)});
            break;
        }
        case OpCode::Declare:
            variableLocked(ins.target) = ins.value;
            break;
        case OpCode::Add: {
            const int64_t a = readOperandLocked(ins.lhs);
            const int64_t b = readOperandLocked(ins.rhs);
            variableLocked(ins.target) = clampToUint16(a + b);
            break;
        }
        case OpCode::Subtract: {
            const int64_t a = readOperandLocked(ins.lhs);
            const int64_t b = readOperandLocked(ins.rhs);
            variableLocked(ins.target) = clampToUint16(a - b);
            break;
        }
        case OpCode::Sleep:
            outcome.kind = StepOutcome::Kind::Slept;
            outcome.sleepTicks = ins.sleepTicks;
            break;
        case OpCode::For:
            // Loops are handled by seekNextLocked and never executed directly.
            break;
    }
}

uint16_t Process::readOperandLocked(const Operand& operand) {
    return operand.isVariable ? variableLocked(operand.name) : operand.literal;
}

uint16_t& Process::variableLocked(const std::string& name) {
    // operator[] declares a missing variable with a value of 0.
    return variables_[name];
}

void Process::finishLocked() {
    state_ = ProcessState::Finished;
    finishedAt_ = timeutil::nowTimestamp();
    pendingDelay_ = 0;
    // Program and variables are released once the process finishes; logs are kept.
    loader_ = nullptr;
    loaded_ = false;
    stack_.clear();
    std::vector<Instruction>().swap(program_);
    std::unordered_map<std::string, uint16_t>().swap(variables_);
}

ProcessState Process::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Process::setState(ProcessState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != ProcessState::Finished) {
        state_ = state;
    }
}

bool Process::isFinished() const { return state() == ProcessState::Finished; }

int Process::coreId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return coreId_;
}

void Process::setCoreId(int coreId) {
    std::lock_guard<std::mutex> lock(mutex_);
    coreId_ = coreId;
}

uint64_t Process::pendingDelay() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pendingDelay_;
}

void Process::setPendingDelay(uint64_t ticks) {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingDelay_ = ticks;
}

uint64_t Process::executedInstructions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return executed_;
}

std::vector<LogEntry> Process::logs() const {
    std::vector<RawLog> raw;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        raw = logs_;
    }
    const std::string defaultMessage = InstructionGenerator::defaultPrintMessage(name_);
    std::vector<LogEntry> out;
    out.reserve(raw.size());
    for (auto& entry : raw) {
        out.push_back(LogEntry{timeutil::formatTimestamp(entry.when), entry.coreId,
                               entry.text.empty() ? defaultMessage : std::move(entry.text)});
    }
    return out;
}

std::size_t Process::logCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logs_.size();
}

std::optional<uint16_t> Process::variable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = variables_.find(name);
    if (it == variables_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string Process::finishedAt() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return finishedAt_;
}

ProcessSnapshot Process::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ProcessSnapshot snap;
    snap.id = id_;
    snap.name = name_;
    snap.state = state_;
    snap.createdAt = createdAt_;
    snap.finishedAt = finishedAt_;
    snap.coreId = coreId_;
    snap.executed = executed_;
    snap.total = totalInstructions_;
    return snap;
}

std::string processStateName(ProcessState state) {
    switch (state) {
        case ProcessState::Ready:
            return "Ready";
        case ProcessState::Running:
            return "Running";
        case ProcessState::Sleeping:
            return "Sleeping";
        case ProcessState::Finished:
            return "Finished";
    }
    return "Unknown";
}

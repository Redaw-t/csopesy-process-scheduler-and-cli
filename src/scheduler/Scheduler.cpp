#include "scheduler/Scheduler.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

Scheduler::Scheduler(const Config& config, uint64_t seed)
    : config_(config), generator_(seed), cores_(config.numCpu) {}

Scheduler::~Scheduler() { shutdown(); }

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

void Scheduler::start() {
    if (running_.exchange(true)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(syncMutex_);
        stopCores_ = false;
    }
    coreThreads_.reserve(cores_.size());
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        coreThreads_.emplace_back(&Scheduler::coreLoop, this, static_cast<int>(i));
    }
    clockThread_ = std::thread(&Scheduler::clockLoop, this);
}

void Scheduler::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
    }
    clockWake_.notify_all();
    if (clockThread_.joinable()) {
        clockThread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(syncMutex_);
        stopCores_ = true;
    }
    coresGo_.notify_all();
    for (auto& t : coreThreads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    coreThreads_.clear();
}

void Scheduler::clockLoop() {
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::milliseconds(config_.tickMs);
    auto next = clock::now();

    while (running_.load()) {
        tick();

        if (config_.tickMs == 0) {
            std::this_thread::yield();
            continue;
        }
        next += period;
        const auto now = clock::now();
        if (next < now - period * 10) {
            next = now;  // fell far behind (e.g. machine was busy); don't burst to catch up
        }
        std::unique_lock<std::mutex> lock(clockMutex_);
        clockWake_.wait_until(lock, next, [this] { return !running_.load(); });
    }
}

void Scheduler::coreLoop(int coreId) {
    uint64_t seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(syncMutex_);
            coresGo_.wait(lock, [&] { return stopCores_ || generation_ != seen; });
            if (stopCores_) {
                return;
            }
            seen = generation_;
        }
        // The clock thread does not touch cores_ until every core reports back,
        // so this core's slot can be used without holding mutex_.
        runCore(cores_[static_cast<std::size_t>(coreId)], coreId);
        {
            std::lock_guard<std::mutex> lock(syncMutex_);
            if (--pendingCores_ == 0) {
                coresDone_.notify_one();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// One tick
// ---------------------------------------------------------------------------

void Scheduler::tickOnce() {
    if (!running_.load()) {
        tick();
    }
}

void Scheduler::tick() {
    bool anyBusy = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wakeSleepersLocked();
        dispatchLocked();
        for (const auto& core : cores_) {
            anyBusy = anyBusy || core.process != nullptr;
        }
    }

    if (anyBusy) {
        runCores();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (anyBusy) {
            handleOutcomesLocked();
        }
        generateBatchLocked();
        // Refill cores freed this tick right away, so reports between ticks never show
        // idle cores while processes are waiting.
        dispatchLocked();
        ++tick_;
    }
}

void Scheduler::runCores() {
    if (coreThreads_.empty()) {
        for (std::size_t i = 0; i < cores_.size(); ++i) {
            runCore(cores_[i], static_cast<int>(i));
        }
        return;
    }
    std::unique_lock<std::mutex> lock(syncMutex_);
    pendingCores_ = static_cast<uint32_t>(coreThreads_.size());
    ++generation_;
    coresGo_.notify_all();
    coresDone_.wait(lock, [&] { return pendingCores_ == 0; });
}

void Scheduler::runCore(Core& core, int coreId) {
    core.executed = false;
    if (!core.process) {
        return;
    }
    if (core.delayLeft > 0) {
        --core.delayLeft;  // busy-waiting: the process holds the core but runs nothing
        return;
    }
    core.outcome = core.process->executeNext(coreId);
    core.executed = true;
}

void Scheduler::wakeSleepersLocked() {
    const uint64_t now = tick_.load();
    auto it = sleeping_.begin();
    while (it != sleeping_.end()) {
        if (it->wakeTick <= now) {
            it->process->setState(ProcessState::Ready);
            ready_.push_back(it->process);
            it = sleeping_.erase(it);
        } else {
            ++it;
        }
    }
}

void Scheduler::dispatchLocked() {
    for (std::size_t i = 0; i < cores_.size() && !ready_.empty(); ++i) {
        Core& core = cores_[i];
        if (core.process) {
            continue;
        }
        core.process = ready_.front();
        ready_.pop_front();
        core.quantumUsed = 0;
        core.delayLeft = core.process->pendingDelay();
        core.process->setPendingDelay(0);
        core.process->setCoreId(static_cast<int>(i));
        core.process->setState(ProcessState::Running);
    }
}

void Scheduler::handleOutcomesLocked() {
    const uint64_t now = tick_.load();
    for (auto& core : cores_) {
        if (!core.process) {
            continue;
        }
        ++core.quantumUsed;

        if (core.executed) {
            switch (core.outcome.kind) {
                case StepOutcome::Kind::Finished:
                    finished_.push_back(core.process);
                    core.process.reset();
                    continue;
                case StepOutcome::Kind::Slept:
                    // SLEEP(X): the process stays off the CPU for the next X ticks.
                    core.process->setState(ProcessState::Sleeping);
                    core.process->unloadProgram();
                    sleeping_.push_back(SleepingEntry{now + 1 + core.outcome.sleepTicks, core.process});
                    core.process.reset();
                    continue;
                case StepOutcome::Kind::Executed:
                    core.delayLeft = config_.delayPerExec;
                    break;
            }
        }

        if (config_.scheduler == SchedulerType::RR && core.quantumUsed >= config_.quantumCycles) {
            if (ready_.empty()) {
                core.quantumUsed = 0;  // nobody waiting: keep running with a fresh quantum
            } else {
                core.process->setPendingDelay(core.delayLeft);
                core.process->setState(ProcessState::Ready);
                core.process->unloadProgram();
                ready_.push_back(core.process);
                core.process.reset();
            }
        }
    }
}

void Scheduler::generateBatchLocked() {
    if (!generating_) {
        return;
    }
    ++ticksSinceGenerationStart_;
    if (ticksSinceGenerationStart_ % config_.batchProcessFreq == 0) {
        enqueueNewLocked(makeProcessLocked(nextBatchNameLocked()));
    }
}

// ---------------------------------------------------------------------------
// Processes
// ---------------------------------------------------------------------------

std::shared_ptr<Process> Scheduler::makeProcessLocked(const std::string& name) {
    const ProgramSpec spec = generator_.pickSpec(config_.minIns, config_.maxIns);
    return std::make_shared<Process>(nextId_++, name, spec.instructionCount,
                                     [spec] { return InstructionGenerator::build(spec); });
}

bool Scheduler::enqueueNewLocked(const std::shared_ptr<Process>& process) {
    if (!byName_.emplace(process->name(), process).second) {
        return false;
    }
    process->setState(ProcessState::Ready);
    ready_.push_back(process);
    return true;
}

std::string Scheduler::nextBatchNameLocked() {
    for (;;) {
        ++batchCounter_;
        std::ostringstream name;
        name << 'p' << std::setw(2) << std::setfill('0') << batchCounter_;
        if (byName_.count(name.str()) == 0) {
            return name.str();
        }
    }
}

std::shared_ptr<Process> Scheduler::createProcess(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (byName_.count(name) != 0) {
        return nullptr;
    }
    auto process = makeProcessLocked(name);
    enqueueNewLocked(process);
    return process;
}

bool Scheduler::addProcess(const std::shared_ptr<Process>& process) {
    std::lock_guard<std::mutex> lock(mutex_);
    return enqueueNewLocked(process);
}

std::shared_ptr<Process> Scheduler::findProcess(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : it->second;
}

uint32_t Scheduler::nextProcessId() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextId_++;
}

bool Scheduler::startGenerating() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generating_) {
        return false;
    }
    generating_ = true;
    ticksSinceGenerationStart_ = 0;
    return true;
}

bool Scheduler::stopGenerating() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!generating_) {
        return false;
    }
    generating_ = false;
    return true;
}

bool Scheduler::isGenerating() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return generating_;
}

uint64_t Scheduler::currentTick() const { return tick_.load(); }

SchedulerSnapshot Scheduler::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    SchedulerSnapshot snap;
    snap.tick = tick_.load();
    snap.totalCores = static_cast<uint32_t>(cores_.size());
    for (std::size_t i = 0; i < cores_.size(); ++i) {
        if (cores_[i].process) {
            snap.running.push_back(RunningEntry{static_cast<int>(i), cores_[i].process});
        }
    }
    snap.busyCores = static_cast<uint32_t>(snap.running.size());
    snap.finished = finished_;
    snap.readyCount = ready_.size();
    snap.sleepingCount = sleeping_.size();
    snap.generating = generating_;
    return snap;
}

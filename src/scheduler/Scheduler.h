#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "config/Config.h"
#include "process/InstructionGenerator.h"
#include "process/Process.h"

// A process currently holding a core.
struct RunningEntry {
    int coreId = -1;
    std::shared_ptr<Process> process;
};

// Consistent view of the scheduler at one moment, for reports.
struct SchedulerSnapshot {
    uint64_t tick = 0;
    uint32_t totalCores = 0;
    uint32_t busyCores = 0;
    std::vector<RunningEntry> running;                // ordered by core id
    std::vector<std::shared_ptr<Process>> finished;   // in order of completion
    std::size_t readyCount = 0;
    std::size_t sleepingCount = 0;
    bool generating = false;
};

// CPU emulation and process scheduling.
//
// A clock thread advances a global tick counter. On every tick it:
//   1. moves processes whose SLEEP has ended back to the ready queue,
//   2. gives free cores the next processes from the ready queue (FIFO),
//   3. lets every busy core execute one tick in parallel (one core thread per CPU),
//   4. handles what each core did: finished, slept, or ran an instruction
//      (followed by delay-per-exec busy-wait ticks on the same core),
//   5. preempts processes whose round-robin quantum has expired,
//   6. creates a new process every batch-process-freq ticks while generation is on,
//   7. gives any cores freed during this tick to waiting processes.
class Scheduler {
public:
    explicit Scheduler(const Config& config, uint64_t seed = std::random_device{}());
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Starts the clock and core threads. Ticks advance in real time from then on.
    void start();
    // Stops and joins all threads. Safe to call more than once.
    void shutdown();
    bool isRunning() const { return running_.load(); }

    // Runs one tick on the calling thread. Only for use when start() has not been called.
    void tickOnce();

    // Creates a process with a random program and puts it in the ready queue.
    // Returns nullptr if a process with this name already exists.
    std::shared_ptr<Process> createProcess(const std::string& name);

    // Adds an already-built process to the ready queue. Returns false on a duplicate name.
    bool addProcess(const std::shared_ptr<Process>& process);

    std::shared_ptr<Process> findProcess(const std::string& name) const;

    // Batch generation for scheduler-start / scheduler-stop. Return false if already in that state.
    bool startGenerating();
    bool stopGenerating();
    bool isGenerating() const;

    uint32_t nextProcessId();

    SchedulerSnapshot snapshot() const;
    uint64_t currentTick() const;
    const Config& config() const { return config_; }

private:
    struct Core {
        std::shared_ptr<Process> process;
        uint64_t quantumUsed = 0;   // ticks used in the current time slice
        uint64_t delayLeft = 0;     // busy-wait ticks before the next instruction
        bool executed = false;      // set by the core thread: ran an instruction this tick
        StepOutcome outcome;        // set by the core thread when executed
    };

    struct SleepingEntry {
        uint64_t wakeTick;
        std::shared_ptr<Process> process;
    };

    void clockLoop();
    void coreLoop(int coreId);

    void tick();                      // one full tick; called by the clock thread or tickOnce()
    void runCores();                  // step 3, in parallel when threads exist, otherwise inline
    void runCore(Core& core, int coreId);

    void wakeSleepersLocked();
    void dispatchLocked();
    void handleOutcomesLocked();
    void generateBatchLocked();

    std::shared_ptr<Process> makeProcessLocked(const std::string& name);
    bool enqueueNewLocked(const std::shared_ptr<Process>& process);
    std::string nextBatchNameLocked();

    const Config config_;
    InstructionGenerator generator_;

    // Scheduler state, guarded by mutex_.
    mutable std::mutex mutex_;
    std::vector<Core> cores_;
    std::deque<std::shared_ptr<Process>> ready_;
    std::vector<SleepingEntry> sleeping_;
    std::vector<std::shared_ptr<Process>> finished_;
    std::unordered_map<std::string, std::shared_ptr<Process>> byName_;
    bool generating_ = false;
    uint64_t ticksSinceGenerationStart_ = 0;
    uint64_t batchCounter_ = 0;
    uint32_t nextId_ = 1;
    std::atomic<uint64_t> tick_{0};

    // Threads and the per-tick hand-off between the clock and the cores.
    std::atomic<bool> running_{false};
    std::thread clockThread_;
    std::mutex clockMutex_;
    std::condition_variable clockWake_;  // interrupts the wait between ticks on shutdown
    std::vector<std::thread> coreThreads_;
    std::mutex syncMutex_;
    std::condition_variable coresGo_;
    std::condition_variable coresDone_;
    uint64_t generation_ = 0;
    uint32_t pendingCores_ = 0;
    bool stopCores_ = false;
};

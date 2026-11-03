# Scheduler Implementation

How the emulated CPU runs processes: the clock, the cores, FCFS and Round Robin.

---

## Slide 1: Overview

- Real-time scheduler: starts at `initialize` and runs until `exit`
- Algorithm chosen in `config.txt`: **`fcfs`** or **`rr`**
- **1 clock thread** drives the global CPU tick counter
- **1 thread per CPU core** (1–128 cores), so instructions really execute in parallel
- The console thread only reads scheduler state through thread-safe calls
- Source: `src/scheduler/Scheduler.{h,cpp}`

---

## Slide 2: Data Structures

| Structure | Type | Holds |
|-----------|------|-------|
| Cores | `std::vector<Core>` | The process on each core, quantum used, busy-wait ticks left |
| Ready queue | `std::deque<shared_ptr<Process>>` | Processes waiting for a core (FIFO) |
| Sleeping list | `std::vector<{wakeTick, process}>` | Processes in SLEEP |
| Finished list | `std::vector<shared_ptr<Process>>` | Completed processes, in completion order |
| Process table | `std::unordered_map<string, shared_ptr<Process>>` | Name lookup for `screen -r`; keeps names unique |
| Tick counter | `std::atomic<uint64_t>` | The global CPU clock |

```cpp
struct Core {
    std::shared_ptr<Process> process;  // null = idle
    uint64_t quantumUsed;              // ticks used in the current time slice
    uint64_t delayLeft;                // busy-wait ticks before the next instruction
};
```

All of these are guarded by one scheduler mutex.

---

## Slide 3: One CPU Tick

The clock thread runs this sequence every tick:

```text
 ┌─ tick t ─────────────────────────────────────────────────────────────────┐
 │ 1. Wake sleepers      SLEEP finished?           ──► back to ready queue  │
 │ 2. Dispatch           free core + ready process ──► assign to core       │
 │ 3. Execute (parallel) every busy core runs one tick on its own thread    │
 │ 4. Handle outcomes    finished / slept / executed (+ delay-per-exec)     │
 │ 5. Preempt (RR)       quantum used up + someone waiting ──► back of queue│
 │ 6. Generate           every batch-process-freq ticks ──► new process     │
 │ 7. Refill             give cores freed this tick to waiting processes    │
 │ 8. tick++  and wait tick-ms milliseconds                                 │
 └──────────────────────────────────────────────────────────────────────────┘
```

- Step 7 means `screen -ls` never shows idle cores while processes are waiting.
- `tick-ms` (default 10 ms) is an optional config value that sets how fast the CPU runs in real time.

---

## Slide 4: Clock ↔ Core Synchronization

Each tick, the clock thread hands work to all core threads and waits for them to finish:

```text
 clock thread                         core threads (0 … N-1)
 ────────────                         ──────────────────────
 pending = N; generation++  ───────►  wake up (generation changed)
 notify_all(coresGo)                  run one tick on own core:
                                        busy-waiting? → delayLeft--
 wait(coresDone) ◄──────────────────    else process->executeNext(coreId)
                                      pending--; last one notifies coresDone
 handle all outcomes (single-threaded)
```

- Cores execute **in parallel**, but all queue changes happen on the clock thread after the
  cores report back. Cores never touch the queues, so there are no races on them.
- If no core is busy, the hand-off is skipped entirely, which keeps an idle CPU cheap.
- Built with `std::mutex` and `std::condition_variable` (C++17, no external libraries).

---

## Slide 5: FCFS vs Round Robin

**FCFS (First-Come, First-Served)**
- Ready queue is FIFO; a process keeps its core until it **finishes** or executes **SLEEP**.
- No preemption.

**RR (Round Robin)**
- Same FIFO queue, plus a time slice of **`quantum-cycles`** ticks.
- When a process has used its quantum:
  - another process is waiting → it goes to the **back of the ready queue**
  - nobody is waiting → it keeps the core with a fresh quantum (no pointless switching)
- Busy-wait ticks count toward the quantum; unfinished busy-wait is saved and resumed
  when the process gets a core again.

Example with 1 core, quantum 2, processes A and B (5 instructions each):

```text
tick:   0  1  2  3  4  5  6  7  8  9
core:   A  A  B  B  A  A  B  B  A  B
```

---

## Slide 6: `delay-per-exec` and SLEEP

**`delay-per-exec` = D (busy-waiting)**
- After each instruction, the process **stays on its core** for D ticks without executing.
- D = 0 → one instruction per tick. D = 2 → instructions on ticks 0, 3, 6, …
- The core counts as busy (it shows up in CPU utilization).

**`SLEEP(X)`**
- The process **gives up its core** and joins the sleeping list with
  `wakeTick = currentTick + 1 + X`.
- It returns to the ready queue after X ticks, and another process can use the core meanwhile.
- Applies under both FCFS and RR, since the spec says SLEEP "relinquishes the CPU".

---

## Slide 7: Batch Process Generation

- `scheduler-start` sets a flag; `scheduler-stop` clears it.
- While on, a process is created **every `batch-process-freq` ticks** at the end of the tick,
  so with a frequency of 1, one process is generated at the end of each CPU cycle.
- Names are human-readable and unique: **`p01, p02, …, p99, p100, …`**. A name already taken
  (e.g. by `screen -s p05`) is skipped.
- Each new process gets a random instruction count in [min-ins, max-ins] and joins the back of
  the ready queue.
- Processes created with `screen -s` are scheduled exactly like generated ones.

---

## Slide 8: CPU Utilization and Reporting

```text
CPU utilization = busy cores / total cores × 100   (rounded)
Cores used      = cores currently holding a process
Cores available = total cores − cores used
```

- `Scheduler::snapshot()` copies everything a report needs (tick, busy cores, running
  processes per core, finished list, queue sizes) in one locked step.
- The report is then formatted **outside** the lock, so the CPU never waits on the console.

---

## Slide 9: Real-Time Pacing and Shutdown

- The clock uses `std::chrono::steady_clock` and targets one tick every `tick-ms`.
- If the machine falls far behind, it resets its target instead of rushing through missed
  ticks all at once.
- The wait between ticks is a condition variable, so `exit` wakes it **immediately**, even
  with a slow `tick-ms`. Then core threads are stopped and all threads are joined.

---

## Slide 10: Verification

- **Unit tests** drive the scheduler one tick at a time (`tickOnce()`) and check exact states:
  FCFS order, RR rotation, multi-core dispatch, busy-wait timing, SLEEP wake-up,
  batch naming and frequency, utilization math.
- **Threaded test:** 4 cores at full speed with concurrent console reads. Every process
  finishes exactly its instruction count.
- **ThreadSanitizer:** no data races reported.
- **Stress runs, all clean with instant exit:**

| Scenario | Result |
|----------|--------|
| 128 cores, RR, quantum 1, new process every tick | 100% utilization, 128 running |
| 1 core, FCFS, 1 instruction per process | 100% utilization, processes finishing every tick |
| `delay-per-exec` = 2³² | Cores busy-wait, no hang on exit |
| `min-ins` = `max-ins` = 2³² | Runs with ~23 MB memory |
| 128 cores, `tick-ms 0` (max speed) | ~65 MB peak memory |

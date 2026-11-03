# Technical Report: CSOPESY Emulator

**Group members:** Jann Miro Quilantang, VL Kirsten Camille Saguin, Nathaniel Singh, Nathan Trinidad

Slide content for the technical report. Each `### Slide N` heading is one slide.

---

### Slide 1: System Overview

- Command-line OS emulator in **C++17**: CLI, `screen` multiplexer, multi-core CPU scheduler
- **Threads:** console (main), 1 clock thread, 1 thread per CPU core (1–128)
- Only the console prints; the CPU threads run in the background

```text
 user ─► Console ─► ConfigLoader (config.txt)
            │
            └────► Scheduler ─► clock thread + core threads ─► Process objects
```

---

## 1. Command Recognition

### Slide 2: From Text to Command

```text
input line ─► tokenize ─► look up command table ─► run handler
```

- **Tokenizer** splits on whitespace and groups `"quoted words"`. The command name is
  lower-cased; arguments keep their case.
- **Command table**: `std::map<name, {handler, requiresInitialize, usage}>`
- Main menu: `initialize`, `screen`, `scheduler-start`, `scheduler-stop`, `report-util`, `clear`, `help`, `exit`
- Process screen: `process-smi`, `exit`

### Slide 3: Validation and Feedback

| Input | Response |
|-------|----------|
| Any command before `initialize` (except `exit`/`help`) | `Please run "initialize" first.` |
| Unknown command | `'foo' is not recognized as a command.` |
| Bad `screen` arguments | `Usage: screen -s <name> \| screen -r <name> \| screen -ls` |
| Main-menu command inside a process screen | `not recognized inside a process screen` |

---

## 2. Console UI Implementation

### Slide 4: Main Menu and Process Screens

```text
   MAIN MENU                                   PROCESS SCREEN
 ┌──────────────┐   screen -s / screen -r    ┌──────────────────┐
 │ banner       │ ─────────────────────────► │ clear screen     │
 │ developers   │                            │ process details  │
 │ root:\>      │ ◄───────────────────────── │ root:\>          │
 └──────────────┘           exit             └──────────────────┘
```

- Header with ASCII banner, developer names and last-updated date
- Switching views clears the screen (`cls` on Windows, ANSI codes elsewhere)
- The process keeps running while you are attached or detached, like Linux `screen`

### Slide 5: Output Views

**`process-smi`**

```text
Process name: screen_01
ID: 1
Logs:
(11/03/2026 07:37:03PM) Core:0 "Hello world from screen_01!"

Current instruction line: 11
Lines of code: 32
```

**`screen -ls`** (`report-util` saves the same text to `csopesy-log.txt`)

```text
CPU utilization: 100%
Cores used: 4
Cores available: 0
--------------------------------------
Running processes:
p26       (11/03/2026 07:36:55PM)    Core: 1     27 / 48
Finished processes:
p05       (11/03/2026 07:36:54PM)    Finished    46 / 46
```

---

## 3. Command Interpreter Implementation

### Slide 6: Read-Eval Loop

```cpp
while (running_) {
    printPrompt();                 // root:\>
    getline(in_, line);
    dispatch(CommandParser::parse(line));
}
scheduler_->shutdown();            // stop and join all threads
```

| Command | What it does |
|---------|-------------|
| `initialize` | Load and validate `config.txt`, start the scheduler |
| `screen -s` / `-r` | Create or find a process, open its screen |
| `screen -ls` / `report-util` | Print / save a snapshot of the CPU and processes |
| `scheduler-start` / `-stop` | Turn batch process generation on / off |
| `exit` | Shut down all threads cleanly |

### Slide 7: `initialize` and `config.txt`

- Looks for `config.txt` in the working directory, then next to the `.exe`, then its parent folder
- Checks every value and reports clear errors; the user can fix the file and retry

| Parameter | Range |
|-----------|-------|
| `num-cpu` | 1 – 128 |
| `scheduler` | `fcfs` or `rr` |
| `quantum-cycles`, `batch-process-freq`, `min-ins`, `max-ins` | 1 – 2³² |
| `delay-per-exec` | 0 – 2³² |

```text
Error: num-cpu must be in the range [1, 128] (got 0).
Error: min-ins (5) cannot be greater than max-ins (2).
```

---

## 4. Process Representation

### Slide 8: The Process

| Part | Contents |
|------|----------|
| Identity | ID, name, creation time |
| Program | Instruction list (built from a random seed) |
| Position | Stack of frames: which instruction, inside which FOR loop |
| Memory | Variable table: `name → uint16` |
| Output | PRINT logs with timestamp and core |
| Progress | Instructions executed / total ("lines of code") |

```text
READY ──dispatch──► RUNNING ──last instruction──► FINISHED
  ▲                  │   │
  └── RR preempt ────┘   └── SLEEP(X) ──► SLEEPING ── X ticks ──► READY
```

### Slide 9: Instructions

| Instruction | Behavior |
|-------------|----------|
| `PRINT(msg)` | Log `"Hello world from <name>!"` (can append a variable) |
| `DECLARE(var, value)` | Create a uint16 variable |
| `ADD` / `SUBTRACT(var1, a, b)` | `var1 = a ± b`, **clamped to 0 – 65535** |
| `SLEEP(X)` | Give up the CPU for X ticks |
| `FOR([...], n)` | Repeat a block n times, nested up to 3 levels |

- Undeclared variables are automatically created as 0
- Each CPU tick executes **exactly one** instruction; FOR loops are tracked with a frame
  stack, so a process can pause and resume mid-loop

### Slide 10: Generation and Memory

- Instruction count is random in **[min-ins, max-ins]** and met exactly
- Mix: PRINT 30%, ADD 25%, SUBTRACT 20%, DECLARE 15%, SLEEP 10%, plus random FOR loops
- **Only running processes keep their instructions in memory.** The program is freed when a
  process leaves its core and rebuilt from its seed when it returns, resuming exactly
  where it stopped.
- Result: peak memory under stress dropped from **1.5 GB to 65 MB**

---

## 5. Scheduler Implementation

### Slide 11: One CPU Tick

```text
1. Wake sleepers     SLEEP done ─► ready queue
2. Dispatch          free cores take processes from the ready queue (FIFO)
3. Execute           all busy cores run one tick in parallel (own threads)
4. Handle results    finished / slept / ran (then busy-wait delay-per-exec ticks)
5. Preempt (RR)      quantum used up and someone waiting ─► back of the queue
6. Generate          every batch-process-freq ticks ─► new process p01, p02, …
7. Refill            cores freed this tick get the next waiting process
```

- The clock thread hands each tick to the core threads and waits for all of them before
  touching the queues, so cores run in parallel without race conditions

### Slide 12: FCFS vs Round Robin

| | FCFS | Round Robin |
|---|------|-------------|
| Queue | FIFO | FIFO |
| Gives up the core when | finished or SLEEP | finished, SLEEP, or `quantum-cycles` used |
| Preemption | No | Yes, only if another process is waiting |

1 core, quantum 2, processes A and B:

```text
tick:  0  1  2  3  4  5  6  7  8  9
core:  A  A  B  B  A  A  B  B  A  B
```

- **`delay-per-exec`:** the process busy-waits on its core between instructions
- **CPU utilization** = busy cores ÷ total cores

### Slide 13: Verification

- Unit tests check exact tick-by-tick behavior: FCFS order, RR rotation, multi-core dispatch,
  busy-wait timing, SLEEP wake-up and batch generation
- ThreadSanitizer: **no data races**
- Stress-tested at the limits, all running cleanly with instant exit:
  - 128 cores
  - quantum 1
  - a new process every tick
  - 2³² instructions
  - 2³² delay

# Process Representation

How a process, its instructions and its memory are modeled.

---

## Slide 1: The `Process` Class

Each process is one `Process` object (`src/process/Process.{h,cpp}`), shared between the
console and the CPU through a `std::shared_ptr`.

| Part | Field(s) | Purpose |
|------|----------|---------|
| Identity | `id_`, `name_`, `createdAt_` | Shown in `screen -ls` and `process-smi` |
| Program | `program_`, `loader_` | The instruction list, and how to rebuild it |
| Execution position | `stack_` (frames) | Where the process is, including inside FOR loops |
| Memory | `variables_` | `unordered_map<string, uint16_t>` variable table |
| Output | `logs_` | Every PRINT, with timestamp and core |
| Progress | `executed_`, `totalInstructions_` | "Current instruction line" / "Lines of code" |
| Scheduling | `state_`, `coreId_`, `pendingDelay_` | Read and updated by the scheduler |
| Completion | `finishedAt_` | Shown in the finished list |
| Thread safety | `mutex_` | Guards all of the above |

---

## Slide 2: Process States

```text
            created
               │
               ▼
          ┌─────────┐   dispatched to a core    ┌─────────┐
          │  READY  │ ────────────────────────► │ RUNNING │
          └─────────┘ ◄──────────────────────── └─────────┘
               ▲        RR quantum expired        │     │
               │                                  │     │ last instruction
               │        ┌──────────┐   SLEEP(X)   │     ▼
               └────────│ SLEEPING │ ◄────────────┘  ┌──────────┐
                X ticks └──────────┘                 │ FINISHED │
                passed                               └──────────┘
```

- `READY`: waiting in the ready queue
- `RUNNING`: holding a CPU core
- `SLEEPING`: gave up its core for X ticks
- `FINISHED`: permanent; the process can no longer be attached with `screen -r`

---

## Slide 3: The Instruction Set

| Instruction | Representation | Behavior |
|-------------|----------------|----------|
| `PRINT(msg)` | message, optional variable | Adds a log line; default `"Hello world from <name>!"` |
| `DECLARE(var, value)` | target, value | Sets a uint16 variable |
| `ADD(var1, a, b)` | target, two operands | `var1 = a + b`, clamped |
| `SUBTRACT(var1, a, b)` | target, two operands | `var1 = a - b`, clamped |
| `SLEEP(X)` | uint8 ticks | Gives up the CPU for X ticks |
| `FOR([...], n)` | body (vector of instructions), repeats | Runs the body n times; nests up to 3 levels |

An **operand** is either a variable name or a uint16 literal:

```cpp
struct Operand {
    bool isVariable;
    std::string name;    // when isVariable
    uint16_t literal;    // otherwise
};
```

---

## Slide 4: Variables and uint16 Clamping

- Variables live in a per-process table: `std::unordered_map<std::string, uint16_t>`.
- Reading an undeclared variable **declares it with value 0**, as the spec requires.
- Arithmetic is done in 64-bit, then clamped to the uint16 range:

```cpp
uint16_t clampToUint16(int64_t value) {
    if (value < 0)      return 0;
    if (value > 65535)  return 65535;
    return static_cast<uint16_t>(value);
}
```

| Operation | Result |
|-----------|--------|
| `DECLARE(x, 65000)` then `ADD(x, x, 1000)` | `x = 65535` (not overflowed) |
| `SUBTRACT(y, 5, 10)` | `y = 0` (no negative wrap-around) |
| `ADD(z, undeclared, 7)` | `undeclared = 0`, `z = 7` |

- Variables are kept until the process finishes, then released.

---

## Slide 5: Executing One Instruction at a Time

- The CPU calls `process->executeNext(coreId)` **once per tick**; it runs exactly one
  PRINT / DECLARE / ADD / SUBTRACT / SLEEP.
- FOR loops are handled with an explicit **stack of frames**, not recursion, so a process can
  stop and resume in the middle of a nested loop:

```cpp
struct Frame {
    const std::vector<Instruction>* block;  // the instruction list being run
    std::size_t pc;                         // next instruction in that list
    uint64_t remaining;                     // passes left through the block
};
```

- Reaching a FOR pushes a frame for its body; reaching the end of a block either repeats it
  or pops back to the enclosing block.
- Each call returns what happened, so the scheduler knows what to do next:

| `StepOutcome` | Scheduler reaction |
|---------------|--------------------|
| `Executed` | Keep the core (start `delay-per-exec` busy-wait) |
| `Slept` (X ticks) | Move to the sleeping list |
| `Finished` | Move to the finished list, free the core |

---

## Slide 6: Progress Counters

- **Lines of code** = number of instructions the process will execute, with FOR loops
  unrolled. Example: `FOR([ADD, FOR([ADD, FOR([ADD], 4)], 3)], 2)` followed by `PRINT`:
  `2 × (1 + 3 × (1 + 4)) + 1 = 33`.
- **Current instruction line** = how many of those have executed so far.
- This makes the `screen -ls` progress column (`27 / 48`) an exact measure of how far along
  a process is.

---

## Slide 7: Random Program Generation

`InstructionGenerator` builds each process's program:

- Instruction count picked uniformly in **[min-ins, max-ins]** and met **exactly**
- Instruction mix: PRINT 30%, ADD 25%, SUBTRACT 20%, DECLARE 15%, SLEEP 10%
- 10% chance of a FOR loop (2–5 repeats, body of 1–8 instructions), nested up to 3 levels
- Variables drawn from a small pool (`x, y, z, a, b, c`) so ADD and SUBTRACT reuse them
- Very large counts (up to 2³²) are reached by wrapping instructions in FOR loops, so memory
  stays bounded (under ~20,000 stored instructions per program)

A program is described by a **`ProgramSpec { instructionCount, seed }`**. The same spec always
produces the same program.

---

## Slide 8: Memory-Efficient Programs

Problem: with `batch-process-freq 1`, hundreds of processes are created per second, and most
of them sit in the ready queue.

Solution: **only processes holding a core keep their instructions in memory.**

```text
 created ──► READY (no program yet: just id, name, spec)
                │ first dispatch
                ▼
            RUNNING: program built from the seed
                │ preempted / sleeps
                ▼
            READY / SLEEPING: program freed, position + variables kept
                │ dispatched again
                ▼
            RUNNING: program rebuilt from the same seed, frames re-linked,
                     execution continues at the exact same instruction
```

- Log entries are stored compactly (timestamp as a number; the default message is not
  stored as text) and formatted only when displayed.
- Result in testing: peak memory under stress dropped from **1.5 GB to 65 MB**; a 90-second
  run with the default config stayed around **13 MB**.

---

## Slide 9: Thread Safety

- A process is touched by its core thread (executing) and the console thread
  (`process-smi`, `screen -ls`) at the same time.
- Every public method locks the process's own `std::mutex`.
- The console reads a **`ProcessSnapshot`** (a copy of id, name, state, core, counters,
  timestamps) instead of live fields, so every view is self-consistent.
- Verified with ThreadSanitizer: no data races.

#pragma once

#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <vector>

#include "process/Instruction.h"

// What a generated program will look like: how many instructions it executes
// (with FOR loops unrolled) and the seed that reproduces its exact contents.
struct ProgramSpec {
    uint64_t instructionCount = 0;
    uint64_t seed = 0;
};

// Builds random process programs. The executed instruction count is chosen
// uniformly from [minIns, maxIns] and is met exactly. Thread-safe.
//
// PRINT instructions are generated with an empty message, which a process
// prints as "Hello world from <process name>!".
class InstructionGenerator {
public:
    // Upper bound on Instruction objects kept in memory per program. Very large
    // instruction counts are reached by wrapping instructions in FOR loops.
    static constexpr uint64_t kMaxStoredInstructions = 20000;

    explicit InstructionGenerator(uint64_t seed = std::random_device{}());

    // Picks an instruction count in [minIns, maxIns] and a seed, without building the program.
    ProgramSpec pickSpec(uint64_t minIns, uint64_t maxIns);

    // Builds the program described by a spec. Same spec, same program.
    static std::vector<Instruction> build(const ProgramSpec& spec);

    // Convenience: pickSpec + build.
    std::vector<Instruction> generate(uint64_t minIns, uint64_t maxIns);

    // Builds a random program that executes exactly 'count' instructions.
    std::vector<Instruction> generateExact(uint64_t count);

    static std::string defaultPrintMessage(const std::string& processName);

private:
    std::mutex mutex_;
    std::mt19937_64 rng_;
};

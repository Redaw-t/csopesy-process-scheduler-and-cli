#include "process/InstructionGenerator.h"

#include <algorithm>

#include "process/Process.h"

namespace {

const std::vector<std::string> kVariablePool = {"x", "y", "z", "a", "b", "c"};

// Tuning for ordinary (non-compressing) loop generation.
constexpr uint64_t kMaxRepeats = 5;
constexpr uint64_t kMaxBodyBudget = 8;
constexpr double kForProbability = 0.10;

// Upper bounds for random literal values.
constexpr uint64_t kMaxDeclareValue = 500;
constexpr uint64_t kMaxOperandValue = 100;
constexpr uint64_t kMinSleepTicks = 1;
constexpr uint64_t kMaxSleepTicks = 10;

// Builds one program from a seeded random stream.
class ProgramBuilder {
public:
    explicit ProgramBuilder(uint64_t seed) : rng_(seed) {}

    std::vector<Instruction> build(uint64_t count) { return block(count, 0); }

private:
    std::vector<Instruction> block(uint64_t budget, int depth) {
        std::vector<Instruction> out;
        const bool canNest = depth < Process::kMaxLoopNesting;
        const uint64_t limit = InstructionGenerator::kMaxStoredInstructions;

        while (budget > 0) {
            const uint64_t room = stored_ < limit ? limit - stored_ : 0;

            // Too many instructions left to store individually: cover most of them with one loop.
            if (canNest && budget > room && budget >= 2) {
                const uint64_t bodyRoom = std::max<uint64_t>(1, room / 2);
                const uint64_t repeats = std::max<uint64_t>(2, (budget + bodyRoom - 1) / bodyRoom);
                const uint64_t bodyBudget = budget / repeats;
                stored_ += 1;
                std::vector<Instruction> body = block(bodyBudget, depth + 1);
                out.push_back(Instruction::forLoop(std::move(body), repeats));
                budget -= repeats * bodyBudget;
                continue;
            }

            std::uniform_real_distribution<double> chance(0.0, 1.0);
            if (canNest && budget >= 2 && chance(rng_) < kForProbability) {
                const uint64_t repeats = range(2, std::min(kMaxRepeats, budget));
                const uint64_t bodyBudget = range(1, std::min(kMaxBodyBudget, budget / repeats));
                stored_ += 1;
                std::vector<Instruction> body = block(bodyBudget, depth + 1);
                out.push_back(Instruction::forLoop(std::move(body), repeats));
                budget -= repeats * bodyBudget;
                continue;
            }

            out.push_back(simpleInstruction());
            stored_ += 1;
            budget -= 1;
        }
        return out;
    }

    Instruction simpleInstruction() {
        // Weights: PRINT 30, DECLARE 15, ADD 25, SUBTRACT 20, SLEEP 10.
        const uint64_t roll = range(1, 100);
        if (roll <= 30) {
            return Instruction::print("");
        }
        if (roll <= 45) {
            return Instruction::declare(variable(), static_cast<uint16_t>(range(0, kMaxDeclareValue)));
        }
        if (roll <= 70) {
            return Instruction::add(variable(), operand(), operand());
        }
        if (roll <= 90) {
            return Instruction::subtract(variable(), operand(), operand());
        }
        return Instruction::sleep(static_cast<uint8_t>(range(kMinSleepTicks, kMaxSleepTicks)));
    }

    uint64_t range(uint64_t low, uint64_t high) {
        if (high <= low) {
            return low;
        }
        std::uniform_int_distribution<uint64_t> dist(low, high);
        return dist(rng_);
    }

    const std::string& variable() { return kVariablePool[range(0, kVariablePool.size() - 1)]; }

    Operand operand() {
        if (range(0, 1) == 0) {
            return Operand::variable(variable());
        }
        return Operand::value(static_cast<uint16_t>(range(0, kMaxOperandValue)));
    }

    std::mt19937_64 rng_;
    uint64_t stored_ = 0;
};

}  // namespace

InstructionGenerator::InstructionGenerator(uint64_t seed) : rng_(seed) {}

std::string InstructionGenerator::defaultPrintMessage(const std::string& processName) {
    return "Hello world from " + processName + "!";
}

ProgramSpec InstructionGenerator::pickSpec(uint64_t minIns, uint64_t maxIns) {
    if (minIns > maxIns) {
        std::swap(minIns, maxIns);
    }
    minIns = std::max<uint64_t>(minIns, 1);
    maxIns = std::max<uint64_t>(maxIns, 1);

    std::lock_guard<std::mutex> lock(mutex_);
    ProgramSpec spec;
    spec.instructionCount = std::uniform_int_distribution<uint64_t>(minIns, maxIns)(rng_);
    spec.seed = rng_();
    return spec;
}

std::vector<Instruction> InstructionGenerator::build(const ProgramSpec& spec) {
    return ProgramBuilder(spec.seed).build(spec.instructionCount);
}

std::vector<Instruction> InstructionGenerator::generate(uint64_t minIns, uint64_t maxIns) {
    return build(pickSpec(minIns, maxIns));
}

std::vector<Instruction> InstructionGenerator::generateExact(uint64_t count) {
    ProgramSpec spec;
    spec.instructionCount = count;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        spec.seed = rng_();
    }
    return build(spec);
}

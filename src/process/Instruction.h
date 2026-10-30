#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class OpCode : uint8_t { Print, Declare, Add, Subtract, Sleep, For };

// An instruction argument: either a variable reference or a uint16 literal.
struct Operand {
    bool isVariable = false;
    std::string name;      // used when isVariable
    uint16_t literal = 0;  // used when !isVariable

    static Operand variable(std::string varName);
    static Operand value(uint16_t v);

    std::string toString() const;
};

// One process instruction. Fields are used depending on the opcode:
//   PRINT(msg [+ var])        -> message, printVariable (optional)
//   DECLARE(var, value)       -> target, value
//   ADD/SUBTRACT(var, a, b)   -> target, lhs, rhs
//   SLEEP(ticks)              -> sleepTicks
//   FOR([body], repeats)      -> body, repeats
struct Instruction {
    OpCode op = OpCode::Print;

    std::string message;
    std::string printVariable;

    std::string target;
    uint16_t value = 0;
    Operand lhs;
    Operand rhs;

    uint8_t sleepTicks = 0;

    std::vector<Instruction> body;
    uint64_t repeats = 0;

    static Instruction print(std::string msg, std::string variable = "");
    static Instruction declare(std::string var, uint16_t initialValue);
    static Instruction add(std::string var, Operand a, Operand b);
    static Instruction subtract(std::string var, Operand a, Operand b);
    static Instruction sleep(uint8_t ticks);
    static Instruction forLoop(std::vector<Instruction> loopBody, uint64_t loopRepeats);

    std::string toString() const;
};

// Number of instructions a program executes once every FOR loop is unrolled.
// FOR itself is a control structure and is not counted; only the instructions it runs are.
uint64_t executedInstructionCount(const std::vector<Instruction>& program);

// Number of Instruction objects stored in memory (FOR nodes and their bodies included).
uint64_t storedInstructionCount(const std::vector<Instruction>& program);

// Deepest FOR nesting level in a program (0 if it has no loops).
int maxLoopDepth(const std::vector<Instruction>& program);

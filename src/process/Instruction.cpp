#include "process/Instruction.h"

#include <algorithm>
#include <utility>

Operand Operand::variable(std::string varName) {
    Operand op;
    op.isVariable = true;
    op.name = std::move(varName);
    return op;
}

Operand Operand::value(uint16_t v) {
    Operand op;
    op.isVariable = false;
    op.literal = v;
    return op;
}

std::string Operand::toString() const { return isVariable ? name : std::to_string(literal); }

Instruction Instruction::print(std::string msg, std::string variable) {
    Instruction ins;
    ins.op = OpCode::Print;
    ins.message = std::move(msg);
    ins.printVariable = std::move(variable);
    return ins;
}

Instruction Instruction::declare(std::string var, uint16_t initialValue) {
    Instruction ins;
    ins.op = OpCode::Declare;
    ins.target = std::move(var);
    ins.value = initialValue;
    return ins;
}

Instruction Instruction::add(std::string var, Operand a, Operand b) {
    Instruction ins;
    ins.op = OpCode::Add;
    ins.target = std::move(var);
    ins.lhs = std::move(a);
    ins.rhs = std::move(b);
    return ins;
}

Instruction Instruction::subtract(std::string var, Operand a, Operand b) {
    Instruction ins;
    ins.op = OpCode::Subtract;
    ins.target = std::move(var);
    ins.lhs = std::move(a);
    ins.rhs = std::move(b);
    return ins;
}

Instruction Instruction::sleep(uint8_t ticks) {
    Instruction ins;
    ins.op = OpCode::Sleep;
    ins.sleepTicks = ticks;
    return ins;
}

Instruction Instruction::forLoop(std::vector<Instruction> loopBody, uint64_t loopRepeats) {
    Instruction ins;
    ins.op = OpCode::For;
    ins.body = std::move(loopBody);
    ins.repeats = loopRepeats;
    return ins;
}

std::string Instruction::toString() const {
    switch (op) {
        case OpCode::Print:
            if (printVariable.empty()) {
                return "PRINT(\"" + message + "\")";
            }
            return "PRINT(\"" + message + "\" + " + printVariable + ")";
        case OpCode::Declare:
            return "DECLARE(" + target + ", " + std::to_string(value) + ")";
        case OpCode::Add:
            return "ADD(" + target + ", " + lhs.toString() + ", " + rhs.toString() + ")";
        case OpCode::Subtract:
            return "SUBTRACT(" + target + ", " + lhs.toString() + ", " + rhs.toString() + ")";
        case OpCode::Sleep:
            return "SLEEP(" + std::to_string(sleepTicks) + ")";
        case OpCode::For: {
            std::string out = "FOR([";
            for (std::size_t i = 0; i < body.size(); ++i) {
                if (i > 0) {
                    out += ", ";
                }
                out += body[i].toString();
            }
            return out + "], " + std::to_string(repeats) + ")";
        }
    }
    return "UNKNOWN";
}

uint64_t executedInstructionCount(const std::vector<Instruction>& program) {
    uint64_t total = 0;
    for (const auto& ins : program) {
        if (ins.op == OpCode::For) {
            total += ins.repeats * executedInstructionCount(ins.body);
        } else {
            total += 1;
        }
    }
    return total;
}

uint64_t storedInstructionCount(const std::vector<Instruction>& program) {
    uint64_t total = 0;
    for (const auto& ins : program) {
        total += 1;
        if (ins.op == OpCode::For) {
            total += storedInstructionCount(ins.body);
        }
    }
    return total;
}

int maxLoopDepth(const std::vector<Instruction>& program) {
    int deepest = 0;
    for (const auto& ins : program) {
        if (ins.op == OpCode::For) {
            deepest = std::max(deepest, 1 + maxLoopDepth(ins.body));
        }
    }
    return deepest;
}

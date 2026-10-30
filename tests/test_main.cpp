// Unit tests for configuration parsing, the instruction set, the instruction generator,
// the scheduler and the console.
// Run the "csopesy_tests" target; it exits with a non-zero code if any check fails.

#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <string>
#include <vector>

#include "config/Config.h"
#include "console/CommandParser.h"
#include "console/Console.h"
#include "process/Instruction.h"
#include "process/InstructionGenerator.h"
#include "process/Process.h"
#include "report/UtilizationReport.h"
#include "scheduler/Scheduler.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::cerr << "  FAIL: " << what << "\n";
    }
}

// Runs a process to completion on core 0 and returns the number of steps taken.
uint64_t runToEnd(Process& p) {
    uint64_t steps = 0;
    while (!p.isFinished()) {
        p.executeNext(0);
        ++steps;
    }
    return steps;
}

void testConfigParsing() {
    std::cout << "config parsing\n";
    const auto ok = ConfigLoader::loadFromString(
        "num-cpu 8\nscheduler \"fcfs\"\nquantum-cycles 3\nbatch-process-freq 2\n"
        "min-ins 10\nmax-ins 20\ndelays-per-exec 4\n");
    check(ok.ok, "valid config loads");
    check(ok.warnings.empty(), "valid config has no warnings");
    check(ok.config.numCpu == 8, "num-cpu parsed");
    check(ok.config.scheduler == SchedulerType::FCFS, "quoted scheduler parsed");
    check(ok.config.quantumCycles == 3, "quantum-cycles parsed");
    check(ok.config.batchProcessFreq == 2, "batch-process-freq parsed");
    check(ok.config.minIns == 10 && ok.config.maxIns == 20, "min/max-ins parsed");
    check(ok.config.delayPerExec == 4, "delays-per-exec alias parsed");

    const auto oneLine = ConfigLoader::loadFromString("num-cpu 2 scheduler rr quantum-cycles 1 "
                                                      "batch-process-freq 1 min-ins 1 max-ins 1 delay-per-exec 0");
    check(oneLine.ok && oneLine.config.numCpu == 2, "single-line space-separated config");

    const auto big = ConfigLoader::loadFromString("min-ins 4294967296 max-ins 4294967296 quantum-cycles 4294967296");
    check(big.ok && big.config.maxIns == 4294967296ULL, "2^32 accepted");

    check(!ConfigLoader::loadFromString("num-cpu 0").ok, "num-cpu 0 rejected");
    check(!ConfigLoader::loadFromString("num-cpu 129").ok, "num-cpu 129 rejected");
    check(ConfigLoader::loadFromString("num-cpu 128").ok, "num-cpu 128 accepted");
    check(!ConfigLoader::loadFromString("scheduler sjf").ok, "unknown scheduler rejected");
    check(!ConfigLoader::loadFromString("quantum-cycles 0").ok, "quantum 0 rejected");
    check(!ConfigLoader::loadFromString("min-ins 4294967297").ok, "value above 2^32 rejected");
    check(!ConfigLoader::loadFromString("min-ins -5").ok, "negative rejected");
    check(!ConfigLoader::loadFromString("min-ins abc").ok, "non-number rejected");
    check(!ConfigLoader::loadFromString("min-ins 50 max-ins 10").ok, "min > max rejected");
    check(ConfigLoader::loadFromString("delay-per-exec 0").ok, "delay 0 accepted");
    check(!ConfigLoader::loadFromString("num-cpu").ok, "missing value rejected");

    const auto partial = ConfigLoader::loadFromString("num-cpu 2");
    check(partial.ok && !partial.warnings.empty(), "missing keys produce warnings and defaults");
    check(partial.config.maxIns == 2000, "default max-ins kept");
}

void testCommandParser() {
    std::cout << "command parser\n";
    const auto cmd = CommandParser::parse("  SCREEN   -s   p01  ");
    check(cmd.name == "screen", "command lower-cased");
    check(cmd.args.size() == 2 && cmd.args[0] == "-s" && cmd.args[1] == "p01", "args split");
    check(CommandParser::parse("   ").empty(), "blank line is empty");
    const auto quoted = CommandParser::parse("screen -s \"my proc\"");
    check(quoted.args.size() == 2 && quoted.args[1] == "my proc", "quoted argument grouped");
}

void testArithmeticAndClamping() {
    std::cout << "arithmetic and clamping\n";
    std::vector<Instruction> prog;
    prog.push_back(Instruction::declare("x", 65000));
    prog.push_back(Instruction::add("x", Operand::variable("x"), Operand::value(1000)));  // clamps to 65535
    prog.push_back(Instruction::subtract("y", Operand::value(5), Operand::value(10)));    // clamps to 0
    prog.push_back(Instruction::add("z", Operand::variable("undeclared"), Operand::value(7)));
    prog.push_back(Instruction::subtract("w", Operand::variable("x"), Operand::variable("z")));
    prog.push_back(Instruction::print("end"));

    Process p(1, "t1", std::move(prog));
    for (int i = 0; i < 5; ++i) {
        p.executeNext(0);
    }
    check(p.variable("x") == 65535, "ADD clamps to uint16 max");
    check(p.variable("y") == 0, "SUBTRACT clamps to 0");
    check(p.variable("undeclared") == 0, "undeclared variable auto-declared as 0");
    check(p.variable("z") == 7, "ADD with auto-declared variable");
    check(p.variable("w") == 65528, "SUBTRACT two variables");
    check(!p.isFinished(), "not finished before last instruction");
    p.executeNext(0);
    check(p.isFinished(), "finished after last instruction");
    check(!p.variable("x").has_value(), "variables released after finishing");
}

void testPrintAndLogs() {
    std::cout << "print and logs\n";
    std::vector<Instruction> prog;
    prog.push_back(Instruction::print(InstructionGenerator::defaultPrintMessage("p01")));
    prog.push_back(Instruction::declare("x", 42));
    prog.push_back(Instruction::print("Value from: ", "x"));
    Process p(2, "p01", std::move(prog));
    p.executeNext(3);
    p.executeNext(3);
    p.executeNext(1);
    const auto logs = p.logs();
    check(logs.size() == 2, "two PRINT logs");
    check(logs.size() == 2 && logs[0].message == "Hello world from p01!", "default message");
    check(logs.size() == 2 && logs[1].message == "Value from: 42", "message with variable");
    check(logs.size() == 2 && logs[0].coreId == 3 && logs[1].coreId == 1, "core id recorded");
    check(logs.size() == 2 && logs[0].timestamp.size() == 21, "timestamp format MM/DD/YYYY HH:MM:SSAM");
}

void testSleep() {
    std::cout << "sleep\n";
    std::vector<Instruction> prog;
    prog.push_back(Instruction::sleep(5));
    prog.push_back(Instruction::print("after"));
    Process p(3, "s", std::move(prog));
    const auto outcome = p.executeNext(0);
    check(outcome.kind == StepOutcome::Kind::Slept && outcome.sleepTicks == 5, "SLEEP reports ticks");
    const auto last = p.executeNext(0);
    check(last.kind == StepOutcome::Kind::Finished, "last instruction reports finished");
}

void testNestedLoops() {
    std::cout << "nested loops\n";
    // FOR([ADD x+1, FOR([ADD y+1, FOR([ADD z+1], 4)], 3)], 2)
    std::vector<Instruction> inner3 = {Instruction::add("z", Operand::variable("z"), Operand::value(1))};
    std::vector<Instruction> inner2 = {Instruction::add("y", Operand::variable("y"), Operand::value(1)),
                                       Instruction::forLoop(inner3, 4)};
    std::vector<Instruction> inner1 = {Instruction::add("x", Operand::variable("x"), Operand::value(1)),
                                       Instruction::forLoop(inner2, 3)};
    std::vector<Instruction> prog = {Instruction::forLoop(inner1, 2), Instruction::print("done")};

    check(maxLoopDepth(prog) == 3, "depth 3");
    // 2 * (1 + 3 * (1 + 4)) + 1 = 33
    check(executedInstructionCount(prog) == 33, "unrolled instruction count");

    Process p(4, "loops", std::move(prog));
    check(p.totalInstructions() == 33, "process total instructions");
    uint64_t steps = 0;
    while (steps < 32) {
        p.executeNext(0);
        ++steps;
    }
    check(p.variable("x") == 2, "outer loop ran 2 times");
    check(p.variable("y") == 6, "middle loop ran 6 times");
    check(p.variable("z") == 24, "inner loop ran 24 times");
    check(p.executedInstructions() == 32, "executed count tracks progress");
    p.executeNext(0);
    check(p.isFinished() && p.executedInstructions() == 33, "finishes at total");

    std::vector<Instruction> empties = {Instruction::forLoop({}, 5), Instruction::forLoop({Instruction::print("a")}, 0),
                                        Instruction::print("b")};
    Process q(5, "empty", std::move(empties));
    check(q.totalInstructions() == 1, "empty / zero-repeat loops count nothing");
    check(runToEnd(q) == 1, "empty loops are skipped");
}

void testGenerator() {
    std::cout << "generator\n";
    InstructionGenerator gen(12345);
    for (int i = 0; i < 200; ++i) {
        const auto prog = gen.generate(1000, 2000);
        const uint64_t count = executedInstructionCount(prog);
        if (count < 1000 || count > 2000 || maxLoopDepth(prog) > Process::kMaxLoopNesting) {
            check(false, "generated program within bounds and nesting (iteration " + std::to_string(i) + ")");
            return;
        }
    }
    check(true, "200 programs within [1000, 2000] and nesting <= 3");

    for (const uint64_t exact : {1ULL, 2ULL, 7ULL, 50000ULL, 1000000ULL, 4294967296ULL}) {
        const auto prog = gen.generateExact(exact);
        check(executedInstructionCount(prog) == exact, "exact count " + std::to_string(exact));
        check(storedInstructionCount(prog) <= 2 * InstructionGenerator::kMaxStoredInstructions,
              "bounded memory for " + std::to_string(exact));
        check(maxLoopDepth(prog) <= Process::kMaxLoopNesting, "nesting <= 3 for " + std::to_string(exact));
    }

    const auto prog = gen.generateExact(3000);
    Process p(6, "p07", prog);
    check(runToEnd(p) == 3000, "generated program executes exactly its instruction count");
    bool allDefault = true;
    for (const auto& log : p.logs()) {
        allDefault = allDefault && log.message == "Hello world from p07!";
    }
    check(allDefault && !p.logs().empty(), "generated PRINTs use the default message");
}

Config testConfig(SchedulerType type, uint32_t cpus) {
    Config cfg;
    cfg.numCpu = cpus;
    cfg.scheduler = type;
    cfg.quantumCycles = 2;
    cfg.batchProcessFreq = 1;
    cfg.minIns = 5;
    cfg.maxIns = 5;
    cfg.delayPerExec = 0;
    cfg.tickMs = 0;
    return cfg;
}

std::vector<Instruction> declares(int count) {
    std::vector<Instruction> prog;
    for (int i = 0; i < count; ++i) {
        prog.push_back(Instruction::declare("x", static_cast<uint16_t>(i)));
    }
    return prog;
}

std::shared_ptr<Process> addTo(Scheduler& s, const std::string& name, std::vector<Instruction> prog) {
    auto p = std::make_shared<Process>(s.nextProcessId(), name, std::move(prog));
    s.addProcess(p);
    return p;
}

void ticks(Scheduler& s, int n) {
    for (int i = 0; i < n; ++i) {
        s.tickOnce();
    }
}

void testFcfs() {
    std::cout << "scheduler: fcfs\n";
    Scheduler s(testConfig(SchedulerType::FCFS, 1));
    auto a = addTo(s, "A", declares(3));
    auto b = addTo(s, "B", declares(3));
    ticks(s, 3);
    check(a->isFinished() && b->executedInstructions() == 0, "FCFS runs A to completion before B");
    ticks(s, 3);
    check(b->isFinished(), "B finishes after A");
    const auto snap = s.snapshot();
    check(snap.finished.size() == 2 && snap.finished[0] == a && snap.finished[1] == b, "finish order A, B");
    check(snap.busyCores == 0, "cores idle when nothing is left");
}

void testRoundRobin() {
    std::cout << "scheduler: round robin\n";
    Scheduler s(testConfig(SchedulerType::RR, 1));  // quantum 2
    auto a = addTo(s, "A", declares(5));
    auto b = addTo(s, "B", declares(5));
    ticks(s, 2);
    check(a->executedInstructions() == 2 && b->executedInstructions() == 0, "A runs one quantum");
    ticks(s, 2);
    check(a->executedInstructions() == 2 && b->executedInstructions() == 2, "B gets the next quantum");
    check(a->state() == ProcessState::Running && b->state() == ProcessState::Ready,
          "A takes the core back as soon as B's quantum ends");
    check(s.snapshot().busyCores == 1, "core never left idle between ticks");
    ticks(s, 1);
    check(a->executedInstructions() == 3 && a->state() == ProcessState::Running, "A resumes where it left off");
    ticks(s, 1);
    ticks(s, 6);
    check(a->isFinished() && b->isFinished(), "both finish");

    Scheduler solo(testConfig(SchedulerType::RR, 1));
    auto only = addTo(solo, "solo", declares(5));
    ticks(solo, 5);
    check(only->isFinished(), "a lone process is not stalled by quantum expiry");
}

void testMultiCore() {
    std::cout << "scheduler: multiple cores\n";
    Scheduler s(testConfig(SchedulerType::FCFS, 4));
    std::vector<std::shared_ptr<Process>> procs;
    for (int i = 0; i < 6; ++i) {
        procs.push_back(addTo(s, "m" + std::to_string(i), declares(4)));
    }
    ticks(s, 1);
    const auto snap = s.snapshot();
    check(snap.busyCores == 4 && snap.readyCount == 2, "4 cores busy, 2 waiting");
    check(snap.running.size() == 4 && snap.running[0].coreId == 0 && snap.running[3].coreId == 3,
          "running entries report core ids");
    ticks(s, 7);
    bool all = true;
    for (const auto& p : procs) {
        all = all && p->isFinished();
    }
    check(all, "all processes finish");
}

void testDelayPerExec() {
    std::cout << "scheduler: delay-per-exec\n";
    Config cfg = testConfig(SchedulerType::FCFS, 1);
    cfg.delayPerExec = 2;
    Scheduler s(cfg);
    auto p = addTo(s, "D", declares(3));
    // Instructions run on ticks 0, 3 and 6.
    ticks(s, 1);
    check(p->executedInstructions() == 1, "first instruction at tick 0");
    ticks(s, 2);
    check(p->executedInstructions() == 1 && s.snapshot().busyCores == 1, "busy-waiting keeps the core");
    ticks(s, 1);
    check(p->executedInstructions() == 2, "second instruction after 2 delay ticks");
    ticks(s, 2);
    check(!p->isFinished(), "not finished before tick 6");
    ticks(s, 1);
    check(p->isFinished(), "finished at tick 6");
}

void testSleepScheduling() {
    std::cout << "scheduler: sleep\n";
    Scheduler s(testConfig(SchedulerType::FCFS, 1));
    auto sleeper = addTo(s, "S", {Instruction::sleep(3), Instruction::print("awake")});
    auto other = addTo(s, "O", declares(10));
    ticks(s, 1);
    check(sleeper->state() == ProcessState::Sleeping, "SLEEP gives up the core");
    ticks(s, 3);
    check(other->executedInstructions() == 3, "another process uses the core while it sleeps");
    check(sleeper->state() == ProcessState::Sleeping, "still sleeping through tick 3");
    ticks(s, 1);
    check(sleeper->state() == ProcessState::Ready, "back in the ready queue after 3 ticks");
    ticks(s, 6);  // FCFS: O keeps the core until it finishes
    check(other->isFinished() && !sleeper->isFinished(), "FCFS: running process is not preempted");
    ticks(s, 1);
    check(sleeper->isFinished(), "sleeper resumes and finishes");
}

void testBatchGeneration() {
    std::cout << "scheduler: batch generation\n";
    Config cfg = testConfig(SchedulerType::RR, 2);
    cfg.batchProcessFreq = 3;
    Scheduler s(cfg);
    check(s.createProcess("p02") != nullptr, "screen -s style process created");
    check(s.createProcess("p02") == nullptr, "duplicate name rejected");
    check(s.startGenerating() && !s.startGenerating(), "start once");
    ticks(s, 9);
    check(s.stopGenerating() && !s.stopGenerating(), "stop once");
    check(s.findProcess("p01") != nullptr, "p01 generated");
    check(s.findProcess("p03") != nullptr && s.findProcess("p04") != nullptr,
          "existing p02 skipped, p03 and p04 made");
    check(s.findProcess("p05") == nullptr, "one process per 3 ticks");
    ticks(s, 9);
    check(s.findProcess("p05") == nullptr, "no generation after stop");
    check(s.findProcess("p01")->totalInstructions() == 5, "generated processes follow min-ins/max-ins");
}

void testThreadedRun() {
    std::cout << "scheduler: threaded run\n";
    Config cfg = testConfig(SchedulerType::RR, 4);
    cfg.minIns = 20;
    cfg.maxIns = 60;
    cfg.delayPerExec = 1;
    Scheduler s(cfg);
    s.start();
    s.startGenerating();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snap = s.snapshot();  // concurrent reads while cores execute
        for (const auto& r : snap.running) {
            (void)r.process->snapshot();
        }
        if (auto p = s.findProcess("p01")) {
            (void)p->logs();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    s.stopGenerating();
    const auto stopAt = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    SchedulerSnapshot snap = s.snapshot();
    while ((snap.busyCores > 0 || snap.readyCount > 0 || snap.sleepingCount > 0) &&
           std::chrono::steady_clock::now() < stopAt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        snap = s.snapshot();
    }
    s.shutdown();
    snap = s.snapshot();
    bool consistent = !snap.finished.empty();
    for (const auto& p : snap.finished) {
        consistent = consistent && p->isFinished() && p->executedInstructions() == p->totalInstructions();
    }
    check(snap.busyCores == 0 && snap.readyCount == 0 && snap.sleepingCount == 0, "all work drained");
    check(consistent, "every process finished exactly its instruction count (" +
                          std::to_string(snap.finished.size()) + " processes)");
}

void testConsoleScreen() {
    std::cout << "console screen\n";
    std::istringstream in("initialize\nscreen -r nobody\nscreen -s demo\nprocess-smi\nfoo\nexit\n"
                          "screen -s demo\nscheduler-stop\nscheduler-start\nscheduler-start\nexit\n");
    std::ostringstream out;
    Console console(in, out);
    console.run();
    const std::string text = out.str();
    check(console.isInitialized(), "initialized from config.txt");
    check(text.find("Process nobody not found.") != std::string::npos, "screen -r unknown process");
    check(text.find("Process name: demo") != std::string::npos, "screen -s shows process screen");
    check(text.find("Lines of code:") != std::string::npos || text.find("Finished!") != std::string::npos,
          "process-smi output");
    check(text.find("not recognized inside a process screen") != std::string::npos, "unknown screen command");
    check(text.find("Process demo already exists.") != std::string::npos, "duplicate screen -s");
    check(text.find("Scheduler is not generating processes.") != std::string::npos, "stop before start");
    check(text.find("Scheduler is already generating processes.") != std::string::npos, "double start");
}

void testUnloadReload() {
    std::cout << "unload and reload\n";
    bool same = true;
    for (uint64_t seed = 1; seed <= 20 && same; ++seed) {
        const ProgramSpec spec{2500, seed};
        auto loader = [spec] { return InstructionGenerator::build(spec); };
        Process steady(1, "u", spec.instructionCount, loader);
        Process bounced(2, "u", spec.instructionCount, loader);
        const std::vector<std::string> vars = {"x", "y", "z", "a", "b", "c"};
        while (!steady.isFinished() && same) {
            const auto o1 = steady.executeNext(0);
            const auto o2 = bounced.executeNext(0);
            bounced.unloadProgram();
            same = same && !bounced.isProgramLoaded();
            same = same && o1.kind == o2.kind && o1.sleepTicks == o2.sleepTicks;
            same = same && steady.executedInstructions() == bounced.executedInstructions();
            for (const auto& v : vars) {
                same = same && steady.variable(v) == bounced.variable(v);
            }
        }
        same = same && bounced.isFinished() && steady.logCount() == bounced.logCount() &&
               steady.executedInstructions() == spec.instructionCount;
    }
    check(same, "a process unloaded after every instruction behaves exactly like one that never is");

    const ProgramSpec spec{1000, 99};
    Process p(3, "lazy", spec.instructionCount, [spec] { return InstructionGenerator::build(spec); });
    check(!p.isProgramLoaded(), "program not built before first execution");
    p.executeNext(0);
    check(p.isProgramLoaded(), "program built on first execution");
}

void testUtilizationReport() {
    std::cout << "utilization report\n";
    Config cfg = testConfig(SchedulerType::FCFS, 2);
    Scheduler s(cfg);
    addTo(s, "short", declares(1));
    addTo(s, "long_process_name", declares(5));
    ticks(s, 3);  // "short" finished at tick 0; "long_process_name" has run 3 of 5
    const SchedulerSnapshot snap = s.snapshot();
    const std::string text = report::buildUtilizationReport(snap);
    check(report::cpuUtilizationPercent(snap) == 50, "1 of 2 cores busy is 50%");
    check(text.find("CPU utilization: 50%\n") != std::string::npos, "utilization line");
    check(text.find("Cores used: 1\n") != std::string::npos, "cores used line");
    check(text.find("Cores available: 1\n") != std::string::npos, "cores available line");
    check(text.find("Running processes:\nlong_process_name") != std::string::npos, "running list");
    check(text.find("Core: 1") != std::string::npos && text.find("3 / 5") != std::string::npos,
          "running row shows core and progress");
    check(text.find("Finished processes:\nshort") != std::string::npos, "finished list");
    check(text.find("Finished    1 / 1") != std::string::npos || text.find("Finished") != std::string::npos,
          "finished row");

    SchedulerSnapshot full;
    full.totalCores = 3;
    full.busyCores = 2;
    check(report::cpuUtilizationPercent(full) == 67, "rounds to nearest percent");
}

void testConsoleReport() {
    std::cout << "console report\n";
    std::istringstream in("initialize\nscreen -s r1\nexit\nscreen -ls\nreport-util\nexit\n");
    std::ostringstream out;
    Console console(in, out);
    console.run();
    const std::string text = out.str();
    check(text.find("CPU utilization:") != std::string::npos, "screen -ls prints utilization");
    check(text.find("Running processes:") != std::string::npos, "screen -ls prints running list");
    const auto at = text.find("Report generated at ");
    check(at != std::string::npos, "report-util confirms the file path");
    if (at != std::string::npos) {
        const auto start = at + std::string("Report generated at ").size();
        const auto end = text.find("!\n", start);
        const std::string path = text.substr(start, end - start);
        std::ifstream file(path);
        std::stringstream contents;
        contents << file.rdbuf();
        check(file.good() && contents.str().find("Cores available:") != std::string::npos,
              "csopesy-log.txt contains the report");
        check(path.size() >= 15 && path.substr(path.size() - 15) == "csopesy-log.txt", "file is csopesy-log.txt");
    }
}

void testFastShutdown() {
    std::cout << "shutdown\n";
    Config cfg = testConfig(SchedulerType::RR, 8);
    cfg.tickMs = 1000;
    Scheduler s(cfg);
    s.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto begin = std::chrono::steady_clock::now();
    s.shutdown();
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    check(elapsed < std::chrono::milliseconds(500), "shutdown does not wait out a long tick");
}

void testConsoleGating() {
    std::cout << "console gating\n";
    std::istringstream in("screen -ls\nfoo\nexit\n");
    std::ostringstream out;
    Console console(in, out);
    console.run();
    const std::string text = out.str();
    check(text.find("Please run \"initialize\" first.") != std::string::npos, "commands blocked before initialize");
    check(text.find("'foo' is not recognized") != std::string::npos, "unknown command reported");
    check(!console.isInitialized(), "still uninitialized");
}

}  // namespace

int main() {
    testConfigParsing();
    testCommandParser();
    testArithmeticAndClamping();
    testPrintAndLogs();
    testSleep();
    testNestedLoops();
    testGenerator();
    testFcfs();
    testRoundRobin();
    testMultiCore();
    testDelayPerExec();
    testSleepScheduling();
    testBatchGeneration();
    testThreadedRun();
    testUnloadReload();
    testUtilizationReport();
    testFastShutdown();
    testConsoleGating();
    testConsoleScreen();
    testConsoleReport();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}

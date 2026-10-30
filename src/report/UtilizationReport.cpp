#include "report/UtilizationReport.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

namespace report {

namespace {

constexpr const char* kDivider = "--------------------------------------";

struct Row {
    std::string name;
    std::string timestamp;
    std::string where;  // "Core: N" or "Finished"
    std::string progress;
};

void printRows(std::ostringstream& out, const std::vector<Row>& rows, std::size_t nameWidth,
               std::size_t whereWidth) {
    for (const auto& row : rows) {
        out << std::left << std::setw(static_cast<int>(nameWidth)) << row.name << "(" << row.timestamp << ")    "
            << std::setw(static_cast<int>(whereWidth)) << row.where << row.progress << "\n";
    }
}

}  // namespace

int cpuUtilizationPercent(const SchedulerSnapshot& snapshot) {
    if (snapshot.totalCores == 0) {
        return 0;
    }
    return static_cast<int>((snapshot.busyCores * 100 + snapshot.totalCores / 2) / snapshot.totalCores);
}

std::string buildUtilizationReport(const SchedulerSnapshot& snapshot) {
    std::vector<Row> running;
    running.reserve(snapshot.running.size());
    for (const auto& entry : snapshot.running) {
        const ProcessSnapshot p = entry.process->snapshot();
        running.push_back(Row{p.name, p.createdAt, "Core: " + std::to_string(entry.coreId),
                              std::to_string(p.executed) + " / " + std::to_string(p.total)});
    }

    std::vector<Row> finished;
    finished.reserve(snapshot.finished.size());
    for (const auto& process : snapshot.finished) {
        const ProcessSnapshot p = process->snapshot();
        finished.push_back(Row{p.name, p.finishedAt, "Finished",
                               std::to_string(p.executed) + " / " + std::to_string(p.total)});
    }

    std::size_t nameWidth = 10;
    std::size_t whereWidth = 12;
    for (const auto* rows : {&running, &finished}) {
        for (const auto& row : *rows) {
            nameWidth = std::max(nameWidth, row.name.size() + 4);
            whereWidth = std::max(whereWidth, row.where.size() + 4);
        }
    }

    std::ostringstream out;
    out << "CPU utilization: " << cpuUtilizationPercent(snapshot) << "%\n";
    out << "Cores used: " << snapshot.busyCores << "\n";
    out << "Cores available: " << (snapshot.totalCores - snapshot.busyCores) << "\n";
    out << "\n" << kDivider << "\n";
    out << "Running processes:\n";
    printRows(out, running, nameWidth, whereWidth);
    out << "\nFinished processes:\n";
    printRows(out, finished, nameWidth, whereWidth);
    out << kDivider << "\n";
    return out.str();
}

}  // namespace report

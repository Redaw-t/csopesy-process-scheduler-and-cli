#pragma once

#include <string>

#include "scheduler/Scheduler.h"

namespace report {

// CPU utilization summary shared by "screen -ls" (printed) and "report-util" (saved):
// utilization, cores used/available, then running and finished processes.
std::string buildUtilizationReport(const SchedulerSnapshot& snapshot);

// Percentage of cores currently running a process, rounded to the nearest whole number.
int cpuUtilizationPercent(const SchedulerSnapshot& snapshot);

}  // namespace report

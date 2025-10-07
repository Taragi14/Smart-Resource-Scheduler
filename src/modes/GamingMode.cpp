#include "GamingMode.h"
#include "Logger.h"
#include <sched.h>

void GamingMode::apply(const SchedulerConfig& config, ProcessManager& processManager) {
    Logger::log("Applying Gaming mode with high priority: " + std::to_string(config.priority_high));
    auto processes = processManager.getRunningProcesses();
    for (const auto& proc : processes) {
        processManager.setPriority(proc.pid, config.priority_high);
        processManager.setCPUAffinity(proc.pid, config.cpu_affinity_cores);
        processManager.assignToCgroup(proc.pid, config);
        processManager.migrateToNUMANode(proc.pid, 0); // Prefer NUMA node 0 for low latency
        optimizeForLowLatency(proc.pid);
        Logger::log("Optimized PID " + std::to_string(proc.pid) + " for Gaming mode");
    }
}

void GamingMode::optimizeForLowLatency(int pid) {
    struct sched_param param;
    param.sched_priority = 99; // Real-time priority
    if (sched_setscheduler(pid, SCHED_FIFO, &param) == 0) {
        Logger::log("Set real-time SCHED_FIFO for PID " + std::to_string(pid));
    }
}
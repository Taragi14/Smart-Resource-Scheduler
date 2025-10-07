#include "PowerSavingMode.h"
#include "Logger.h"

void PowerSavingMode::apply(const SchedulerConfig& config, ProcessManager& processManager) {
    Logger::log("Applying Power-Saving mode with low priority: " + std::to_string(config.priority_low));
    auto processes = processManager.getRunningProcesses();
    for (const auto& proc : processes) {
        processManager.setPriority(proc.pid, config.priority_low);
        processManager.assignToCgroup(proc.pid, config);
        if (proc.cpu_usage > 10.0) {
            processManager.pauseProcess(proc.pid);
        }
    }
}
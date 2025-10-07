#include "ProductivityMode.h"
#include "Logger.h"

void ProductivityMode::apply(const SchedulerConfig& config, ProcessManager& processManager) {
    Logger::log("Applying Productivity mode with balanced priority: " + std::to_string(config.priority_high));
    auto processes = processManager.getRunningProcesses();
    for (const auto& proc : processes) {
        if (proc.cpu_usage < 30.0) {
            processManager.setPriority(proc.pid, config.priority_low);
        } else {
            processManager.setPriority(proc.pid, config.priority_high);
        }
        processManager.assignToCgroup(proc.pid, config);
    }
}
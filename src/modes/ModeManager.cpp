#include "ModeManager.h"
#include "Logger.h"

ModeManager::ModeManager() {
    setMode("Productivity");
}

void ModeManager::setMode(const std::string& mode) {
    config = configManager.loadConfig("config/" + mode + "_profile.json");
    Logger::log("Loaded config for mode: " + mode);
}

void ModeManager::applyScheduling() {
    adjustPrioritiesDynamically();
    processManager.adjustPriorities(config);
    memoryManager.monitorMemory(config);
    systemMonitor.logSystemStats();
}

void ModeManager::adjustPrioritiesDynamically() {
    auto processes = processManager.getRunningProcesses();
    for (auto& proc : processes) {
        if (proc.cpu_usage > 75.0) {
            proc.cpu_usage += 5; // Boost priority for high CPU usage
        } else if (proc.memory_usage > config.memory_threshold_mb * 1024) {
            proc.cpu_usage -= 5; // Lower priority for high memory usage
        }
        Logger::log("Dynamic priority adjustment for PID " + std::to_string(proc.pid));
    }
}

SchedulerConfig ModeManager::getConfig() const {
    return config;
}
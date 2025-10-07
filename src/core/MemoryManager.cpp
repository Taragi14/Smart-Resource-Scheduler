#include "MemoryManager.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <numeric>

double MemoryManager::getSystemMemoryUsage() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    long total = 0, free = 0;
    while (std::getline(meminfo, line)) {
        std::istringstream iss(line);
        std::string key;
        long value;
        iss >> key >> value;
        if (key == "MemTotal:") total = value;
        if (key == "MemFree:") free = value;
    }
    meminfo.close();
    return (total > 0) ? 100.0 * (total - free) / total : 0.0;
}

void MemoryManager::monitorMemory(const SchedulerConfig& config) {
    double usage = getSystemMemoryUsage();
    Logger::log("System Memory Usage: " + std::to_string(usage) + "%");
    if (usage > config.memory_threshold_mb / 100.0) {
        Logger::log("Memory threshold exceeded, optimizing...");
        auto processes = ProcessManager().getRunningProcesses();
        for (const auto& proc : processes) {
            optimizeMemory(proc.pid, proc.memory_usage);
        }
    }
}

void MemoryManager::optimizeMemory(int pid, long memory_usage) {
    simulateZswapCompression(pid, memory_usage);
    manageSwap(pid, memory_usage);
    predictMemoryNeeds(pid);
}

void MemoryManager::simulateZswapCompression(int pid, long memory_usage) {
    double compression_ratio = 0.5; // Simulated compression
    Logger::log("Simulating zswap compression for PID " + std::to_string(pid) + ": " + std::to_string(memory_usage * compression_ratio) + " KB");
}

void MemoryManager::manageSwap(int pid, long memory_usage) {
    Logger::log("Managing swap for PID " + std::to_string(pid) + ": " + std::to_string(memory_usage) + " KB");
}

void MemoryManager::predictMemoryNeeds(int pid) {
    memoryTrend[pid] = memoryTrend[pid] * 0.8 + getSystemMemoryUsage() * 0.2; // Exponential moving average
    Logger::log("Predicted memory need for PID " + std::to_string(pid) + ": " + std::to_string(memoryTrend[pid]) + "%");
}
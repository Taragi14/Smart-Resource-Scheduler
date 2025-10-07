#include "SystemMonitor.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <numeric>

double SystemMonitor::getSystemCPUUsage() {
    std::ifstream stat("/proc/stat");
    std::string line;
    std::getline(stat, line);
    std::istringstream iss(line);
    std::string cpu;
    long user, nice, system, idle;
    iss >> cpu >> user >> nice >> system >> idle;
    stat.close();
    long total = user + nice + system + idle;
    double usage = (total > 0) ? 100.0 * (total - idle) / total : 0.0;
    cpuHistory.push_back(usage);
    if (cpuHistory.size() > 100) cpuHistory.erase(cpuHistory.begin());
    return usage;
}

double SystemMonitor::getSystemMemoryUsage() {
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

double SystemMonitor::calculateMovingAverageCPU() {
    if (cpuHistory.empty()) return 0.0;
    double sum = std::accumulate(cpuHistory.begin(), cpuHistory.end(), 0.0);
    return sum / cpuHistory.size();
}

void SystemMonitor::logSystemStats() {
    Logger::log("CPU Usage: " + std::to_string(getSystemCPUUsage()) + "%, Moving Avg: " + std::to_string(calculateMovingAverageCPU()) + "%");
    Logger::log("Memory Usage: " + std::to_string(getSystemMemoryUsage()) + "%");
}
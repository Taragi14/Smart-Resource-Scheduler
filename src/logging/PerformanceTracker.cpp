#include "PerformanceTracker.h"
#include "Logger.h"
#include <numeric>
#include <fstream>

void PerformanceTracker::trackCPU(double usage) {
    cpu_usages.push_back(usage);
    if (cpu_usages.size() > 1000) cpu_usages.erase(cpu_usages.begin());
    Logger::log("Tracked CPU usage: " + std::to_string(usage) + "%");
}

void PerformanceTracker::trackMemory(double usage) {
    memory_usages.push_back(usage);
    if (memory_usages.size() > 1000) memory_usages.erase(memory_usages.begin());
    Logger::log("Tracked Memory usage: " + std::to_string(usage) + "%");
}

double PerformanceTracker::calculateVariance(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double variance = 0.0;
    for (double x : data) {
        variance += (x - mean) * (x - mean);
    }
    return variance / data.size();
}

void PerformanceTracker::generateReport() {
    std::ofstream report("logs/performance_report.json");
    report << "{\n";
    report << "  \"cpu_variance\": " << calculateVariance(cpu_usages) << ",\n";
    report << "  \"memory_variance\": " << calculateVariance(memory_usages) << "\n";
    report << "}\n";
    report.close();
    Logger::log("Generated performance report");
}
#ifndef PERFORMANCE_TRACKER_H
#define PERFORMANCE_TRACKER_H

#include <vector>
#include <string>

class PerformanceTracker {
public:
    void trackCPU(double usage);
    void trackMemory(double usage);
    void generateReport();

private:
    std::vector<double> cpu_usages;
    std::vector<double> memory_usages;
    double calculateVariance(const std::vector<double>& data);
};

#endif
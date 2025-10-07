#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include "types.h"
#include <vector>

class SystemMonitor {
public:
    double getSystemCPUUsage();
    double getSystemMemoryUsage();
    void logSystemStats();
    double calculateMovingAverageCPU();

private:
    std::vector<double> cpuHistory;
};

#endif
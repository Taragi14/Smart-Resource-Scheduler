#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include "types.h"
#include <map>

class MemoryManager {
public:
    void monitorMemory(const SchedulerConfig& config);
    void optimizeMemory(int pid, long memory_usage);
    double getSystemMemoryUsage();
    void predictMemoryNeeds(int pid);

private:
    void simulateZswapCompression(int pid, long memory_usage);
    void manageSwap(int pid, long memory_usage);
    std::map<int, double> memoryTrend; // For predictive allocation
};

#endif
#ifndef GAMING_MODE_H
#define GAMING_MODE_H

#include "types.h"
#include "ProcessManager.h"

class GamingMode {
public:
    void apply(const SchedulerConfig& config, ProcessManager& processManager);
    void optimizeForLowLatency(int pid);
};

#endif
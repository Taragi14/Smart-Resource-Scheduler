#ifndef POWER_SAVING_MODE_H
#define POWER_SAVING_MODE_H

#include "types.h"
#include "ProcessManager.h"

class PowerSavingMode {
public:
    void apply(const SchedulerConfig& config, ProcessManager& processManager);
};

#endif
#ifndef PRODUCTIVITY_MODE_H
#define PRODUCTIVITY_MODE_H

#include "types.h"
#include "ProcessManager.h"

class ProductivityMode {
public:
    void apply(const SchedulerConfig& config, ProcessManager& processManager);
};

#endif
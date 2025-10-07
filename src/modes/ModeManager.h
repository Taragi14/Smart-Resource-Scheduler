#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include "types.h"
#include "ConfigManager.h"
#include "ProcessManager.h"
#include "MemoryManager.h"
#include "SystemMonitor.h"

class ModeManager {
public:
    ModeManager();
    void setMode(const std::string& mode);
    void applyScheduling();
    SchedulerConfig getConfig() const;

private:
    SchedulerConfig config;
    ProcessManager processManager;
    MemoryManager memoryManager;
    SystemMonitor systemMonitor;
    ConfigManager configManager;
    void adjustPrioritiesDynamically();
};

#endif
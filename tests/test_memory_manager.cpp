#include "MemoryManager.h"
#include "Logger.h"
#include <cassert>

void testMemoryManager() {
    MemoryManager mm;
    SchedulerConfig config;
    config.memory_threshold_mb = 2048;
    mm.monitorMemory(config);
    assert(mm.getSystemMemoryUsage() >= 0.0);
    Logger::log("MemoryManager test passed");
}

int main() {
    testMemoryManager();
    return 0;
}
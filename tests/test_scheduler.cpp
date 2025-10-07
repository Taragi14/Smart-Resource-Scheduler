#include "Scheduler.h"
#include "Logger.h"
#include <cassert>

void testScheduler() {
    Scheduler scheduler;
    scheduler.setMode("Gaming");
    assert(scheduler.getCurrentCPULoad() >= 0.0);
    scheduler.startScheduling();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    scheduler.stopScheduling();
    Logger::log("Scheduler test passed");
}

int main() {
    testScheduler();
    return 0;
}
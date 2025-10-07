#include "Scheduler.h"
#include "SystemMonitor.h"
#include "IPCManager.h"
#include <iostream>

int main(int argc, char* argv[]) {
    Scheduler scheduler;
    SystemMonitor monitor;
    IPCManager ipc;

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "get_cpu") {
            std::cout << monitor.getSystemCPUUsage() << std::endl;
            return 0;
        } else if (arg == "get_mem") {
            std::cout << monitor.getSystemMemoryUsage() << std::endl;
            return 0;
        } else {
            scheduler.setMode(arg);
        }
    }

    scheduler.startScheduling();
    monitor.logSystemStats();
    std::cout << "Smart Resource Scheduler running\n";
    return 0;
}
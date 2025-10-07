#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "ModeManager.h"
#include "ThreadPool.h"
#include "IPCManager.h"
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

class Scheduler {
public:
    Scheduler();
    ~Scheduler();
    void setMode(const std::string& mode);
    void startScheduling();
    void stopScheduling();
    void scheduleProcesses();
    void adjustQuantumBasedOnLoad();
    double getCurrentCPULoad();

private:
    std::atomic<bool> running;
    std::mutex mtx;
    std::vector<std::thread> workerThreads;
    ModeManager modeManager;
    ThreadPool threadPool;
    IPCManager ipcManager;
    std::map<int, double> processLoadHistory; // For adaptive scheduling

    void scheduleWorker();
    void updateProcessLoad(int pid, double load);
};

#endif
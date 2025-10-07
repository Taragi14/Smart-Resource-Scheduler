#include "Scheduler.h"
#include "Logger.h"
#include "SystemMonitor.h"
#include <chrono>
#include <numeric>

Scheduler::Scheduler() : running(false), threadPool(4) {
    Logger::log("Scheduler initialized with 4 worker threads and IPC");
}

Scheduler::~Scheduler() {
    stopScheduling();
}

void Scheduler::setMode(const std::string& mode) {
    std::lock_guard<std::mutex> lock(mtx);
    modeManager.setMode(mode);
    ipcManager.sendMessage("Mode changed to: " + mode);
    Logger::log("Mode set to: " + mode);
}

void Scheduler::startScheduling() {
    std::lock_guard<std::mutex> lock(mtx);
    if (running) return;
    running = true;
    workerThreads.emplace_back(&Scheduler::scheduleWorker, this);
    Logger::log("Scheduling started");
}

void Scheduler::stopScheduling() {
    std::lock_guard<std::mutex> lock(mtx);
    running = false;
    threadPool.stop();
    for (auto& thread : workerThreads) {
        if (thread.joinable()) thread.join();
    }
    workerThreads.clear();
    Logger::log("Scheduling stopped");
}

void Scheduler::scheduleWorker() {
    SystemMonitor monitor;
    while (running) {
        adjustQuantumBasedOnLoad();
        scheduleProcesses();
        std::this_thread::sleep_for(std::chrono::milliseconds(modeManager.getConfig().time_quantum_ms));
    }
}

void Scheduler::scheduleProcesses() {
    threadPool.enqueue([this]() {
        modeManager.applyScheduling();
        ipcManager.sendMessage("Scheduling cycle completed");
    });
}

void Scheduler::adjustQuantumBasedOnLoad() {
    SystemMonitor monitor;
    double load = monitor.getSystemCPUUsage();
    auto& config = modeManager.getConfig();
    if (load > 80.0) {
        config.time_quantum_ms = std::max(5, config.time_quantum_ms - 5);
    } else if (load < 20.0) {
        config.time_quantum_ms = std::min(100, config.time_quantum_ms + 5);
    }
    Logger::log("Adjusted quantum to " + std::to_string(config.time_quantum_ms) + "ms based on CPU load: " + std::to_string(load));
}

double Scheduler::getCurrentCPULoad() {
    SystemMonitor monitor;
    return monitor.getSystemCPUUsage();
}

void Scheduler::updateProcessLoad(int pid, double load) {
    processLoadHistory[pid] = load;
    if (processLoadHistory.size() > 100) {
        processLoadHistory.erase(processLoadHistory.begin());
    }
}
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <queue>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include "SystemMonitor.h"
#include "MemoryManager.h"

using namespace std;

namespace SmartScheduler {

struct Process {
    int id;
    string name;
    size_t memory_requirement;
    int priority; // higher = higher priority
};

class Scheduler {
public:
    Scheduler(SystemMonitor& sys_monitor, MemoryManager& mem_manager);
    ~Scheduler();

    // Core functionality
    void addProcess(const Process& process);
    void removeProcess(int process_id);
    void startScheduling();
    void stopScheduling();
    bool isRunning() const;

    // Callback registration
    void setProcessStartedCallback(function<void(const Process&)> callback);
    void setProcessStoppedCallback(function<void(const Process&)> callback);

    // Configuration
    void setSchedulingInterval(chrono::milliseconds interval);

private:
    void schedulingLoop();
    void scheduleNextProcess();

    SystemMonitor& sys_monitor_;
    MemoryManager& mem_manager_;

    unordered_map<int, Process> process_map_;
    queue<int> ready_queue_;

    thread scheduling_thread_;
    atomic<bool> running_;
    chrono::milliseconds interval_;

    mutable mutex scheduler_mutex_;

    // Callbacks
    function<void(const Process&)> process_started_callback_;
    function<void(const Process&)> process_stopped_callback_;
};

} // namespace SmartScheduler

#endif // SCHEDULER_H

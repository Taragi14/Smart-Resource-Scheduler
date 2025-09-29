#include "Scheduler.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>

using namespace std;

namespace SmartScheduler {

Scheduler::Scheduler(SystemMonitor& sys_monitor, MemoryManager& mem_manager)
    : sys_monitor_(sys_monitor),
      mem_manager_(mem_manager),
      running_(false),
      interval_(chrono::milliseconds(1000)) {}

Scheduler::~Scheduler() {
    stopScheduling();
}

void Scheduler::addProcess(const Process& process) {
    lock_guard<mutex> lock(scheduler_mutex_);
    process_map_[process.id] = process;
    ready_queue_.push(process.id);
}

void Scheduler::removeProcess(int process_id) {
    lock_guard<mutex> lock(scheduler_mutex_);
    process_map_.erase(process_id);

    // Rebuild queue without removed process
    queue<int> new_queue;
    while (!ready_queue_.empty()) {
        int pid = ready_queue_.front(); ready_queue_.pop();
        if (pid != process_id) new_queue.push(pid);
    }
    swap(ready_queue_, new_queue);
}

void Scheduler::startScheduling() {
    if (running_) return;
    running_ = true;
    scheduling_thread_ = thread(&Scheduler::schedulingLoop, this);
}

void Scheduler::stopScheduling() {
    running_ = false;
    if (scheduling_thread_.joinable()) {
        scheduling_thread_.join();
    }
}

bool Scheduler::isRunning() const {
    return running_;
}

void Scheduler::setSchedulingInterval(chrono::milliseconds interval) {
    interval_ = interval;
}

void Scheduler::setProcessStartedCallback(function<void(const Process&)> callback) {
    process_started_callback_ = callback;
}

void Scheduler::setProcessStoppedCallback(function<void(const Process&)> callback) {
    process_stopped_callback_ = callback;
}

// -------------------- Internal --------------------
void Scheduler::schedulingLoop() {
    while (running_) {
        scheduleNextProcess();
        this_thread::sleep_for(interval_);
    }
}

void Scheduler::scheduleNextProcess() {
    lock_guard<mutex> lock(scheduler_mutex_);
    if (ready_queue_.empty()) return;

    int process_id = ready_queue_.front();
    ready_queue_.pop();

    auto it = process_map_.find(process_id);
    if (it == process_map_.end()) return;

    Process& proc = it->second;

    // Check memory availability
    if (!mem_manager_.allocateMemory(proc.id, proc.memory_requirement)) {
        // Cannot run due to memory pressure, push back to queue
        ready_queue_.push(proc.id);
        return;
    }

    // Simulate process start
    if (process_started_callback_) process_started_callback_(proc);

    // Simulate CPU usage check
    auto cpu_usage = sys_monitor_.getCurrentCpuUsage();
    if (cpu_usage > 90.0) {
        // High CPU, pause process
        if (process_stopped_callback_) process_stopped_callback_(proc);
        mem_manager_.freeMemory(proc.id, proc.memory_requirement);
        ready_queue_.push(proc.id);
        return;
    }

    // Process completes (simulate instantly for now)
    if (process_stopped_callback_) process_stopped_callback_(proc);
    mem_manager_.freeMemory(proc.id, proc.memory_requirement);
}

} // namespace SmartScheduler

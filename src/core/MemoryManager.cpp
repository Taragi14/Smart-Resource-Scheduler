#include "MemoryManager.h"
#include <iostream>
#include <algorithm>

using namespace std;

namespace SmartScheduler {

MemoryManager::MemoryManager(size_t total_memory)
    : total_memory_(total_memory),
      allocated_memory_(0),
      memory_threshold_percent_(DEFAULT_MEMORY_THRESHOLD_PERCENT),
      high_memory_alerted_(false) {}

MemoryManager::~MemoryManager() {
    shutdown();
}

bool MemoryManager::initialize() {
    lock_guard<mutex> lock(memory_mutex_);
    allocated_memory_ = 0;
    process_memory_.clear();
    high_memory_alerted_ = false;
    return true;
}

void MemoryManager::shutdown() {
    lock_guard<mutex> lock(memory_mutex_);
    process_memory_.clear();
    allocated_memory_ = 0;
}

bool MemoryManager::allocateMemory(int process_id, size_t amount) {
    lock_guard<mutex> lock(memory_mutex_);
    if (allocated_memory_ + amount > total_memory_) {
        return false; // not enough memory
    }

    process_memory_[process_id] += amount;
    allocated_memory_ += amount;

    checkThresholds();
    return true;
}

bool MemoryManager::freeMemory(int process_id, size_t amount) {
    lock_guard<mutex> lock(memory_mutex_);
    auto it = process_memory_.find(process_id);
    if (it == process_memory_.end() || it->second < amount) {
        return false; // invalid free request
    }

    it->second -= amount;
    allocated_memory_ -= amount;

    if (it->second == 0) {
        process_memory_.erase(it);
    }

    checkThresholds();
    return true;
}

size_t MemoryManager::getProcessMemoryUsage(int process_id) const {
    lock_guard<mutex> lock(memory_mutex_);
    auto it = process_memory_.find(process_id);
    return (it != process_memory_.end()) ? it->second : 0;
}

size_t MemoryManager::getTotalAllocatedMemory() const {
    lock_guard<mutex> lock(memory_mutex_);
    return allocated_memory_;
}

size_t MemoryManager::getAvailableMemory() const {
    lock_guard<mutex> lock(memory_mutex_);
    return (total_memory_ > allocated_memory_) ? total_memory_ - allocated_memory_ : 0;
}

bool MemoryManager::isMemoryPressure() const {
    lock_guard<mutex> lock(memory_mutex_);
    double usage_percent = (total_memory_ > 0)
        ? (100.0 * allocated_memory_ / total_memory_)
        : 0.0;
    return usage_percent > memory_threshold_percent_;
}

void MemoryManager::setMemoryThreshold(double percent) {
    lock_guard<mutex> lock(memory_mutex_);
    memory_threshold_percent_ = percent;
}

void MemoryManager::setHighMemoryCallback(function<void(double)> callback) {
    lock_guard<mutex> lock(memory_mutex_);
    high_memory_callback_ = callback;
}

void MemoryManager::checkThresholds() {
    if (total_memory_ == 0) return;

    double usage_percent = 100.0 * allocated_memory_ / total_memory_;
    if (usage_percent > memory_threshold_percent_ && !high_memory_alerted_) {
        if (high_memory_callback_) {
            high_memory_callback_(usage_percent);
        }
        high_memory_alerted_ = true;
    } else if (usage_percent <= memory_threshold_percent_) {
        high_memory_alerted_ = false;
    }
}

} // namespace SmartScheduler

#include "PerformanceTracker.h"
#include <algorithm>
#include <numeric>
#include <iostream>
#include <sstream>
#include <iomanip>

PerformanceTracker::PerformanceTracker(std::shared_ptr<SystemMonitor> monitor,
                                       std::shared_ptr<ProcessManager> process_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , tracking_active_(false)
    , tracking_interval_(std::chrono::milliseconds(5000))
    , max_snapshots_(1000)
    , auto_log_(false) {
    
    snapshots_.reserve(max_snapshots_);
}

PerformanceTracker::~PerformanceTracker() {
    stopTracking();
}

void PerformanceTracker::startTracking() {
    if (tracking_active_) {
        return;
    }
    
    tracking_active_ = true;
    tracking_thread_ = std::thread(&PerformanceTracker::trackingLoop, this);
}

void PerformanceTracker::stopTracking() {
    if (tracking_active_) {
        tracking_active_ = false;
        if (tracking_thread_.joinable()) {
            tracking_thread_.join();
        }
    }
}

void PerformanceTracker::trackingLoop() {
    while (tracking_active_) {
        try {
            captureSnapshot();
            pruneOldSnapshots();
            
            if (auto_log_ && !log_file_path_.empty()) {
                exportToCSV(log_file_path_);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Performance tracking error: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(tracking_interval_);
    }
}

void PerformanceTracker::captureSnapshot() {
    PerformanceSnapshot snapshot;
    snapshot.timestamp = std::chrono::system_clock::now();
    
    // Get system statistics
    auto sys_stats = system_monitor_->getSystemStatistics();
    snapshot.cpu_usage = sys_stats.cpu_usage_total;
    snapshot.load_average = sys_stats.load_average_1min;
    
    // Calculate memory usage
    if (sys_stats.memory_total_kb > 0) {
        snapshot.memory_usage = 100.0 * sys_stats.memory_used_kb / sys_stats.memory_total_kb;
    } else {
        snapshot.memory_usage = 0.0;
    }
    
    // Get process count
    auto processes = system_monitor_->getProcesses();
    snapshot.process_count = processes.size();
    
    // Approximate response time (simplified)
    snapshot.response_time = snapshot.cpu_usage / 100.0 * 10.0; // milliseconds
    
    // Context switches (would need scheduler integration for accurate count)
    snapshot.context_switches = 0;
    
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    snapshots_.push_back(snapshot);
}

void PerformanceTracker::pruneOldSnapshots() {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    if (snapshots_.size() > max_snapshots_) {
        size_t to_remove = snapshots_.size() - max_snapshots_;
        snapshots_.erase(snapshots_.begin(), snapshots_.begin() + to_remove);
    }
}

void PerformanceTracker::updateMetrics() {
    captureSnapshot();
}

PerformanceStats PerformanceTracker::getStatistics() const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    PerformanceStats stats;
    std::memset(&stats, 0, sizeof(PerformanceStats));
    
    if (snapshots_.empty()) {
        return stats;
    }
    
    stats.total_snapshots = snapshots_.size();
    stats.start_time = snapshots_.front().timestamp;
    stats.end_time = snapshots_.back().timestamp;
    
    // Calculate CPU statistics
    double total_cpu = 0.0;
    stats.max_cpu_usage = 0.0;
    stats.min_cpu_usage = 100.0;
    
    for (const auto& snapshot : snapshots_) {
        total_cpu += snapshot.cpu_usage;
        stats.max_cpu_usage = std::max(stats.max_cpu_usage, snapshot.cpu_usage);
        stats.min_cpu_usage = std::min(stats.min_cpu_usage, snapshot.cpu_usage);
    }
    
    stats.avg_cpu_usage = total_cpu / snapshots_.size();
    
    // Calculate memory statistics
    double total_memory = 0.0;
    stats.max_memory_usage = 0.0;
    
    for (const auto& snapshot : snapshots_) {
        total_memory += snapshot.memory_usage;
        stats.max_memory_usage = std::max(stats.max_memory_usage, snapshot.memory_usage);
    }
    
    stats.avg_memory_usage = total_memory / snapshots_.size();
    
    return stats;
}

std::vector<PerformanceSnapshot> PerformanceTracker::getSnapshots(size_t count) const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    if (count == 0 || count >= snapshots_.size()) {
        return snapshots_;
    }
    
    // Return most recent 'count' snapshots
    return std::vector<PerformanceSnapshot>(
        snapshots_.end() - count, 
        snapshots_.end()
    );
}

PerformanceSnapshot PerformanceTracker::getLatestSnapshot() const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    if (snapshots_.empty()) {
        return PerformanceSnapshot();
    }
    
    return snapshots_.back();
}

double PerformanceTracker::getAverageCpuUsage(std::chrono::minutes duration) const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    if (snapshots_.empty()) {
        return 0.0;
    }
    
    auto cutoff_time = std::chrono::system_clock::now() - duration;
    double total = 0.0;
    size_t count = 0;
    
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
        if (it->timestamp < cutoff_time) {
            break;
        }
        total += it->cpu_usage;
        count++;
    }
    
    return count > 0 ? total / count : 0.0;
}

double PerformanceTracker::getAverageMemoryUsage(std::chrono::minutes duration) const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    if (snapshots_.empty()) {
        return 0.0;
    }
    
    auto cutoff_time = std::chrono::system_clock::now() - duration;
    double total = 0.0;
    size_t count = 0;
    
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
        if (it->timestamp < cutoff_time) {
            break;
        }
        total += it->memory_usage;
        count++;
    }
    
    return count > 0 ? total / count : 0.0;
}

void PerformanceTracker::clearHistory() {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    snapshots_.clear();
}

bool PerformanceTracker::exportToCSV(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    // Write header
    file << "Timestamp,CPU Usage (%),Memory Usage (%),Process Count,Load Average,Response Time (ms)\n";
    
    // Write data
    for (const auto& snapshot : snapshots_) {
        auto time_t = std::chrono::system_clock::to_time_t(snapshot.timestamp);
        file << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << ","
             << snapshot.cpu_usage << ","
             << snapshot.memory_usage << ","
             << snapshot.process_count << ","
             << snapshot.load_average << ","
             << snapshot.response_time << "\n";
    }
    
    file.close();
    return true;
}

bool PerformanceTracker::exportToJSON(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(tracker_mutex_);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << "{\n";
    file << "  \"snapshots\": [\n";
    
    for (size_t i = 0; i < snapshots_.size(); ++i) {
        const auto& snapshot = snapshots_[i];
        auto time_t = std::chrono::system_clock::to_time_t(snapshot.timestamp);
        
        file << "    {\n";
        file << "      \"timestamp\": \"" << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\",\n";
        file << "      \"cpu_usage\": " << snapshot.cpu_usage << ",\n";
        file << "      \"memory_usage\": " << snapshot.memory_usage << ",\n";
        file << "      \"process_count\": " << snapshot.process_count << ",\n";
        file << "      \"load_average\": " << snapshot.load_average << ",\n";
        file << "      \"response_time\": " << snapshot.response_time << "\n";
        file << "    }";
        
        if (i < snapshots_.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    
    file << "  ]\n";
    file << "}\n";
    
    file.close();
    return true;
}

void PerformanceTracker::setTrackingInterval(std::chrono::milliseconds interval) {
    tracking_interval_ = interval;
}

void PerformanceTracker::setMaxSnapshots(size_t max) {
    max_snapshots_ = max;
}

void PerformanceTracker::setLogFile(const std::string& filepath) {
    log_file_path_ = filepath;
}
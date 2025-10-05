#ifndef PERFORMANCE_TRACKER_H
#define PERFORMANCE_TRACKER_H

#include "SystemMonitor.h"
#include "ProcessManager.h"
#include <memory>
#include <vector>
#include <chrono>
#include <mutex>
#include <fstream>

struct PerformanceSnapshot {
    std::chrono::system_clock::time_point timestamp;
    double cpu_usage;
    double memory_usage;
    size_t process_count;
    size_t context_switches;
    double load_average;
    double response_time;
};

struct PerformanceStats {
    double avg_cpu_usage;
    double max_cpu_usage;
    double min_cpu_usage;
    double avg_memory_usage;
    double max_memory_usage;
    size_t total_snapshots;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
};

class PerformanceTracker {
private:
    std::shared_ptr<SystemMonitor> system_monitor_;
    std::shared_ptr<ProcessManager> process_manager_;
    
    std::vector<PerformanceSnapshot> snapshots_;
    mutable std::mutex tracker_mutex_;
    
    bool tracking_active_;
    std::thread tracking_thread_;
    std::chrono::milliseconds tracking_interval_;
    
    size_t max_snapshots_;
    std::string log_file_path_;
    bool auto_log_;
    
    void trackingLoop();
    void captureSnapshot();
    void pruneOldSnapshots();

public:
    PerformanceTracker(std::shared_ptr<SystemMonitor> monitor,
                      std::shared_ptr<ProcessManager> process_manager);
    ~PerformanceTracker();
    
    void startTracking();
    void stopTracking();
    bool isTracking() const { return tracking_active_; }
    
    void setTrackingInterval(std::chrono::milliseconds interval);
    void setMaxSnapshots(size_t max);
    void setLogFile(const std::string& filepath);
    void setAutoLog(bool enable) { auto_log_ = enable; }
    
    PerformanceStats getStatistics() const;
    std::vector<PerformanceSnapshot> getSnapshots(size_t count = 0) const;
    PerformanceSnapshot getLatestSnapshot() const;
    
    void updateMetrics();
    void clearHistory();
    bool exportToCSV(const std::string& filepath) const;
    bool exportToJSON(const std::string& filepath) const;
    
    double getAverageCpuUsage(std::chrono::minutes duration) const;
    double getAverageMemoryUsage(std::chrono::minutes duration) const;
};

#endif
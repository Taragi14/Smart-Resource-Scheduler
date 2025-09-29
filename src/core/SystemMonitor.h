#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include <vector>
#include <deque>
#include <string>

using namespace std;

namespace SmartScheduler {

// System resource information
struct SystemStats {
    double cpu_usage;
    size_t total_memory;
    size_t used_memory;
    size_t available_memory;
    double memory_percent;
    int active_processes;
    double load_average_1min;
    double load_average_5min;
    double load_average_15min;
    chrono::steady_clock::time_point timestamp;
};

// Historical data point for trending
struct HistoricalDataPoint {
    chrono::steady_clock::time_point timestamp;
    double cpu_usage;
    double memory_usage;
    double load_average;
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    // Core functionality
    bool initialize();
    void shutdown();
    
    // Monitoring control
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_active_.load(); }
    
    // Current system state
    SystemStats getCurrentStats();
    double getCurrentCpuUsage();
    double getCurrentMemoryUsage();
    size_t getAvailableMemory();
    size_t getTotalMemory();
    
    // Historical data
    vector<HistoricalDataPoint> getHistoricalData(chrono::minutes duration);
    void clearHistoricalData();
    
    // System health monitoring
    bool isSystemUnderHighLoad() const;
    bool isMemoryPressure() const;
    bool isCpuPressure() const;
    
    // Thresholds and alerts
    void setCpuThreshold(double threshold);
    void setMemoryThreshold(double threshold);
    void setLoadThreshold(double threshold);
    
    // Callback registration
    void setSystemStatsCallback(function<void(const SystemStats&)> callback);
    void setHighCpuCallback(function<void(double)> callback);
    void setHighMemoryCallback(function<void(double)> callback);
    void setSystemOverloadCallback(function<void()> callback);
    
    // Configuration
    void setUpdateInterval(chrono::milliseconds interval);
    void setHistoryDuration(chrono::minutes duration);
    
    // System information
    int getCpuCoreCount() const;
    string getCpuModel() const;
    string getKernelVersion() const;
    string getDistribution() const;
    
    // Network monitoring (basic)
    struct NetworkStats {
        size_t bytes_sent;
        size_t bytes_received;
        size_t packets_sent;
        size_t packets_received;
    };
    NetworkStats getNetworkStats();
    
    // Disk I/O monitoring (basic)
    struct DiskStats {
        size_t read_bytes;
        size_t write_bytes;
        size_t read_ops;
        size_t write_ops;
    };
    DiskStats getDiskStats();

private:
    // Monitoring thread
    void monitoringLoop();
    
    // System data collection
    SystemStats collectSystemStats();
    double calculateCpuUsage();
    void updateMemoryStats(SystemStats& stats);
    void updateLoadAverages(SystemStats& stats);
    
    // Historical data management
    void addHistoricalDataPoint(const SystemStats& stats);
    void pruneHistoricalData();
    
    // System file reading helpers
    bool readMemInfo();
    bool readCpuInfo();
    bool readLoadAvg();
    bool readStat();
    
    // Alert checking
    void checkThresholds(const SystemStats& stats);
    
    // Thread safety
    mutable mutex stats_mutex_;
    mutable mutex history_mutex_;
    mutable mutex callbacks_mutex_;
    
    // Current system state
    SystemStats current_stats_;
    
    // Historical data storage
    deque<HistoricalDataPoint> historical_data_;
    chrono::minutes history_duration_;
    
    // CPU calculation data
    struct CpuTimes {
        long long user, nice, system, idle, iowait, irq, softirq, steal;
        long long total() const { return user + nice + system + idle + iowait + irq + softirq + steal; }
        long long active() const { return user + nice + system + irq + softirq + steal; }
    };
    CpuTimes last_cpu_times_;
    bool cpu_times_initialized_;
    
    // Monitoring thread
    thread monitoring_thread_;
    atomic<bool> monitoring_active_;
    atomic<bool> shutdown_requested_;
    
    // Configuration
    chrono::milliseconds update_interval_;
    
    // Thresholds
    double cpu_threshold_;
    double memory_threshold_;
    double load_threshold_;
    
    // Callbacks
    function<void(const SystemStats&)> stats_callback_;
    function<void(double)> high_cpu_callback_;
    function<void(double)> high_memory_callback_;
    function<void()> system_overload_callback_;
    
    // System information cache
    int cpu_core_count_;
    string cpu_model_;
    string kernel_version_;
    string distribution_;
    
    // Network and disk tracking
    NetworkStats last_network_stats_;
    DiskStats last_disk_stats_;
    
    // Alert state tracking
    bool high_cpu_alerted_;
    bool high_memory_alerted_;
    bool system_overload_alerted_;
    
    // Constants
    static constexpr double DEFAULT_CPU_THRESHOLD = 80.0;
    static constexpr double DEFAULT_MEMORY_THRESHOLD = 85.0;
    static constexpr double DEFAULT_LOAD_THRESHOLD = 2.0;
    static constexpr int DEFAULT_UPDATE_INTERVAL_MS = 1000;
    static constexpr int DEFAULT_HISTORY_DURATION_MIN = 60;
    static constexpr size_t MAX_HISTORY_POINTS = 3600; // 1 hour at 1s intervals
};

} // namespace SmartScheduler

#endif // SYSTEM_MONITOR_H

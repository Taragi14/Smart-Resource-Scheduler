#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

using namespace std;

namespace SmartScheduler {

// Forward declarations
struct ProcessInfo;

// Memory usage information for a process
struct ProcessMemoryInfo {
    int pid;
    string process_name;
    size_t virtual_memory;    // VmSize in KB
    size_t resident_memory;   // VmRSS in KB  
    size_t shared_memory;     // VmShared in KB
    size_t private_memory;    // Private memory in KB
    size_t swap_memory;       // VmSwap in KB
    double memory_percent;    // Percentage of total system memory
    chrono::steady_clock::time_point last_update;
};

// System memory information
struct SystemMemoryInfo {
    size_t total_memory;      // Total system memory in KB
    size_t available_memory;  // Available memory in KB
    size_t used_memory;       // Used memory in KB
    size_t free_memory;       // Free memory in KB
    size_t buffers;           // Buffer memory in KB
    size_t cached;            // Cached memory in KB
    size_t swap_total;        // Total swap in KB
    size_t swap_used;         // Used swap in KB
    size_t swap_free;         // Free swap in KB
    double memory_pressure;   // Memory pressure indicator (0-100)
    chrono::steady_clock::time_point timestamp;
};

// Memory optimization action
enum class MemoryAction {
    NONE,
    DROP_CACHES,
    COMPACT_MEMORY,
    SUSPEND_PROCESS,
    TERMINATE_PROCESS,
    ADJUST_SWAPPINESS
};

// Memory optimization result
struct MemoryOptimizationResult {
    MemoryAction action;
    bool success;
    size_t memory_freed;  // Memory freed in KB
    string description;
    chrono::steady_clock::time_point timestamp;
};

class MemoryManager {
public:
    MemoryManager();
    ~MemoryManager();

    // Core functionality
    bool initialize();
    void shutdown();
    
    // Memory monitoring
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_active_.load(); }
    
    // System memory information
    SystemMemoryInfo getSystemMemoryInfo();
    double getMemoryUsagePercent();
    size_t getAvailableMemory();
    bool isMemoryPressure() const;
    bool isSwapActive() const;
    
    // Process memory tracking
    vector<ProcessMemoryInfo> getAllProcessMemoryInfo();
    ProcessMemoryInfo getProcessMemoryInfo(int pid);
    vector<ProcessMemoryInfo> getHighMemoryProcesses(size_t threshold_kb);
    vector<ProcessMemoryInfo> getTopMemoryConsumers(int count);
    
    // Memory optimization
    MemoryOptimizationResult optimizeMemory();
    MemoryOptimizationResult dropCaches();
    MemoryOptimizationResult compactMemory();
    bool adjustSwappiness(int value);
    
    // Process-specific memory management
    bool suspendHighMemoryProcesses(size_t threshold_kb);
    bool terminateNonCriticalProcesses(size_t threshold_kb);
    vector<int> identifyMemoryLeaks();
    
    // Memory thresholds and alerts
    void setMemoryPressureThreshold(double threshold);
    void setHighMemoryThreshold(size_t threshold_kb);
    void setCriticalMemoryThreshold(size_t threshold_kb);
    
    // Callback registration
    void setMemoryPressureCallback(function<void(double)> callback);
    void setHighMemoryProcessCallback(function<void(const ProcessMemoryInfo&)> callback);
    void setMemoryOptimizedCallback(function<void(const MemoryOptimizationResult&)> callback);
    void setOutOfMemoryCallback(function<void()> callback);
    
    // Configuration
    void setUpdateInterval(chrono::milliseconds interval);
    void setAutoOptimizeEnabled(bool enabled);
    void setAggressiveOptimization(bool enabled);
    
    // Statistics and reporting
    size_t getTotalMemoryAllocated() const;
    size_t getTotalMemoryFreed() const;
    int getOptimizationCount() const;
    double getAverageMemoryUsage() const;
    
    // Memory pool management (advanced)
    bool enableMemoryPool(size_t pool_size_mb);
    void disableMemoryPool();
    size_t getMemoryPoolUsage() const;

private:
    // Monitoring thread
    void monitoringLoop();
    
    // Memory information collection
    SystemMemoryInfo collectSystemMemoryInfo();
    ProcessMemoryInfo collectProcessMemoryInfo(int pid);
    void updateProcessMemoryInfo();
    
    // Memory analysis
    double calculateMemoryPressure(const SystemMemoryInfo& info);
    bool detectMemoryLeak(const ProcessMemoryInfo& current, const ProcessMemoryInfo& previous);
    
    // Optimization strategies
    MemoryOptimizationResult executeOptimizationStrategy();
    MemoryOptimizationResult basicOptimization();
    MemoryOptimizationResult aggressiveOptimization();
    
    // System memory operations
    bool dropPageCache();
    bool dropInodeDentryCache();
    bool dropAllCaches();
    bool triggerMemoryCompaction();
    
    // Process memory operations
    bool suspendProcess(int pid);
    bool resumeProcess(int pid);
    bool terminateProcess(int pid);
    
    // File system helpers
    bool readMemInfo(SystemMemoryInfo& info);
    bool readProcessMemInfo(int pid, ProcessMemoryInfo& info);
    bool writeToSysFile(const string& path, const string& value);
    
    // Alert checking
    void checkMemoryThresholds(const SystemMemoryInfo& info);
    void checkProcessMemoryThresholds();
    
    // Thread safety
    mutable mutex memory_info_mutex_;
    mutable mutex process_memory_mutex_;
    mutable mutex callbacks_mutex_;
    mutable mutex optimization_mutex_;
    
    // Current system state
    SystemMemoryInfo current_memory_info_;
    unordered_map<int, ProcessMemoryInfo> process_memory_info_;
    
    // Historical data for leak detection
    unordered_map<int, vector<ProcessMemoryInfo>> process_memory_history_;
    
    // Monitoring thread
    thread monitoring_thread_;
    atomic<bool> monitoring_active_;
    atomic<bool> shutdown_requested_;
    
    // Configuration
    chrono::milliseconds update_interval_;
    bool auto_optimize_enabled_;
    bool aggressive_optimization_;
    
    // Thresholds
    double memory_pressure_threshold_;
    size_t high_memory_threshold_;
    size_t critical_memory_threshold_;
    
    // Callbacks
    function<void(double)> memory_pressure_callback_;
    function<void(const ProcessMemoryInfo&)> high_memory_process_callback_;
    function<void(const MemoryOptimizationResult&)> memory_optimized_callback_;
    function<void()> out_of_memory_callback_;
    
    // Statistics
    mutable mutex stats_mutex_;
    size_t total_memory_allocated_;
    size_t total_memory_freed_;
    int optimization_count_;
    vector<double> memory_usage_history_;
    
    // Alert state tracking
    bool memory_pressure_alerted_;
    bool out_of_memory_alerted_;
    unordered_set<int> high_memory_processes_alerted_;
    
    // Memory pool (if enabled)
    bool memory_pool_enabled_;
    size_t memory_pool_size_;
    size_t memory_pool_used_;
    
    // Constants
    static constexpr double DEFAULT_MEMORY_PRESSURE_THRESHOLD = 80.0;
    static constexpr size_t DEFAULT_HIGH_MEMORY_THRESHOLD = 1024 * 1024; // 1GB in KB
    static constexpr size_t DEFAULT_CRITICAL_MEMORY_THRESHOLD = 512 * 1024; // 512MB in KB
    static constexpr int DEFAULT_UPDATE_INTERVAL_MS = 2000;
    static constexpr int MEMORY_HISTORY_SIZE = 10;
    static constexpr double MEMORY_LEAK_GROWTH_THRESHOLD = 1.5; // 50% growth
    static constexpr size_t MIN_PROCESS_MEMORY_MB = 10; // Don't optimize processes using less than 10MB
};

} // namespace SmartScheduler

#endif // MEMORY_MANAGER_H

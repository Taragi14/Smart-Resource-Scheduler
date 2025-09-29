#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <string>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

using namespace std;

// Forward declarations for includes
struct ProcessInfo {
    int pid;
    string name;
    string command;
    double cpu_percent;
    size_t memory_usage; // in KB
    int priority;
    string state;
    chrono::steady_clock::time_point start_time;
    chrono::steady_clock::time_point last_cpu_time;
    bool is_suspended;
    bool is_critical;
};

enum class ProcessAction {
    PAUSE,
    RESUME,
    TERMINATE,
    SET_PRIORITY,
    SET_AFFINITY
};

namespace SmartScheduler {

class ProcessManager {
public:
    ProcessManager();
    ~ProcessManager();

    // Core functionality
    bool initialize();
    void shutdown();
    
    // Process discovery and monitoring
    vector<shared_ptr<ProcessInfo>> getAllProcesses();
    shared_ptr<ProcessInfo> getProcessById(int pid);
    void refreshProcessList();
    
    // Process control operations
    bool pauseProcess(int pid);
    bool resumeProcess(int pid);
    bool terminateProcess(int pid);
    bool setPriority(int pid, int priority);
    bool setAffinity(int pid, const vector<int>& cpu_cores);
    
    // Process monitoring
    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_active.load(); }
    
    // Process filtering and categorization
    vector<shared_ptr<ProcessInfo>> getProcessesByName(const string& name);
    vector<shared_ptr<ProcessInfo>> getHighCpuProcesses(double threshold);
    vector<shared_ptr<ProcessInfo>> getHighMemoryProcesses(size_t threshold);
    vector<shared_ptr<ProcessInfo>> getCriticalProcesses();
    vector<shared_ptr<ProcessInfo>> getSuspendableProcesses();
    
    // Callback registration
    void setProcessChangeCallback(function<void(const ProcessInfo&)> callback);
    void setProcessTerminatedCallback(function<void(int)> callback);
    
    // Statistics
    int getActiveProcessCount() const;
    double getTotalCpuUsage() const;
    size_t getTotalMemoryUsage() const;
    
    // Critical process management
    void addCriticalProcess(const string& process_name);
    void removeCriticalProcess(const string& process_name);
    bool isCriticalProcess(const string& process_name) const;

private:
    // Internal process management
    void monitoringLoop();
    void updateProcessInfo(shared_ptr<ProcessInfo> process);
    shared_ptr<ProcessInfo> createProcessInfo(int pid);
    
    // System interaction helpers
    bool readProcessStats(int pid, ProcessInfo& info);
    bool readProcessStatus(int pid, ProcessInfo& info);
    bool readProcessCmdline(int pid, ProcessInfo& info);
    vector<int> getRunningPids();
    
    // CPU calculation helpers
    void calculateCpuUsage(shared_ptr<ProcessInfo> process);
    double getSystemCpuTime();
    
    // Process validation
    bool isValidProcess(int pid);
    bool hasPermission(int pid);
    
    // Thread safety
    mutable mutex processes_mutex_;
    mutable mutex critical_processes_mutex_;
    
    // Data storage
    unordered_map<int, shared_ptr<ProcessInfo>> processes_;
    unordered_set<string> critical_process_names_;
    
    // Monitoring thread
    thread monitoring_thread_;
    atomic<bool> monitoring_active_;
    atomic<bool> shutdown_requested_;
    
    // Callbacks
    function<void(const ProcessInfo&)> process_change_callback_;
    function<void(int)> process_terminated_callback_;
    
    // System state tracking
    double last_system_cpu_time_;
    chrono::steady_clock::time_point last_update_time_;
    
    // Configuration
    chrono::milliseconds update_interval_;
    
    // Constants
    static constexpr int INVALID_PID = -1;
    static constexpr double CPU_CALCULATION_INTERVAL = 1.0; // seconds
};

} // namespace SmartScheduler

#endif // PROCESS_MANAGER_H

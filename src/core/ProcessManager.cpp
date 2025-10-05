#include "ProcessManager.h"
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

ProcessManager::ProcessManager(std::shared_ptr<SystemMonitor> monitor)
    : system_monitor_(monitor)
    , auto_management_enabled_(false)
    , memory_protection_enabled_(true)
    , cpu_throttling_enabled_(true)
    , system_cpu_threshold_(90.0)
    , system_memory_threshold_(85.0)
    , memory_warning_threshold_kb_(1024 * 1024) // 1GB
    , cpu_warning_threshold_percent_(80.0)
    , monitoring_active_(false)
    , total_terminated_processes_(0)
    , total_suspended_processes_(0) {
    
    // Initialize critical system processes
    critical_process_names_ = {
        "init", "kernel", "kthreadd", "systemd", "dbus", "networkd",
        "X", "Xorg", "gdm", "lightdm", "pulseaudio", "NetworkManager"
    };
    
    // Initialize system processes
    system_process_names_ = {
        "ksoftirqd", "migration", "rcu_", "watchdog", "systemd-",
        "kworker", "irq/", "mmcqd", "jbd2", "ext4-", "usb-storage"
    };
    
    // Start monitoring thread
    monitoring_active_.store(true);
    monitor_thread_ = std::thread(&ProcessManager::monitoringLoop, this);
}

ProcessManager::~ProcessManager() {
    monitoring_active_.store(false);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    // Restore all managed processes before destruction
    restoreAllProcesses();
}

bool ProcessManager::terminateProcess(int pid) {
    if (!canModifyProcess(pid)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    // Update managed process info if it exists
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        it->second.current_state = ProcessState::TERMINATED;
        it->second.last_action_time = std::chrono::system_clock::now();
        total_terminated_processes_++;
    }
    
    bool success = killProcess(pid);
    notifyProcessAction(pid, "terminate", success);
    
    return success;
}

bool ProcessManager::pauseProcess(int pid) {
    if (!canModifyProcess(pid)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        it->second.current_state = ProcessState::SUSPENDED;
        it->second.last_action_time = std::chrono::system_clock::now();
        total_suspended_processes_++;
    }
    
    bool success = suspendProcess(pid);
    notifyProcessAction(pid, "suspend", success);
    
    return success;
}

bool ProcessManager::resumeProcess(int pid) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end() && it->second.current_state == ProcessState::SUSPENDED) {
        it->second.current_state = ProcessState::RUNNING;
        it->second.last_action_time = std::chrono::system_clock::now();
    }
    
    bool success = this->resumeProcess(pid);
    notifyProcessAction(pid, "resume", success);
    
    return success;
}

bool ProcessManager::setProcessPriority(int pid, ProcessPriority priority) {
    if (!canModifyProcess(pid)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        it->second.current_priority = priority;
        it->second.last_action_time = std::chrono::system_clock::now();
    }
    
    bool success = setPriority(pid, priority);
    notifyProcessAction(pid, "set_priority", success);
    
    return success;
}

bool ProcessManager::restoreProcessPriority(int pid) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        ProcessPriority original_priority = it->second.original_priority;
        it->second.current_priority = original_priority;
        it->second.last_action_time = std::chrono::system_clock::now();
        
        return setPriority(pid, original_priority);
    }
    
    return false;
}

// Batch operations
bool ProcessManager::terminateProcessesByName(const std::string& name) {
    auto processes = system_monitor_->getProcessesByName(name);
    bool all_success = true;
    
    for (const auto& proc : processes) {
        if (!terminateProcess(proc.pid)) {
            all_success = false;
        }
    }
    
    return all_success;
}

bool ProcessManager::pauseProcessesByCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    bool all_success = true;
    
    for (auto& [pid, managed_proc] : managed_processes_) {
        if (managed_proc.category == category && !managed_proc.is_critical) {
            if (!pauseProcess(pid)) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

bool ProcessManager::resumeProcessesByCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    bool all_success = true;
    
    for (auto& [pid, managed_proc] : managed_processes_) {
        if (managed_proc.category == category && managed_proc.current_state == ProcessState::SUSPENDED) {
            if (!resumeProcess(pid)) {
                all_success = false;
            }
        }
    }
    
    return all_success;
}

// Internal process control methods
bool ProcessManager::killProcess(int pid) {
    return sendSignal(pid, SIGTERM);
}

bool ProcessManager::suspendProcess(int pid) {
    return sendSignal(pid, SIGSTOP);
}

bool ProcessManager::resumeProcess(int pid) {
    return sendSignal(pid, SIGCONT);
}

bool ProcessManager::setPriority(int pid, ProcessPriority priority) {
    return setProcessNiceValue(pid, static_cast<int>(priority));
}

bool ProcessManager::sendSignal(int pid, int signal) {
    if (kill(pid, signal) == 0) {
        return true;
    }
    return false;
}

bool ProcessManager::setProcessNiceValue(int pid, int nice_value) {
    // Clamp nice value to valid range
    nice_value = std::clamp(nice_value, -20, 19);
    
    if (setpriority(PRIO_PROCESS, pid, nice_value) == 0) {
        return true;
    }
    return false;
}

int ProcessManager::getProcessNiceValue(int pid) {
    errno = 0;
    int nice_value = getpriority(PRIO_PROCESS, pid);
    
    if (errno == 0) {
        return nice_value;
    }
    return 0; // Default nice value
}

// Process management
void ProcessManager::addToManaged(int pid, bool is_critical) {
    if (!system_monitor_->isProcessRunning(pid)) {
        return;
    }
    
    ProcessInfo info = system_monitor_->getProcess(pid);
    if (info.pid == -1) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    ManagedProcess managed;
    managed.pid = pid;
    managed.name = info.name;
    managed.command = info.command;
    managed.original_state = ProcessState::RUNNING;
    managed.current_state = ProcessState::RUNNING;
    managed.original_priority = static_cast<ProcessPriority>(getProcessNiceValue(pid));
    managed.current_priority = managed.original_priority;
    managed.is_managed = true;
    managed.is_critical = is_critical || isProcessCritical(info.name);
    managed.memory_limit_kb = 0; // No limit by default
    managed.cpu_limit_percent = 100.0; // No limit by default
    managed.last_action_time = std::chrono::system_clock::now();
    managed.category = categorizeProcess(info);
    
    managed_processes_[pid] = managed;
}

void ProcessManager::removeFromManaged(int pid) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        // Restore original state if possible
        if (it->second.current_priority != it->second.original_priority) {
            setPriority(pid, it->second.original_priority);
        }
        
        if (it->second.current_state == ProcessState::SUSPENDED) {
            resumeProcess(pid);
        }
        
        managed_processes_.erase(it);
    }
}

bool ProcessManager::isManagedProcess(int pid) const {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    return managed_processes_.find(pid) != managed_processes_.end();
}

ManagedProcess ProcessManager::getManagedProcess(int pid) const {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        return it->second;
    }
    
    ManagedProcess empty;
    empty.pid = -1;
    return empty;
}

std::vector<ManagedProcess> ProcessManager::getAllManagedProcesses() const {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    std::vector<ManagedProcess> result;
    result.reserve(managed_processes_.size());
    
    for (const auto& [pid, managed_proc] : managed_processes_) {
        result.push_back(managed_proc);
    }
    
    return result;
}

// Resource limits
void ProcessManager::setMemoryLimit(int pid, size_t limit_kb) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        it->second.memory_limit_kb = limit_kb;
    }
}

void ProcessManager::setCpuLimit(int pid, double limit_percent) {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    auto it = managed_processes_.find(pid);
    if (it != managed_processes_.end()) {
        it->second.cpu_limit_percent = std::clamp(limit_percent, 0.0, 100.0);
    }
}

// Helper methods
bool ProcessManager::isProcessCritical(const std::string& name) const {
    return std::find(critical_process_names_.begin(), critical_process_names_.end(), name) 
           != critical_process_names_.end();
}

bool ProcessManager::isSystemProcess(const std::string& name) const {
    for (const auto& sys_name : system_process_names_) {
        if (name.find(sys_name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string ProcessManager::categorizeProcess(const ProcessInfo& info) const {
    if (isProcessCritical(info.name)) {
        return "critical";
    }
    
    if (isSystemProcess(info.name)) {
        return "system";
    }
    
    // Simple categorization based on process name patterns
    std::string lower_name = info.name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    if (lower_name.find("game") != std::string::npos ||
        lower_name.find("steam") != std::string::npos ||
        lower_name.find("wine") != std::string::npos) {
        return "gaming";
    }
    
    if (lower_name.find("browser") != std::string::npos ||
        lower_name.find("firefox") != std::string::npos ||
        lower_name.find("chrome") != std::string::npos ||
        lower_name.find("office") != std::string::npos ||
        lower_name.find("editor") != std::string::npos) {
        return "productivity";
    }
    
    return "user";
}

bool ProcessManager::canModifyProcess(int pid) const {
    // Check if process is critical
    ProcessInfo info = system_monitor_->getProcess(pid);
    if (info.pid == -1) {
        return false;
    }
    
    if (isProcessCritical(info.name)) {
        return false;
    }
    
    // Check permissions
    return hasPermission(pid);
}

bool ProcessManager::hasPermission(int pid) const {
    // Simple permission check - can we send signal 0 (null signal)?
    return kill(pid, 0) == 0;
}

// Monitoring loop
void ProcessManager::monitoringLoop() {
    while (monitoring_active_.load()) {
        try {
            updateManagedProcessInfo();
            
            if (auto_management_enabled_.load()) {
                checkResourceLimits();
                checkSystemThresholds();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "ProcessManager monitoring error: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

void ProcessManager::updateManagedProcessInfo() {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    // Remove processes that are no longer running
    auto it = managed_processes_.begin();
    while (it != managed_processes_.end()) {
        if (!system_monitor_->isProcessRunning(it->first)) {
            it = managed_processes_.erase(it);
        } else {
            ++it;
        }
    }
}

void ProcessManager::checkResourceLimits() {
    std::vector<int> violating_processes;
    
    {
        std::lock_guard<std::mutex> lock(processes_mutex_);
        
        for (const auto& [pid, managed_proc] : managed_processes_) {
            ProcessInfo current_info = system_monitor_->getProcess(pid);
            
            if (current_info.pid == -1) continue;
            
            // Check memory limit
            if (managed_proc.memory_limit_kb > 0 && 
                current_info.memory_usage_kb > managed_proc.memory_limit_kb) {
                
                notifyResourceLimitExceeded(pid, "memory", 
                                          current_info.memory_usage_kb, 
                                          managed_proc.memory_limit_kb);
                violating_processes.push_back(pid);
            }
            
            // Check CPU limit
            if (managed_proc.cpu_limit_percent < 100.0 && 
                current_info.cpu_usage > managed_proc.cpu_limit_percent) {
                
                notifyResourceLimitExceeded(pid, "cpu", 
                                          current_info.cpu_usage, 
                                          managed_proc.cpu_limit_percent);
                violating_processes.push_back(pid);
            }
        }
    }
    
    // Handle violating processes
    for (int pid : violating_processes) {
        if (cpu_throttling_enabled_.load()) {
            setProcessPriority(pid, ProcessPriority::LOW);
        }
    }
}

void ProcessManager::checkSystemThresholds() {
    SystemStats stats = system_monitor_->getSystemStatistics();
    
    // Check system CPU usage
    if (stats.cpu_usage_total > system_cpu_threshold_) {
        notifySystemThresholdExceeded("cpu", stats.cpu_usage_total, system_cpu_threshold_);
        handleHighSystemLoad();
    }
    
    // Check system memory usage
    double memory_usage_percent = 100.0 * stats.memory_used_kb / stats.memory_total_kb;
    if (memory_usage_percent > system_memory_threshold_) {
        notifySystemThresholdExceeded("memory", memory_usage_percent, system_memory_threshold_);
        
        if (memory_protection_enabled_.load()) {
            emergencyKillHighMemoryProcesses();
        }
    }
}

void ProcessManager::handleHighSystemLoad() {
    // Lower priority of non-critical processes
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    for (auto& [pid, managed_proc] : managed_processes_) {
        if (!managed_proc.is_critical && managed_proc.category != "gaming") {
            setProcessPriority(pid, ProcessPriority::LOW);
        }
    }
}

// Emergency actions
void ProcessManager::emergencyKillHighMemoryProcesses() {
    auto top_memory_processes = system_monitor_->getTopMemoryProcesses(5);
    
    for (const auto& proc : top_memory_processes) {
        if (!isProcessCritical(proc.name) && proc.memory_usage_kb > memory_warning_threshold_kb_) {
            std::cout << "Emergency terminating high memory process: " << proc.name 
                     << " (PID: " << proc.pid << ", Memory: " << proc.memory_usage_kb << " KB)" << std::endl;
            terminateProcess(proc.pid);
        }
    }
    
    last_emergency_action_ = std::chrono::system_clock::now();
}

void ProcessManager::restoreAllProcesses() {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    for (auto& [pid, managed_proc] : managed_processes_) {
        if (managed_proc.current_state == ProcessState::SUSPENDED) {
            resumeProcess(pid);
        }
        
        if (managed_proc.current_priority != managed_proc.original_priority) {
            setPriority(pid, managed_proc.original_priority);
        }
    }
}

// Statistics
size_t ProcessManager::getManagedProcessCount() const {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    return managed_processes_.size();
}

size_t ProcessManager::getSuspendedProcessCount() const {
    std::lock_guard<std::mutex> lock(processes_mutex_);
    
    return std::count_if(managed_processes_.begin(), managed_processes_.end(),
                        [](const auto& pair) {
                            return pair.second.current_state == ProcessState::SUSPENDED;
                        });
}

// Callback methods
void ProcessManager::registerProcessActionCallback(ProcessActionCallback callback) {
    action_callbacks_.push_back(callback);
}

void ProcessManager::registerResourceLimitCallback(ResourceLimitCallback callback) {
    resource_callbacks_.push_back(callback);
}

void ProcessManager::registerSystemThresholdCallback(SystemThresholdCallback callback) {
    threshold_callbacks_.push_back(callback);
}

void ProcessManager::notifyProcessAction(int pid, const std::string& action, bool success) {
    for (const auto& callback : action_callbacks_) {
        callback(pid, action, success);
    }
}

void ProcessManager::notifyResourceLimitExceeded(int pid, const std::string& resource, double usage, double limit) {
    for (const auto& callback : resource_callbacks_) {
        callback(pid, resource, usage, limit);
    }
}

void ProcessManager::notifySystemThresholdExceeded(const std::string& resource, double usage, double threshold) {
    for (const auto& callback : threshold_callbacks_) {
        callback(resource, usage, threshold);
    }
}

void ProcessManager::enableAutoManagement(bool enable) {
    auto_management_enabled_.store(enable);
}
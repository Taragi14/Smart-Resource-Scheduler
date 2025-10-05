#include "SystemMonitor.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <cstring>
#include <iostream>

SystemMonitor::SystemMonitor() 
    : monitoring_active_(false)
    , update_interval_(1000) // 1 second default
    , detailed_monitoring_(true) {
    
    // Initialize system stats
    std::memset(&system_stats_, 0, sizeof(SystemStats));
    system_stats_.cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
}

SystemMonitor::~SystemMonitor() {
    stopMonitoring();
}

bool SystemMonitor::startMonitoring() {
    if (monitoring_active_.load()) {
        return false; // Already running
    }
    
    monitoring_active_.store(true);
    monitor_thread_ = std::thread(&SystemMonitor::monitorLoop, this);
    return true;
}

void SystemMonitor::stopMonitoring() {
    if (monitoring_active_.load()) {
        monitoring_active_.store(false);
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
}

void SystemMonitor::setUpdateInterval(std::chrono::milliseconds interval) {
    update_interval_ = interval;
}

void SystemMonitor::setDetailedMonitoring(bool enable) {
    detailed_monitoring_ = enable;
}

void SystemMonitor::monitorLoop() {
    auto last_update = std::chrono::steady_clock::now();
    
    while (monitoring_active_.load()) {
        auto start = std::chrono::steady_clock::now();
        
        try {
            // Update process information
            auto new_processes = getProcessList();
            auto new_stats = getSystemStats();
            
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                processes_ = std::move(new_processes);
                system_stats_ = new_stats;
            }
            
            // Notify callbacks
            notifyProcessUpdate(processes_);
            notifySystemStatsUpdate(system_stats_);
            
        } catch (const std::exception& e) {
            std::cerr << "SystemMonitor error: " << e.what() << std::endl;
        }
        
        // Calculate sleep time
        auto end = std::chrono::steady_clock::now();
        auto processing_time = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        auto sleep_time = update_interval_ - processing_time;
        
        if (sleep_time > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

std::vector<ProcessInfo> SystemMonitor::getProcessList() {
    std::vector<ProcessInfo> processes;
    std::vector<int> pids = getRunningPids();
    
    for (int pid : pids) {
        try {
            ProcessInfo info = getProcessInfo(pid);
            if (info.pid != -1) {
                processes.push_back(info);
            }
        } catch (const std::exception&) {
            // Skip processes that can't be read (permission issues, etc.)
            continue;
        }
    }
    
    return processes;
}

std::vector<int> SystemMonitor::getRunningPids() {
    std::vector<int> pids;
    DIR* proc_dir = opendir("/proc");
    
    if (!proc_dir) {
        return pids;
    }
    
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            char* endptr;
            int pid = strtol(entry->d_name, &endptr, 10);
            
            if (*endptr == '\0' && pid > 0) {
                pids.push_back(pid);
            }
        }
    }
    
    closedir(proc_dir);
    return pids;
}

ProcessInfo SystemMonitor::getProcessInfo(int pid) {
    ProcessInfo info;
    info.pid = pid;
    
    // Read /proc/[pid]/stat
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::string stat_content = readFile(stat_path);
    
    if (stat_content.empty()) {
        info.pid = -1;
        return info;
    }
    
    // Parse stat file
    std::istringstream iss(stat_content);
    std::string token;
    std::vector<std::string> tokens;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    if (tokens.size() < 24) {
        info.pid = -1;
        return info;
    }
    
    // Extract information from stat tokens
    info.name = tokens[1].substr(1, tokens[1].length() - 2); // Remove parentheses
    info.state = tokens[2][0];
    info.parent_pid = std::stoi(tokens[3]);
    info.priority = std::stoi(tokens[17]);
    info.thread_count = std::stoi(tokens[19]);
    info.virtual_memory_kb = std::stoull(tokens[22]) / 1024; // Convert to KB
    info.resident_memory_kb = std::stoull(tokens[23]) * getpagesize() / 1024; // Pages to KB
    
    // CPU times (in clock ticks)
    info.cpu_time_user = std::stod(tokens[13]) / sysconf(_SC_CLK_TCK);
    info.cpu_time_system = std::stod(tokens[14]) / sysconf(_SC_CLK_TCK);
    
    // Read /proc/[pid]/status for more detailed memory info
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::string status_content = readFile(status_path);
    
    if (!status_content.empty()) {
        std::istringstream status_stream(status_content);
        std::string line;
        
        while (std::getline(status_stream, line)) {
            if (line.find("VmRSS:") == 0) {
                size_t pos = line.find_first_of("0123456789");
                if (pos != std::string::npos) {
                    info.memory_usage_kb = std::stoull(line.substr(pos));
                }
            }
        }
    }
    
    // Read command line
    std::string cmdline_path = "/proc/" + std::to_string(pid) + "/cmdline";
    std::string cmdline_content = readFile(cmdline_path);
    
    if (!cmdline_content.empty()) {
        // Replace null terminators with spaces
        std::replace(cmdline_content.begin(), cmdline_content.end(), '\0', ' ');
        info.command = cmdline_content;
    }
    
    info.start_time = std::chrono::system_clock::now(); // Simplified for now
    info.cpu_usage = 0.0; // Will be calculated by comparing with previous readings
    
    return info;
}

SystemStats SystemMonitor::getSystemStats() {
    SystemStats stats;
    std::memset(&stats, 0, sizeof(SystemStats));
    
    stats.timestamp = std::chrono::system_clock::now();
    stats.cpu_core_count = sysconf(_SC_NPROCESSORS_ONLN);
    
    // Read /proc/stat for CPU information
    std::string stat_content = readFile("/proc/stat");
    if (!stat_content.empty()) {
        std::istringstream iss(stat_content);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.find("cpu ") == 0) {
                // Parse total CPU stats
                std::istringstream cpu_stream(line);
                std::string cpu_label;
                long long user, nice, system, idle, iowait, irq, softirq, steal;
                
                cpu_stream >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
                
                long long total_idle = idle + iowait;
                long long total_non_idle = user + nice + system + irq + softirq + steal;
                long long total = total_idle + total_non_idle;
                
                if (total > 0) {
                    stats.cpu_usage_total = 100.0 * total_non_idle / total;
                }
                break;
            }
        }
    }
    
    // Read /proc/meminfo for memory information
    std::string meminfo_content = readFile("/proc/meminfo");
    if (!meminfo_content.empty()) {
        std::istringstream iss(meminfo_content);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.find("MemTotal:") == 0) {
                stats.memory_total_kb = parseMemoryValue(line);
            } else if (line.find("MemAvailable:") == 0) {
                stats.memory_available_kb = parseMemoryValue(line);
            } else if (line.find("Cached:") == 0) {
                stats.memory_cached_kb = parseMemoryValue(line);
            } else if (line.find("Buffers:") == 0) {
                stats.memory_buffered_kb = parseMemoryValue(line);
            }
        }
        
        stats.memory_used_kb = stats.memory_total_kb - stats.memory_available_kb;
    }
    
    // Read load averages
    std::string loadavg_content = readFile("/proc/loadavg");
    if (!loadavg_content.empty()) {
        std::istringstream iss(loadavg_content);
        iss >> stats.load_average_1min >> stats.load_average_5min >> stats.load_average_15min;
    }
    
    return stats;
}

std::string SystemMonitor::readFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

size_t SystemMonitor::parseMemoryValue(const std::string& meminfo_line) {
    size_t pos = meminfo_line.find_first_of("0123456789");
    if (pos != std::string::npos) {
        return std::stoull(meminfo_line.substr(pos));
    }
    return 0;
}

// Getter methods implementation
std::vector<ProcessInfo> SystemMonitor::getProcesses() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return processes_;
}

ProcessInfo SystemMonitor::getProcess(int pid) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto it = std::find_if(processes_.begin(), processes_.end(),
                           [pid](const ProcessInfo& p) { return p.pid == pid; });
    
    if (it != processes_.end()) {
        return *it;
    }
    
    ProcessInfo empty;
    empty.pid = -1;
    return empty;
}

SystemStats SystemMonitor::getSystemStatistics() const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return system_stats_;
}

std::vector<ProcessInfo> SystemMonitor::getProcessesByName(const std::string& name) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<ProcessInfo> result;
    
    std::copy_if(processes_.begin(), processes_.end(), std::back_inserter(result),
                 [&name](const ProcessInfo& p) { return p.name.find(name) != std::string::npos; });
    
    return result;
}

std::vector<ProcessInfo> SystemMonitor::getTopCpuProcesses(int count) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<ProcessInfo> sorted_processes = processes_;
    
    std::partial_sort(sorted_processes.begin(), 
                     sorted_processes.begin() + std::min(count, (int)sorted_processes.size()),
                     sorted_processes.end(),
                     [](const ProcessInfo& a, const ProcessInfo& b) {
                         return a.cpu_usage > b.cpu_usage;
                     });
    
    sorted_processes.resize(std::min(count, (int)sorted_processes.size()));
    return sorted_processes;
}

std::vector<ProcessInfo> SystemMonitor::getTopMemoryProcesses(int count) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::vector<ProcessInfo> sorted_processes = processes_;
    
    std::partial_sort(sorted_processes.begin(), 
                     sorted_processes.begin() + std::min(count, (int)sorted_processes.size()),
                     sorted_processes.end(),
                     [](const ProcessInfo& a, const ProcessInfo& b) {
                         return a.memory_usage_kb > b.memory_usage_kb;
                     });
    
    sorted_processes.resize(std::min(count, (int)sorted_processes.size()));
    return sorted_processes;
}

int SystemMonitor::getCpuCoreCount() const {
    return system_stats_.cpu_core_count;
}

size_t SystemMonitor::getTotalMemoryKB() const {
    return system_stats_.memory_total_kb;
}

double SystemMonitor::getSystemCpuUsage() const {
    return system_stats_.cpu_usage_total;
}

double SystemMonitor::getSystemMemoryUsage() const {
    if (system_stats_.memory_total_kb > 0) {
        return 100.0 * system_stats_.memory_used_kb / system_stats_.memory_total_kb;
    }
    return 0.0;
}

bool SystemMonitor::isProcessRunning(int pid) const {
    std::lock_guard<std::mutex> lock(data_mutex_);
    return std::any_of(processes_.begin(), processes_.end(),
                       [pid](const ProcessInfo& p) { return p.pid == pid; });
}

std::string SystemMonitor::getProcessName(int pid) const {
    ProcessInfo info = getProcess(pid);
    return (info.pid != -1) ? info.name : "";
}

// Callback methods
void SystemMonitor::registerProcessUpdateCallback(ProcessUpdateCallback callback) {
    process_callbacks_.push_back(callback);
}

void SystemMonitor::registerSystemStatsCallback(SystemStatsCallback callback) {
    stats_callbacks_.push_back(callback);
}

void SystemMonitor::notifyProcessUpdate(const std::vector<ProcessInfo>& processes) {
    for (const auto& callback : process_callbacks_) {
        callback(processes);
    }
}

void SystemMonitor::notifySystemStatsUpdate(const SystemStats& stats) {
    for (const auto& callback : stats_callbacks_) {
        callback(stats);
    }
}
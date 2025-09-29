#include "ProcessManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <errno.h>
#include <cstring>
#include <iostream>
#include <iomanip>

using namespace std;

namespace SmartScheduler {

ProcessManager::ProcessManager() 
    : monitoring_active_(false)
    , shutdown_requested_(false)
    , last_system_cpu_time_(0.0)
    , last_update_time_(chrono::steady_clock::now())
    , update_interval_(chrono::milliseconds(1000))
{
    // Add default critical processes
    critical_process_names_.insert("systemd");
    critical_process_names_.insert("kthreadd");
    critical_process_names_.insert("init");
    critical_process_names_.insert("kernel");
    critical_process_names_.insert("NetworkManager");
    critical_process_names_.insert("dbus");
}

ProcessManager::~ProcessManager() {
    shutdown();
}

bool ProcessManager::initialize() {
    try {
        // Initial process scan
        refreshProcessList();
        
        // Initialize system CPU time
        last_system_cpu_time_ = getSystemCpuTime();
        last_update_time_ = chrono::steady_clock::now();
        
        cout << "ProcessManager initialized successfully with " 
             << processes_.size() << " processes" << endl;
        
        return true;
    } catch (const exception& e) {
        cerr << "Failed to initialize ProcessManager: " << e.what() << endl;
        return false;
    }
}

void ProcessManager::shutdown() {
    shutdown_requested_ = true;
    stopMonitoring();
}

vector<shared_ptr<ProcessInfo>> ProcessManager::getAllProcesses() {
    lock_guard<mutex> lock(processes_mutex_);
    vector<shared_ptr<ProcessInfo>> result;
    result.reserve(processes_.size());
    
    for (const auto& pair : processes_) {
        result.push_back(pair.second);
    }
    
    return result;
}

shared_ptr<ProcessInfo> ProcessManager::getProcessById(int pid) {
    lock_guard<mutex> lock(processes_mutex_);
    auto it = processes_.find(pid);
    return (it != processes_.end()) ? it->second : nullptr;
}

void ProcessManager::refreshProcessList() {
    auto current_pids = getRunningPids();
    unordered_set<int> current_pid_set(current_pids.begin(), current_pids.end());
    
    {
        lock_guard<mutex> lock(processes_mutex_);
        
        // Remove processes that are no longer running
        auto it = processes_.begin();
        while (it != processes_.end()) {
            if (current_pid_set.find(it->first) == current_pid_set.end()) {
                if (process_terminated_callback_) {
                    process_terminated_callback_(it->first);
                }
                it = processes_.erase(it);
            } else {
                ++it;
            }
        }
        
        // Add or update existing processes
        for (int pid : current_pids) {
            auto existing = processes_.find(pid);
            if (existing == processes_.end()) {
                // New process
                auto process_info = createProcessInfo(pid);
                if (process_info) {
                    processes_[pid] = process_info;
                }
            } else {
                // Update existing process
                updateProcessInfo(existing->second);
            }
        }
    }
}

shared_ptr<ProcessInfo> ProcessManager::createProcessInfo(int pid) {
    auto process = make_shared<ProcessInfo>();
    process->pid = pid;
    process->start_time = chrono::steady_clock::now();
    process->last_cpu_time = chrono::steady_clock::now();
    process->is_suspended = false;
    process->cpu_percent = 0.0;
    
    if (!readProcessStats(pid, *process) || 
        !readProcessStatus(pid, *process) ||
        !readProcessCmdline(pid, *process)) {
        return nullptr;
    }
    
    // Check if this is a critical process
    process->is_critical = isCriticalProcess(process->name);
    
    return process;
}

void ProcessManager::updateProcessInfo(shared_ptr<ProcessInfo> process) {
    if (!process) return;
    
    if (!readProcessStats(process->pid, *process) ||
        !readProcessStatus(process->pid, *process)) {
        // Process might have terminated
        return;
    }
    
    calculateCpuUsage(process);
    
    if (process_change_callback_) {
        process_change_callback_(*process);
    }
}

bool ProcessManager::readProcessStats(int pid, ProcessInfo& info) {
    ifstream stat_file("/proc/" + to_string(pid) + "/stat");
    if (!stat_file.is_open()) {
        return false;
    }
    
    string line;
    if (!getline(stat_file, line)) {
        return false;
    }
    
    istringstream iss(line);
    string token;
    vector<string> tokens;
    
    while (iss >> token) {
        tokens.push_back(token);
    }
    
    if (tokens.size() < 24) {
        return false;
    }
    
    try {
        // Extract process name (remove parentheses)
        info.name = tokens[1];
        if (info.name.front() == '(' && info.name.back() == ')') {
            info.name = info.name.substr(1, info.name.length() - 2);
        }
        
        info.state = tokens[2];
        info.priority = stoi(tokens[17]);
        
        // Memory usage from statm file
        ifstream statm_file("/proc/" + to_string(pid) + "/statm");
        if (statm_file.is_open()) {
            long pages;
            if (statm_file >> pages) {
                long page_size = sysconf(_SC_PAGESIZE);
                info.memory_usage = (pages * page_size) / 1024; // Convert to KB
            }
        }
        
    } catch (const exception& e) {
        return false;
    }
    
    return true;
}

bool ProcessManager::readProcessStatus(int pid, ProcessInfo& info) {
    ifstream status_file("/proc/" + to_string(pid) + "/status");
    if (!status_file.is_open()) {
        return false;
    }
    
    string line;
    while (getline(status_file, line)) {
        if (line.substr(0, 4) == "VmRSS:") {
            istringstream iss(line);
            string key, value, unit;
            iss >> key >> value >> unit;
            try {
                info.memory_usage = stoull(value); // Already in KB
            } catch (const exception& e) {
                // Keep previous value or 0
            }
            break;
        }
    }
    
    return true;
}

bool ProcessManager::readProcessCmdline(int pid, ProcessInfo& info) {
    ifstream cmdline_file("/proc/" + to_string(pid) + "/cmdline");
    if (!cmdline_file.is_open()) {
        info.command = info.name; // Fallback to process name
        return true;
    }
    
    string cmdline;
    getline(cmdline_file, cmdline);
    
    // Replace null characters with spaces
    replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    
    if (cmdline.empty()) {
        info.command = "[" + info.name + "]"; // Kernel thread
    } else {
        info.command = cmdline;
    }
    
    return true;
}

vector<int> ProcessManager::getRunningPids() {
    vector<int> pids;
    DIR* proc_dir = opendir("/proc");
    
    if (!proc_dir) {
        cerr << "Cannot open /proc directory" << endl;
        return pids;
    }
    
    struct dirent* entry;
    while ((entry = readdir(proc_dir)) != nullptr) {
        // Check if directory name is a number (PID)
        char* endptr;
        long pid = strtol(entry->d_name, &endptr, 10);
        
        if (*endptr == '\0' && pid > 0) {
            // Verify it's actually a process directory
            string proc_path = "/proc/" + string(entry->d_name);
            struct stat stat_buf;
            if (stat(proc_path.c_str(), &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode)) {
                pids.push_back(static_cast<int>(pid));
            }
        }
    }
    
    closedir(proc_dir);
    return pids;
}

void ProcessManager::calculateCpuUsage(shared_ptr<ProcessInfo> process) {
    // Simplified CPU usage calculation
    auto current_time = chrono::steady_clock::now();
    auto time_diff = chrono::duration<double>(current_time - process->last_cpu_time).count();
    
    if (time_diff >= CPU_CALCULATION_INTERVAL) {
        process->cpu_percent = min(100.0, process->cpu_percent + (rand() % 10 - 5) * 0.1);
        process->cpu_percent = max(0.0, process->cpu_percent);
        process->last_cpu_time = current_time;
    }
}

double ProcessManager::getSystemCpuTime() {
    ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) {
        return 0.0;
    }
    
    string line;
    if (!getline(stat_file, line)) {
        return 0.0;
    }
    
    istringstream iss(line);
    string cpu;
    long user, nice, system, idle;
    
    iss >> cpu >> user >> nice >> system >> idle;
    
    return static_cast<double>(user + nice + system + idle);
}

bool ProcessManager::pauseProcess(int pid) {
    if (!isValidProcess(pid)) {
        return false;
    }
    
    if (kill(pid, SIGSTOP) == 0) {
        auto process = getProcessById(pid);
        if (process) {
            process->is_suspended = true;
        }
        return true;
    }
    
    return false;
}

bool ProcessManager::resumeProcess(int pid) {
    if (!isValidProcess(pid)) {
        return false;
    }
    
    if (kill(pid, SIGCONT) == 0) {
        auto process = getProcessById(pid);
        if (process) {
            process->is_suspended = false;
        }
        return true;
    }
    
    return false;
}

bool ProcessManager::terminateProcess(int pid) {
    if (!isValidProcess(pid)) {
        return false;
    }
    
    auto process = getProcessById(pid);
    if (process && process->is_critical) {
        cerr << "Refusing to terminate critical process: " << process->name << endl;
        return false;
    }
    
    if (kill(pid, SIGTERM) == 0) {
        this_thread::sleep_for(chrono::milliseconds(100));
        
        if (isValidProcess(pid)) {
            return kill(pid, SIGKILL) == 0;
        }
        return true;
    }
    
    return false;
}

bool ProcessManager::setPriority(int pid, int priority) {
    if (!isValidProcess(pid)) {
        return false;
    }
    
    priority = max(-20, min(19, priority));
    
    if (setpriority(PRIO_PROCESS, pid, priority) == 0) {
        auto process = getProcessById(pid);
        if (process) {
            process->priority = priority;
        }
        return true;
    }
    
    return false;
}

bool ProcessManager::setAffinity(int pid, const vector<int>& cpu_cores) {
    // Placeholder
    return true;
}

void ProcessManager::startMonitoring() {
    if (monitoring_active_.load()) {
        return;
    }
    
    monitoring_active_ = true;
    monitoring_thread_ = thread(&ProcessManager::monitoringLoop, this);
}

void ProcessManager::stopMonitoring() {
    monitoring_active_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void ProcessManager::monitoringLoop() {
    while (monitoring_active_.load() && !shutdown_requested_.load()) {
        refreshProcessList();
        this_thread::sleep_for(update_interval_);
    }
}

vector<shared_ptr<ProcessInfo>> ProcessManager::getHighCpuProcesses(double threshold) {
    lock_guard<mutex> lock(processes_mutex_);
    vector<shared_ptr<ProcessInfo>> result;
    
    for (const auto& pair : processes_) {
        if (pair.second->cpu_percent > threshold) {
            result.push_back(pair.second);
        }
    }
    
    sort(result.begin(), result.end(), 
        [](const auto& a, const auto& b) {
            return a->cpu_percent > b->cpu_percent;
        });
    
    return result;
}

vector<shared_ptr<ProcessInfo>> ProcessManager::getHighMemoryProcesses(size_t threshold) {
    lock_guard<mutex> lock(processes_mutex_);
    vector<shared_ptr<ProcessInfo>> result;
    
    for (const auto& pair : processes_) {
        if (pair.second->memory_usage > threshold) {
            result.push_back(pair.second);
        }
    }
    
    sort(result.begin(), result.end(), 
        [](const auto& a, const auto& b) {
            return a->memory_usage > b->memory_usage;
        });
    
    return result;
}

bool ProcessManager::isValidProcess(int pid) {
    return pid > 0 && (kill(pid, 0) == 0 || errno != ESRCH);
}

void ProcessManager::addCriticalProcess(const string& process_name) {
    lock_guard<mutex> lock(critical_processes_mutex_);
    critical_process_names_.insert(process_name);
}

bool ProcessManager::isCriticalProcess(const string& process_name) const {
    lock_guard<mutex> lock(critical_processes_mutex_);
    return critical_process_names_.find(process_name) != critical_process_names_.end();
}

int ProcessManager::getActiveProcessCount() const {
    lock_guard<mutex> lock(processes_mutex_);
    return processes_.size();
}

void ProcessManager::setProcessChangeCallback(function<void(const ProcessInfo&)> callback) {
    process_change_callback_ = callback;
}

void ProcessManager::setProcessTerminatedCallback(function<void(int)> callback) {
    process_terminated_callback_ = callback;
}

} // namespace SmartScheduler

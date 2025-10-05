#include "MemoryManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <sys/sysinfo.h>
#include <unistd.h>

MemoryManager::MemoryManager(std::shared_ptr<SystemMonitor> monitor,
                             std::shared_ptr<ProcessManager> process_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , monitoring_active_(false)
    , monitoring_interval_(std::chrono::milliseconds(2000))
    , optimization_strategy_(MemoryOptimizationStrategy::BALANCED)
    , auto_optimization_enabled_(false)
    , swap_management_enabled_(true)
    , cache_management_enabled_(true)
    , low_memory_threshold_(70.0)
    , critical_memory_threshold_(90.0)
    , max_process_memory_kb_(4 * 1024 * 1024) // 4GB default
    , memory_warning_threshold_kb_(2 * 1024 * 1024) // 2GB
    , enable_memory_compression_(false)
    , enable_proactive_swapping_(false)
    , enable_cache_trimming_(true)
    , minimum_free_memory_kb_(512 * 1024) // 512MB
    , total_memory_freed_kb_(0)
    , total_processes_optimized_(0)
    , total_cache_cleared_kb_(0)
    , total_swap_operations_(0) {
    
    // Initialize system memory info
    std::memset(&system_memory_info_, 0, sizeof(SystemMemoryInfo));
    system_memory_info_.page_size_bytes = getpagesize();
}

MemoryManager::~MemoryManager() {
    stopMonitoring();
}

bool MemoryManager::startMonitoring() {
    if (monitoring_active_.load()) {
        return false;
    }
    
    monitoring_active_.store(true);
    monitor_thread_ = std::thread(&MemoryManager::monitoringLoop, this);
    
    return true;
}

void MemoryManager::stopMonitoring() {
    if (monitoring_active_.load()) {
        monitoring_active_.store(false);
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
}

void MemoryManager::monitoringLoop() {
    while (monitoring_active_.load()) {
        try {
            // Gather system memory information
            auto new_sys_info = gatherSystemMemoryInfo();
            
            {
                std::lock_guard<std::mutex> lock(memory_info_mutex_);
                system_memory_info_ = new_sys_info;
            }
            
            // Gather process memory information
            auto all_processes = system_monitor_->getProcesses();
            for (const auto& proc : all_processes) {
                try {
                    auto proc_mem_info = gatherProcessMemoryInfo(proc.pid);
                    
                    std::lock_guard<std::mutex> lock(memory_info_mutex_);
                    process_memory_info_[proc.pid] = proc_mem_info;
                    
                    // Check for memory limit violations
                    if (proc_mem_info.resident_memory_kb > max_process_memory_kb_) {
                        notifyProcessMemoryChange(proc.pid, 
                                                 proc_mem_info.resident_memory_kb, 
                                                 true);
                    }
                    
                } catch (const std::exception&) {
                    // Skip processes we can't read
                    continue;
                }
            }
            
            // Check memory pressure and act if needed
            auto pressure = calculateMemoryPressure();
            
            if (pressure != MemoryPressureLevel::LOW) {
                double usage = getMemoryUsagePercent();
                notifyMemoryPressure(pressure, usage);
                
                if (auto_optimization_enabled_.load()) {
                    handleMemoryPressure(pressure);
                }
            }
            
            // Clean up dead process entries
            {
                std::lock_guard<std::mutex> lock(memory_info_mutex_);
                auto it = process_memory_info_.begin();
                while (it != process_memory_info_.end()) {
                    if (!system_monitor_->isProcessRunning(it->first)) {
                        it = process_memory_info_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "MemoryManager error: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(monitoring_interval_);
    }
}

SystemMemoryInfo MemoryManager::gatherSystemMemoryInfo() {
    SystemMemoryInfo info;
    std::memset(&info, 0, sizeof(SystemMemoryInfo));
    
    info.timestamp = std::chrono::system_clock::now();
    info.page_size_bytes = getpagesize();
    
    // Read /proc/meminfo
    std::string meminfo_content = readProcFile("/proc/meminfo");
    if (!meminfo_content.empty()) {
        std::istringstream iss(meminfo_content);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.find("MemTotal:") == 0) {
                info.total_memory_kb = parseMemoryValue(line);
            } else if (line.find("Dirty:") == 0) {
                info.dirty_pages = parseMemoryValue(line) / (info.page_size_bytes / 1024);
            } else if (line.find("Writeback:") == 0) {
                info.writeback_pages = parseMemoryValue(line) / (info.page_size_bytes / 1024);
            }
        }
    }
    
    // Calculate derived values
    info.used_memory_kb = info.total_memory_kb - info.available_memory_kb;
    info.used_swap_kb = info.total_swap_kb - info.free_swap_kb;
    info.total_pages = info.total_memory_kb / (info.page_size_bytes / 1024);
    info.free_pages = info.free_memory_kb / (info.page_size_bytes / 1024);
    info.cached_pages = info.cached_memory_kb / (info.page_size_bytes / 1024);
    
    // Calculate memory pressure
    info.pressure_level = calculateMemoryPressure();
    if (info.total_memory_kb > 0) {
        info.pressure_ratio = static_cast<double>(info.used_memory_kb) / info.total_memory_kb;
    }
    
    return info;
}

ProcessMemoryInfo MemoryManager::gatherProcessMemoryInfo(int pid) {
    ProcessMemoryInfo info;
    info.pid = pid;
    info.name = system_monitor_->getProcessName(pid);
    info.last_updated = std::chrono::system_clock::now();
    
    // Read /proc/[pid]/status
    std::string status_path = "/proc/" + std::to_string(pid) + "/status";
    std::string status_content = readProcFile(status_path);
    
    if (!status_content.empty()) {
        std::istringstream iss(status_content);
        std::string line;
        
        while (std::getline(iss, line)) {
            if (line.find("VmSize:") == 0) {
                info.virtual_memory_kb = parseMemoryValue(line);
            } else if (line.find("VmRSS:") == 0) {
                info.resident_memory_kb = parseMemoryValue(line);
            } else if (line.find("RssAnon:") == 0) {
                info.private_memory_kb = parseMemoryValue(line);
            } else if (line.find("RssShmem:") == 0) {
                info.shared_memory_kb = parseMemoryValue(line);
            } else if (line.find("VmData:") == 0) {
                info.data_memory_kb = parseMemoryValue(line);
            } else if (line.find("VmStk:") == 0) {
                info.stack_memory_kb = parseMemoryValue(line);
            } else if (line.find("VmExe:") == 0) {
                info.code_memory_kb = parseMemoryValue(line);
            } else if (line.find("VmPeak:") == 0) {
                size_t peak = parseMemoryValue(line);
                if (peak > info.peak_memory_kb) {
                    info.peak_memory_kb = peak;
                    info.peak_memory_time = std::chrono::system_clock::now();
                }
            }
        }
    }
    
    // Read /proc/[pid]/stat for page faults
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::string stat_content = readProcFile(stat_path);
    
    if (!stat_content.empty()) {
        std::istringstream iss(stat_content);
        std::vector<std::string> tokens;
        std::string token;
        
        while (iss >> token) {
            tokens.push_back(token);
        }
        
        if (tokens.size() >= 12) {
            info.page_faults_minor = std::stoull(tokens[9]);
            info.page_faults_major = std::stoull(tokens[11]);
        }
    }
    
    // Estimate heap usage (approximate)
    info.heap_memory_kb = info.data_memory_kb;
    
    // Calculate memory growth rate
    {
        std::lock_guard<std::mutex> lock(memory_info_mutex_);
        auto prev_it = process_memory_info_.find(pid);
        if (prev_it != process_memory_info_.end()) {
            auto time_diff = std::chrono::duration_cast<std::chrono::seconds>(
                info.last_updated - prev_it->second.last_updated);
            
            if (time_diff.count() > 0) {
                size_t memory_diff = info.resident_memory_kb - prev_it->second.resident_memory_kb;
                info.memory_growth_rate_kb_per_sec = 
                    static_cast<double>(memory_diff) / time_diff.count();
            }
        }
    }
    
    return info;
}

std::vector<MemoryRegion> MemoryManager::parseMemoryMaps(int pid) {
    std::vector<MemoryRegion> regions;
    
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::string maps_content = readProcFile(maps_path);
    
    if (maps_content.empty()) {
        return regions;
    }
    
    std::istringstream iss(maps_content);
    std::string line;
    
    while (std::getline(iss, line)) {
        MemoryRegion region;
        
        // Parse line format: address perms offset dev inode pathname
        std::istringstream line_stream(line);
        std::string address_range, perms, offset, dev, inode;
        
        line_stream >> address_range >> perms >> offset >> dev >> inode;
        
        // Parse address range
        size_t dash_pos = address_range.find('-');
        if (dash_pos != std::string::npos) {
            region.start_address = std::stoull(address_range.substr(0, dash_pos), nullptr, 16);
            uintptr_t end_address = std::stoull(address_range.substr(dash_pos + 1), nullptr, 16);
            region.size_bytes = end_address - region.start_address;
        }
        
        region.permissions = perms;
        region.is_shared = (perms.find('s') != std::string::npos);
        region.is_swappable = true; // Most regions are swappable
        
        // Get pathname if exists
        std::string pathname;
        std::getline(line_stream, pathname);
        if (!pathname.empty()) {
            pathname = pathname.substr(pathname.find_first_not_of(" \t"));
            region.file_path = pathname;
            
            // Determine mapping type
            if (pathname.find("[heap]") != std::string::npos) {
                region.mapping_type = "heap";
            } else if (pathname.find("[stack]") != std::string::npos) {
                region.mapping_type = "stack";
            } else if (pathname.find(".so") != std::string::npos) {
                region.mapping_type = "shared";
            } else if (perms[2] == 'x') {
                region.mapping_type = "code";
            } else {
                region.mapping_type = "data";
            }
        } else {
            region.mapping_type = "anonymous";
        }
        
        regions.push_back(region);
    }
    
    return regions;
}

MemoryPressureLevel MemoryManager::calculateMemoryPressure() const {
    double usage_percent = getMemoryUsagePercent();
    
    if (usage_percent >= critical_memory_threshold_) {
        return MemoryPressureLevel::CRITICAL;
    } else if (usage_percent >= (low_memory_threshold_ + critical_memory_threshold_) / 2) {
        return MemoryPressureLevel::HIGH;
    } else if (usage_percent >= low_memory_threshold_) {
        return MemoryPressureLevel::MEDIUM;
    }
    
    return MemoryPressureLevel::LOW;
}

double MemoryManager::calculateFragmentation() const {
    // Simplified fragmentation calculation
    // Real implementation would analyze memory regions
    size_t total = system_memory_info_.total_memory_kb;
    size_t available = system_memory_info_.available_memory_kb;
    size_t free = system_memory_info_.free_memory_kb;
    
    if (total == 0) return 0.0;
    
    // If available is much less than free, we have fragmentation
    double fragmentation = 1.0 - (static_cast<double>(available) / (free + 1));
    return std::max(0.0, std::min(1.0, fragmentation));
}

std::vector<int> MemoryManager::identifyMemoryHogs(int count) const {
    std::vector<std::pair<int, size_t>> pid_memory_pairs;
    
    {
        std::lock_guard<std::mutex> lock(memory_info_mutex_);
        for (const auto& [pid, mem_info] : process_memory_info_) {
            pid_memory_pairs.push_back({pid, mem_info.resident_memory_kb});
        }
    }
    
    std::partial_sort(pid_memory_pairs.begin(),
                     pid_memory_pairs.begin() + std::min(count, (int)pid_memory_pairs.size()),
                     pid_memory_pairs.end(),
                     [](const auto& a, const auto& b) {
                         return a.second > b.second;
                     });
    
    std::vector<int> result;
    for (int i = 0; i < std::min(count, (int)pid_memory_pairs.size()); ++i) {
        result.push_back(pid_memory_pairs[i].first);
    }
    
    return result;
}

void MemoryManager::handleMemoryPressure(MemoryPressureLevel level) {
    switch (level) {
        case MemoryPressureLevel::MEDIUM:
            handleMediumMemoryPressure();
            break;
        
        case MemoryPressureLevel::HIGH:
            handleHighMemoryPressure();
            break;
        
        case MemoryPressureLevel::CRITICAL:
            handleCriticalMemoryPressure();
            break;
        
        default:
            break;
    }
}

void MemoryManager::handleMediumMemoryPressure() {
    // Clear caches if enabled
    if (cache_management_enabled_.load() && enable_cache_trimming_) {
        size_t freed = clearPageCache();
        total_cache_cleared_kb_ += freed;
        notifyOptimizationComplete("clear_page_cache", freed);
    }
}

void MemoryManager::handleHighMemoryPressure() {
    // Clear all caches
    if (cache_management_enabled_.load()) {
        size_t freed = clearAllCaches();
        total_cache_cleared_kb_ += freed;
        notifyOptimizationComplete("clear_all_caches", freed);
    }
    
    // Optimize memory-heavy processes
    auto memory_hogs = identifyMemoryHogs(5);
    for (int pid : memory_hogs) {
        optimizeProcessMemory(pid);
    }
}

void MemoryManager::handleCriticalMemoryPressure() {
    std::cout << "CRITICAL MEMORY PRESSURE - Taking emergency action!" << std::endl;
    
    // Clear all caches
    clearAllCaches();
    
    // Try to free memory by killing non-critical processes
    size_t target_free = minimum_free_memory_kb_;
    size_t current_free = system_memory_info_.available_memory_kb;
    
    if (current_free < target_free) {
        size_t needed = target_free - current_free;
        emergencyMemoryCleanup();
        killMemoryHoggingProcesses(needed);
    }
}

size_t MemoryManager::optimizeSystemMemory() {
    size_t total_freed = 0;
    
    switch (optimization_strategy_) {
        case MemoryOptimizationStrategy::CONSERVATIVE:
            applyConservativeStrategy();
            break;
        
        case MemoryOptimizationStrategy::BALANCED:
            applyBalancedStrategy();
            break;
        
        case MemoryOptimizationStrategy::AGGRESSIVE:
            applyAggressiveStrategy();
            break;
        
        default:
            break;
    }
    
    return total_freed;
}

size_t MemoryManager::optimizeProcessMemory(int pid) {
    if (!system_monitor_->isProcessRunning(pid)) {
        return 0;
    }
    
    size_t freed = 0;
    
    // Lower process priority to reduce memory pressure
    process_manager_->setProcessPriority(pid, ProcessPriority::LOW);
    
    total_processes_optimized_++;
    
    return freed;
}

size_t MemoryManager::clearPageCache() {
    size_t before = system_memory_info_.cached_memory_kb;
    
    if (dropCaches(1)) {
        // Re-read memory info
        auto new_info = gatherSystemMemoryInfo();
        size_t freed = before - new_info.cached_memory_kb;
        return freed;
    }
    
    return 0;
}

size_t MemoryManager::clearAllCaches() {
    size_t before_cached = system_memory_info_.cached_memory_kb;
    size_t before_buffered = system_memory_info_.buffered_memory_kb;
    
    // Drop all caches (pagecache, dentries, inodes)
    if (dropCaches(3)) {
        auto new_info = gatherSystemMemoryInfo();
        size_t freed = (before_cached + before_buffered) - 
                      (new_info.cached_memory_kb + new_info.buffered_memory_kb);
        return freed;
    }
    
    return 0;
}

bool MemoryManager::dropCaches(int cache_type) {
    if (!hasRootPrivileges()) {
        std::cerr << "Insufficient privileges to drop caches" << std::endl;
        return false;
    }
    
    std::ofstream drop_caches("/proc/sys/vm/drop_caches");
    if (drop_caches.is_open()) {
        drop_caches << cache_type;
        drop_caches.close();
        return true;
    }
    
    return false;
}

void MemoryManager::emergencyMemoryCleanup() {
    std::cout << "Performing emergency memory cleanup..." << std::endl;
    
    // Clear all caches
    clearAllCaches();
    
    // Compact memory
    compactSystemMemory();
    
    // Force swap out non-critical processes
    if (swap_management_enabled_.load()) {
        auto all_processes = system_monitor_->getProcesses();
        for (const auto& proc : all_processes) {
            if (proc.memory_usage_kb > memory_warning_threshold_kb_) {
                swapOutProcess(proc.pid);
            }
        }
    }
}

size_t MemoryManager::killMemoryHoggingProcesses(size_t target_free_kb) {
    auto memory_hogs = identifyMemoryHogs(10);
    size_t freed = 0;
    
    for (int pid : memory_hogs) {
        if (freed >= target_free_kb) {
            break;
        }
        
        auto mem_info = getProcessMemoryInfo(pid);
        
        // Don't kill critical processes
        if (!process_manager_->canModifyProcess(pid)) {
            continue;
        }
        
        std::cout << "Emergency killing process: " << mem_info.name 
                 << " (PID: " << pid << ", Memory: " << mem_info.resident_memory_kb << " KB)" 
                 << std::endl;
        
        if (process_manager_->terminateProcess(pid)) {
            freed += mem_info.resident_memory_kb;
        }
    }
    
    return freed;
}

bool MemoryManager::compactSystemMemory() {
    if (!hasRootPrivileges()) {
        return false;
    }
    
    std::ofstream compact("/proc/sys/vm/compact_memory");
    if (compact.is_open()) {
        compact << "1";
        compact.close();
        return true;
    }
    
    return false;
}

void MemoryManager::applyConservativeStrategy() {
    // Only clear page cache when really needed
    if (getMemoryUsagePercent() > 85.0) {
        clearPageCache();
    }
}

void MemoryManager::applyBalancedStrategy() {
    // Clear caches and optimize some processes
    if (getMemoryUsagePercent() > 75.0) {
        clearPageCache();
        
        auto memory_hogs = identifyMemoryHogs(3);
        for (int pid : memory_hogs) {
            optimizeProcessMemory(pid);
        }
    }
}

void MemoryManager::applyAggressiveStrategy() {
    // Aggressively free memory
    clearAllCaches();
    compactSystemMemory();
    
    auto memory_hogs = identifyMemoryHogs(5);
    for (int pid : memory_hogs) {
        optimizeProcessMemory(pid);
    }
}

// Getter methods
ProcessMemoryInfo MemoryManager::getProcessMemoryInfo(int pid) const {
    std::lock_guard<std::mutex> lock(memory_info_mutex_);
    
    auto it = process_memory_info_.find(pid);
    if (it != process_memory_info_.end()) {
        return it->second;
    }
    
    return ProcessMemoryInfo();
}

SystemMemoryInfo MemoryManager::getSystemMemoryInfo() const {
    std::lock_guard<std::mutex> lock(memory_info_mutex_);
    return system_memory_info_;
}

size_t MemoryManager::getTotalMemoryKB() const {
    return system_memory_info_.total_memory_kb;
}

size_t MemoryManager::getAvailableMemoryKB() const {
    return system_memory_info_.available_memory_kb;
}

size_t MemoryManager::getUsedMemoryKB() const {
    return system_memory_info_.used_memory_kb;
}

double MemoryManager::getMemoryUsagePercent() const {
    if (system_memory_info_.total_memory_kb > 0) {
        return 100.0 * system_memory_info_.used_memory_kb / 
               system_memory_info_.total_memory_kb;
    }
    return 0.0;
}

MemoryPressureLevel MemoryManager::getCurrentMemoryPressure() const {
    return system_memory_info_.pressure_level;
}

// Notification methods
void MemoryManager::notifyMemoryPressure(MemoryPressureLevel level, double usage_percent) {
    for (const auto& callback : pressure_callbacks_) {
        callback(level, usage_percent);
    }
}

void MemoryManager::notifyProcessMemoryChange(int pid, size_t memory_kb, bool exceeded_limit) {
    for (const auto& callback : process_callbacks_) {
        callback(pid, memory_kb, exceeded_limit);
    }
}

void MemoryManager::notifyOptimizationComplete(const std::string& action, size_t freed_kb) {
    for (const auto& callback : optimization_callbacks_) {
        callback(action, freed_kb);
    }
}

// Callback registration
void MemoryManager::registerMemoryPressureCallback(MemoryPressureCallback callback) {
    pressure_callbacks_.push_back(callback);
}

void MemoryManager::registerProcessMemoryCallback(ProcessMemoryCallback callback) {
    process_callbacks_.push_back(callback);
}

void MemoryManager::registerOptimizationCallback(OptimizationCallback callback) {
    optimization_callbacks_.push_back(callback);
}

// Configuration methods
void MemoryManager::setOptimizationStrategy(MemoryOptimizationStrategy strategy) {
    optimization_strategy_ = strategy;
}

void MemoryManager::enableAutoOptimization(bool enable) {
    auto_optimization_enabled_.store(enable);
}

// Helper methods
std::string MemoryManager::readProcFile(const std::string& filepath) const {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return "";
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    return content;
}

size_t MemoryManager::parseMemoryValue(const std::string& line) const {
    size_t pos = line.find_first_of("0123456789");
    if (pos != std::string::npos) {
        std::string number_str;
        while (pos < line.length() && std::isdigit(line[pos])) {
            number_str += line[pos];
            pos++;
        }
        return std::stoull(number_str);
    }
    return 0;
}

bool MemoryManager::hasRootPrivileges() const {
    return geteuid() == 0;
}

bool MemoryManager::canAccessProcess(int pid) const {
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream file(stat_path);
    return file.good();
}

size_t MemoryManager::swapOutProcess(int pid) {
    // This is a placeholder - actual implementation would use madvise or similar
    total_swap_operations_++;
    return 0;
}

std::vector<int> MemoryManager::getTopMemoryProcesses(int count) const {
    return identifyMemoryHogs(count);
}line.find("MemFree:") == 0) {
                info.free_memory_kb = parseMemoryValue(line);
            } else if (line.find("MemAvailable:") == 0) {
                info.available_memory_kb = parseMemoryValue(line);
            } else if (line.find("Cached:") == 0) {
                info.cached_memory_kb = parseMemoryValue(line);
            } else if (line.find("Buffers:") == 0) {
                info.buffered_memory_kb = parseMemoryValue(line);
            } else if (line.find("SwapTotal:") == 0) {
                info.total_swap_kb = parseMemoryValue(line);
            } else if (line.find("SwapFree:") == 0) {
                info.free_swap_kb = parseMemoryValue(line);
            } else if (
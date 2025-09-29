#include "SystemMonitor.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <unistd.h>
#include <sys/sysinfo.h>

namespace SmartScheduler {

SystemMonitor::SystemMonitor()
    : monitoring_active_(false)
    , shutdown_requested_(false)
    , update_interval_(std::chrono::milliseconds(DEFAULT_UPDATE_INTERVAL_MS))
    , history_duration_(std::chrono::minutes(DEFAULT_HISTORY_DURATION_MIN))
    , cpu_threshold_(DEFAULT_CPU_THRESHOLD)
    , memory_threshold_(DEFAULT_MEMORY_THRESHOLD)
    , load_threshold_(DEFAULT_LOAD_THRESHOLD)
    , cpu_times_initialized_(false)
    , cpu_core_count_(0)
    , high_cpu_alerted_(false)
    , high_memory_alerted_(false)
    , system_overload_alerted_(false)
{
    current_stats_ = {};
    current_stats_.timestamp = std::chrono::steady_clock::now();
}

SystemMonitor::~SystemMonitor() {
    shutdown();
}

bool SystemMonitor::initialize() {
    try {
        // CPU core count
        cpu_core_count_ = std::thread::hardware_concurrency();
        if (cpu_core_count_ == 0) {
            cpu_core_count_ = sysconf(_SC_NPROCESSORS_ONLN);
        }

        // Read CPU/system info
        readCpuInfo();

        // Init CPU timing for usage calculation
        if (!readStat()) {
            std::cerr << "Warning: Failed to initialize CPU timing data\n";
        }

        // Initial stats
        current_stats_ = collectSystemStats();
        std::cout << "SystemMonitor initialized successfully\n";
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "SystemMonitor init failed: " << e.what() << "\n";
        return false;
    }
}

void SystemMonitor::shutdown() {
    shutdown_requested_.store(true);
    stopMonitoring();
}

void SystemMonitor::startMonitoring() {
    if (monitoring_active_) return;

    monitoring_active_ = true;
    shutdown_requested_ = false;

    monitoring_thread_ = std::thread(&SystemMonitor::monitoringLoop, this);
}

void SystemMonitor::stopMonitoring() {
    monitoring_active_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

SystemStats SystemMonitor::getCurrentStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_;
}

double SystemMonitor::getCurrentCpuUsage() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_.cpu_usage;
}

double SystemMonitor::getCurrentMemoryUsage() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_.memory_percent;
}

size_t SystemMonitor::getAvailableMemory() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_.available_memory;
}

size_t SystemMonitor::getTotalMemory() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_.total_memory;
}

std::vector<HistoricalDataPoint> SystemMonitor::getHistoricalData(std::chrono::minutes duration) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    std::vector<HistoricalDataPoint> result;
    auto cutoff = std::chrono::steady_clock::now() - duration;
    for (auto& p : historical_data_) {
        if (p.timestamp >= cutoff) result.push_back(p);
    }
    return result;
}

void SystemMonitor::clearHistoricalData() {
    std::lock_guard<std::mutex> lock(history_mutex_);
    historical_data_.clear();
}

bool SystemMonitor::isSystemUnderHighLoad() const {
    return current_stats_.load_average_1min > load_threshold_;
}

bool SystemMonitor::isMemoryPressure() const {
    return current_stats_.memory_percent > memory_threshold_;
}

bool SystemMonitor::isCpuPressure() const {
    return current_stats_.cpu_usage > cpu_threshold_;
}

void SystemMonitor::setCpuThreshold(double threshold) { cpu_threshold_ = threshold; }
void SystemMonitor::setMemoryThreshold(double threshold) { memory_threshold_ = threshold; }
void SystemMonitor::setLoadThreshold(double threshold) { load_threshold_ = threshold; }

void SystemMonitor::setSystemStatsCallback(std::function<void(const SystemStats&)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    stats_callback_ = callback;
}

void SystemMonitor::setHighCpuCallback(std::function<void(double)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    high_cpu_callback_ = callback;
}

void SystemMonitor::setHighMemoryCallback(std::function<void(double)> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    high_memory_callback_ = callback;
}

void SystemMonitor::setSystemOverloadCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    system_overload_callback_ = callback;
}

void SystemMonitor::setUpdateInterval(std::chrono::milliseconds interval) {
    update_interval_ = interval;
}

void SystemMonitor::setHistoryDuration(std::chrono::minutes duration) {
    history_duration_ = duration;
}

// ---------- Monitoring Loop ----------
void SystemMonitor::monitoringLoop() {
    while (monitoring_active_ && !shutdown_requested_) {
        SystemStats stats = collectSystemStats();

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            current_stats_ = stats;
        }

        addHistoricalDataPoint(stats);
        checkThresholds(stats);

        {
            std::lock_guard<std::mutex> lock(callbacks_mutex_);
            if (stats_callback_) stats_callback_(stats);
        }

        std::this_thread::sleep_for(update_interval_);
    }
}

// ---------- Data Collection ----------
SystemStats SystemMonitor::collectSystemStats() {
    SystemStats stats{};
    stats.timestamp = std::chrono::steady_clock::now();

    stats.cpu_usage = calculateCpuUsage();
    updateMemoryStats(stats);
    updateLoadAverages(stats);

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        stats.active_processes = info.procs;
    }
    return stats;
}

double SystemMonitor::calculateCpuUsage() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0.0;

    std::string line;
    std::getline(file, line);
    std::istringstream ss(line);

    std::string cpu;
    CpuTimes times;
    ss >> cpu >> times.user >> times.nice >> times.system >> times.idle
       >> times.iowait >> times.irq >> times.softirq >> times.steal;

    if (!cpu_times_initialized_) {
        last_cpu_times_ = times;
        cpu_times_initialized_ = true;
        return 0.0;
    }

    long long prev_total = last_cpu_times_.total();
    long long prev_active = last_cpu_times_.active();

    long long total = times.total();
    long long active = times.active();

    long long total_delta = total - prev_total;
    long long active_delta = active - prev_active;

    last_cpu_times_ = times;
    if (total_delta == 0) return 0.0;

    return 100.0 * active_delta / total_delta;
}

void SystemMonitor::updateMemoryStats(SystemStats& stats) {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        stats.total_memory = info.totalram / 1024;
        stats.available_memory = info.freeram / 1024;
        stats.used_memory = stats.total_memory - stats.available_memory;
        stats.memory_percent = (stats.total_memory > 0)
            ? (100.0 * stats.used_memory / stats.total_memory) : 0.0;
    }
}

void SystemMonitor::updateLoadAverages(SystemStats& stats) {
    std::ifstream file("/proc/loadavg");
    if (!file.is_open()) return;

    file >> stats.load_average_1min
         >> stats.load_average_5min
         >> stats.load_average_15min;
}

// ---------- Historical Data ----------
void SystemMonitor::addHistoricalDataPoint(const SystemStats& stats) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    historical_data_.push_back({stats.timestamp, stats.cpu_usage, stats.memory_percent, stats.load_average_1min});
    pruneHistoricalData();
}

void SystemMonitor::pruneHistoricalData() {
    auto cutoff = std::chrono::steady_clock::now() - history_duration_;
    while (!historical_data_.empty() && historical_data_.front().timestamp < cutoff) {
        historical_data_.pop_front();
    }
    if (historical_data_.size() > MAX_HISTORY_POINTS) {
        historical_data_.pop_front();
    }
}

// ---------- Thresholds & Alerts ----------
void SystemMonitor::checkThresholds(const SystemStats& stats) {
    if (stats.cpu_usage > cpu_threshold_ && !high_cpu_alerted_) {
        if (high_cpu_callback_) high_cpu_callback_(stats.cpu_usage);
        high_cpu_alerted_ = true;
    } else if (stats.cpu_usage <= cpu_threshold_) {
        high_cpu_alerted_ = false;
    }

    if (stats.memory_percent > memory_threshold_ && !high_memory_alerted_) {
        if (high_memory_callback_) high_memory_callback_(stats.memory_percent);
        high_memory_alerted_ = true;
    } else if (stats.memory_percent <= memory_threshold_) {
        high_memory_alerted_ = false;
    }

    if ((stats.cpu_usage > cpu_threshold_ && stats.memory_percent > memory_threshold_)
        && !system_overload_alerted_) {
        if (system_overload_callback_) system_overload_callback_();
        system_overload_alerted_ = true;
    } else if (!(stats.cpu_usage > cpu_threshold_ && stats.memory_percent > memory_threshold_)) {
        system_overload_alerted_ = false;
    }
}

// ---------- System Info Helpers ----------
bool SystemMonitor::readCpuInfo() {
    std::ifstream file("/proc/cpuinfo");
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("model name") != std::string::npos) {
            cpu_model_ = line.substr(line.find(":") + 2);
            break;
        }
    }

    std::ifstream versionFile("/proc/version");
    if (versionFile.is_open()) {
        std::getline(versionFile, kernel_version_);
    }

    std::ifstream osFile("/etc/os-release");
    if (osFile.is_open()) {
        while (std::getline(osFile, line)) {
            if (line.find("PRETTY_NAME=") == 0) {
                distribution_ = line.substr(13);
                distribution_.erase(remove(distribution_.begin(), distribution_.end(), '\"'), distribution_.end());
                break;
            }
        }
    }
    return true;
}

bool SystemMonitor::readStat() {
    std::ifstream file("/proc/stat");
    return file.is_open();
}

int SystemMonitor::getCpuCoreCount() const { return cpu_core_count_; }
std::string SystemMonitor::getCpuModel() const { return cpu_model_; }
std::string SystemMonitor::getKernelVersion() const { return kernel_version_; }
std::string SystemMonitor::getDistribution() const { return distribution_; }

// ---------- Network/Disk Monitoring (Stub) ----------
SystemMonitor::NetworkStats SystemMonitor::getNetworkStats() {
    // Placeholder: parse /proc/net/dev
    NetworkStats stats{};
    return stats;
}

SystemMonitor::DiskStats SystemMonitor::getDiskStats() {
    // Placeholder: parse /proc/diskstats
    DiskStats stats{};
    return stats;
}

} // namespace SmartScheduler

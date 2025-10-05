#include "ModeManager.h"
#include <fstream>
#include <iostream>
#include <algorithm>

ModeManager::ModeManager(std::shared_ptr<SystemMonitor> monitor,
                         std::shared_ptr<ProcessManager> process_manager,
                         std::shared_ptr<Scheduler> scheduler,
                         std::shared_ptr<MemoryManager> memory_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , scheduler_(scheduler)
    , memory_manager_(memory_manager)
    , current_mode_(SystemMode::BALANCED)
    , previous_mode_(SystemMode::BALANCED)
    , mode_switching_active_(false)
    , auto_mode_enabled_(false)
    , auto_check_interval_(std::chrono::seconds(30))
    , adaptive_mode_enabled_(false)
    , smooth_transitions_enabled_(true)
    , transition_delay_(std::chrono::seconds(2))
    , battery_threshold_percent_(20)
    , thermal_threshold_celsius_(80.0) {
    
    // Initialize default mode configurations
    initializeDefaultModes();
    
    // Initialize metrics
    std::memset(&current_metrics_, 0, sizeof(ModeMetrics));
    current_metrics_.active_mode = current_mode_;
    current_metrics_.mode_start_time = std::chrono::system_clock::now();
}

ModeManager::~ModeManager() {
    // Stop auto-mode thread
    auto_mode_enabled_.store(false);
    if (auto_mode_thread_.joinable()) {
        auto_mode_thread_.join();
    }
    
    // Restore system state
    restoreSystemState();
}

void ModeManager::initializeDefaultModes() {
    // Gaming Mode Configuration
    mode_configs_[SystemMode::GAMING] = createGamingModeConfig();
    
    // Productivity Mode Configuration
    mode_configs_[SystemMode::PRODUCTIVITY] = createProductivityModeConfig();
    
    // Power Saving Mode Configuration
    mode_configs_[SystemMode::POWER_SAVING] = createPowerSavingModeConfig();
    
    // Balanced Mode Configuration
    mode_configs_[SystemMode::BALANCED] = createBalancedModeConfig();
}

ModeConfiguration ModeManager::createGamingModeConfig() const {
    ModeConfiguration config;
    config.mode = SystemMode::GAMING;
    config.name = "Gaming Mode";
    config.description = "Optimized for maximum gaming performance";
    
    // Scheduler settings
    config.scheduling_algorithm = SchedulingAlgorithm::PRIORITY_BASED;
    config.time_slice = std::chrono::milliseconds(50);
    config.enable_real_time_boost = true;
    config.enable_interactive_boost = true;
    
    // High priority game-related processes
    config.high_priority_processes = {
        "steam", "game", "wine", "proton", "dota", "csgo", 
        "unity", "unreal", "godot", "minecraft"
    };
    
    // Low priority background processes
    config.low_priority_processes = {
        "update", "backup", "indexer", "tracker"
    };
    
    // Suspend non-essential background tasks
    config.suspended_processes = {
        "update-notifier", "packagekit", "snapd"
    };
    
    // Memory management
    config.memory_strategy = MemoryOptimizationStrategy::CONSERVATIVE;
    config.enable_aggressive_cleanup = false;
    config.memory_pressure_threshold = 90.0;
    config.enable_swap = false; // Disable swap for gaming
    
    // CPU settings
    config.cpu_usage_limit = 100.0;
    config.enable_cpu_boost = true;
    config.enable_turbo_boost = true;
    config.cpu_governor = "performance";
    
    // System optimization
    config.suspend_non_essential = true;
    config.limit_background_apps = true;
    config.disable_visual_effects = false;
    config.optimize_network = true;
    config.optimize_disk_cache = true;
    config.reduce_system_logging = true;
    
    return config;
}

ModeConfiguration ModeManager::createProductivityModeConfig() const {
    ModeConfiguration config;
    config.mode = SystemMode::PRODUCTIVITY;
    config.name = "Productivity Mode";
    config.description = "Balanced performance for work applications";
    
    // Scheduler settings
    config.scheduling_algorithm = SchedulingAlgorithm::COMPLETELY_FAIR;
    config.time_slice = std::chrono::milliseconds(100);
    config.enable_real_time_boost = false;
    config.enable_interactive_boost = true;
    
    // High priority productivity apps
    config.high_priority_processes = {
        "chrome", "firefox", "code", "vscode", "sublime",
        "intellij", "eclipse", "libreoffice", "gimp", "blender"
    };
    
    // Memory management
    config.memory_strategy = MemoryOptimizationStrategy::BALANCED;
    config.enable_aggressive_cleanup = false;
    config.memory_pressure_threshold = 80.0;
    config.enable_swap = true;
    
    // CPU settings
    config.cpu_usage_limit = 90.0;
    config.enable_cpu_boost = false;
    config.enable_turbo_boost = false;
    config.cpu_governor = "ondemand";
    
    // System optimization
    config.suspend_non_essential = false;
    config.limit_background_apps = true;
    config.optimize_network = false;
    
    return config;
}

ModeConfiguration ModeManager::createPowerSavingModeConfig() const {
    ModeConfiguration config;
    config.mode = SystemMode::POWER_SAVING;
    config.name = "Power Saving Mode";
    config.description = "Minimize power consumption and extend battery life";
    
    // Scheduler settings
    config.scheduling_algorithm = SchedulingAlgorithm::ROUND_ROBIN;
    config.time_slice = std::chrono::milliseconds(200);
    config.enable_real_time_boost = false;
    config.enable_interactive_boost = false;
    
    // Low priority for most processes
    config.low_priority_processes = {
        "chrome", "firefox", "update", "indexer"
    };
    
    // Suspend many background processes
    config.suspended_processes = {
        "update-notifier", "packagekit", "snapd", "tracker-miner"
    };
    
    // Memory management
    config.memory_strategy = MemoryOptimizationStrategy::AGGRESSIVE;
    config.enable_aggressive_cleanup = true;
    config.memory_pressure_threshold = 70.0;
    config.enable_swap = true;
    
    // CPU settings
    config.cpu_usage_limit = 50.0;
    config.enable_cpu_boost = false;
    config.enable_turbo_boost = false;
    config.cpu_governor = "powersave";
    
    // System optimization
    config.suspend_non_essential = true;
    config.limit_background_apps = true;
    config.disable_visual_effects = true;
    config.reduce_system_logging = true;
    
    // Power management
    config.power_profile = "power-saver";
    config.screen_brightness = 30;
    config.cpu_frequency_limit = 60; // Limit to 60% of max
    config.enable_deep_sleep = true;
    
    return config;
}

ModeConfiguration ModeManager::createBalancedModeConfig() const {
    ModeConfiguration config;
    config.mode = SystemMode::BALANCED;
    config.name = "Balanced Mode";
    config.description = "Balance between performance and power efficiency";
    
    // Scheduler settings
    config.scheduling_algorithm = SchedulingAlgorithm::PRIORITY_BASED;
    config.time_slice = std::chrono::milliseconds(100);
    config.enable_interactive_boost = true;
    
    // Memory management
    config.memory_strategy = MemoryOptimizationStrategy::BALANCED;
    config.memory_pressure_threshold = 80.0;
    config.enable_swap = true;
    
    // CPU settings
    config.cpu_usage_limit = 100.0;
    config.cpu_governor = "ondemand";
    
    // System optimization
    config.limit_background_apps = false;
    
    return config;
}

bool ModeManager::switchToMode(SystemMode mode) {
    if (mode_switching_active_.load()) {
        std::cout << "Mode switch already in progress" << std::endl;
        return false;
    }
    
    if (mode == current_mode_) {
        std::cout << "Already in requested mode" << std::endl;
        return false;
    }
    
    mode_switching_active_.store(true);
    
    std::cout << "Switching from " << modeToString(current_mode_) 
              << " to " << modeToString(mode) << std::endl;
    
    try {
        // Backup current state before switching
        backupCurrentState();
        
        // Apply smooth transition delay if enabled
        if (smooth_transitions_enabled_) {
            std::this_thread::sleep_for(transition_delay_);
        }
        
        // Get configuration for new mode
        auto config = getModeConfiguration(mode);
        
        // Apply the new mode configuration
        appleModeConfiguration(config);
        
        // Update mode tracking
        previous_mode_ = current_mode_;
        current_mode_ = mode;
        
        // Reset metrics for new mode
        current_metrics_.active_mode = mode;
        current_metrics_.mode_start_time = std::chrono::system_clock::now();
        
        // Notify callbacks
        notifyModeChange(previous_mode_, current_mode_);
        
        std::cout << "Successfully switched to " << modeToString(mode) << std::endl;
        
        mode_switching_active_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to switch mode: " << e.what() << std::endl;
        
        // Try to restore previous state
        restoreSystemState();
        
        mode_switching_active_.store(false);
        return false;
    }
}

void ModeManager::appleModeConfiguration(const ModeConfiguration& config) {
    std::lock_guard<std::mutex> lock(mode_mutex_);
    
    // Configure scheduler
    configureScheduler(config);
    
    // Configure memory manager
    configureMemoryManager(config);
    
    // Configure process priorities
    configureProcessPriorities(config);
    
    // Configure CPU governor
    if (!config.cpu_governor.empty()) {
        configureCpuGovernor(config.cpu_governor);
    }
    
    // Configure system services
    configureSystemServices(config);
    
    // Configure power management
    configurePowerManagement(config);
}

void ModeManager::configureScheduler(const ModeConfiguration& config) {
    scheduler_->setSchedulingAlgorithm(config.scheduling_algorithm);
    scheduler_->setDefaultTimeSlice(config.time_slice);
    
    if (config.enable_real_time_boost) {
        scheduler_->enablePriorityBoosting(true);
    }
    
    std::cout << "Scheduler configured for " << config.name << std::endl;
}

void ModeManager::configureMemoryManager(const ModeConfiguration& config) {
    memory_manager_->setOptimizationStrategy(config.memory_strategy);
    memory_manager_->enableAutoOptimization(config.enable_aggressive_cleanup);
    
    if (config.memory_pressure_threshold > 0) {
        memory_manager_->setLowMemoryThreshold(config.memory_pressure_threshold);
    }
    
    if (!config.enable_swap) {
        memory_manager_->enableSwapManagement(false);
    } else {
        memory_manager_->enableSwapManagement(true);
    }
    
    std::cout << "Memory manager configured for " << config.name << std::endl;
}

void ModeManager::configureProcessPriorities(const ModeConfiguration& config) {
    // Set high priority processes
    for (const auto& proc_name : config.high_priority_processes) {
        auto processes = system_monitor_->getProcessesByName(proc_name);
        for (const auto& proc : processes) {
            process_manager_->setProcessPriority(proc.pid, ProcessPriority::HIGH);
            std::cout << "Set HIGH priority for: " << proc.name << std::endl;
        }
    }
    
    // Set low priority processes
    for (const auto& proc_name : config.low_priority_processes) {
        auto processes = system_monitor_->getProcessesByName(proc_name);
        for (const auto& proc : processes) {
            process_manager_->setProcessPriority(proc.pid, ProcessPriority::LOW);
            std::cout << "Set LOW priority for: " << proc.name << std::endl;
        }
    }
    
    // Suspend processes
    for (const auto& proc_name : config.suspended_processes) {
        auto processes = system_monitor_->getProcessesByName(proc_name);
        for (const auto& proc : processes) {
            process_manager_->pauseProcess(proc.pid);
            std::cout << "Suspended: " << proc.name << std::endl;
        }
    }
}

void ModeManager::configureCpuGovernor(const std::string& governor) {
    if (!setCpuGovernor(governor)) {
        std::cerr << "Failed to set CPU governor to: " << governor << std::endl;
    } else {
        std::cout << "CPU governor set to: " << governor << std::endl;
    }
}

void ModeManager::configureSystemServices(const ModeConfiguration& config) {
    // Disable services
    for (const auto& service : config.disabled_services) {
        if (disableSystemService(service)) {
            std::cout << "Disabled service: " << service << std::endl;
        }
    }
    
    // Enable services
    for (const auto& service : config.enabled_services) {
        if (enableSystemService(service)) {
            std::cout << "Enabled service: " << service << std::endl;
        }
    }
}

void ModeManager::configurePowerManagement(const ModeConfiguration& config) {
    // Set screen brightness if specified
    if (config.screen_brightness >= 0) {
        setScreenBrightness(config.screen_brightness);
    }
    
    // Enable/disable CPU turbo boost
    if (config.enable_turbo_boost) {
        enableCpuTurboBoost(true);
    } else {
        enableCpuTurboBoost(false);
    }
}

void ModeManager::backupCurrentState() {
    state_backup_.is_valid = true;
    
    // Backup scheduler algorithm
    state_backup_.scheduler_algorithm = scheduler_->getCurrentAlgorithm();
    
    // Backup CPU governor
    state_backup_.cpu_governor = getCurrentCpuGovernor();
    
    // Backup process priorities
    auto managed_processes = process_manager_->getAllManagedProcesses();
    for (const auto& proc : managed_processes) {
        state_backup_.process_priorities[proc.pid] = proc.current_priority;
        
        if (proc.current_state == ProcessState::SUSPENDED) {
            state_backup_.suspended_processes.push_back(proc.pid);
        }
    }
    
    // Backup memory strategy
    state_backup_.memory_strategy = MemoryOptimizationStrategy::BALANCED;
}

void ModeManager::restoreSystemState() {
    if (!state_backup_.is_valid) {
        return;
    }
    
    std::cout << "Restoring previous system state..." << std::endl;
    
    // Restore scheduler
    scheduler_->setSchedulingAlgorithm(state_backup_.scheduler_algorithm);
    
    // Restore CPU governor
    if (!state_backup_.cpu_governor.empty()) {
        setCpuGovernor(state_backup_.cpu_governor);
    }
    
    // Restore process priorities
    for (const auto& [pid, priority] : state_backup_.process_priorities) {
        if (system_monitor_->isProcessRunning(pid)) {
            process_manager_->setProcessPriority(pid, priority);
        }
    }
    
    // Resume suspended processes
    for (int pid : state_backup_.suspended_processes) {
        if (system_monitor_->isProcessRunning(pid)) {
            process_manager_->resumeProcess(pid);
        }
    }
    
    state_backup_.is_valid = false;
}

void ModeManager::enableAutoMode(bool enable) {
    if (enable && !auto_mode_enabled_.load()) {
        auto_mode_enabled_.store(true);
        auto_mode_thread_ = std::thread(&ModeManager::autoModeDetectionLoop, this);
        std::cout << "Auto-mode detection enabled" << std::endl;
        
    } else if (!enable && auto_mode_enabled_.load()) {
        auto_mode_enabled_.store(false);
        if (auto_mode_thread_.joinable()) {
            auto_mode_thread_.join();
        }
        std::cout << "Auto-mode detection disabled" << std::endl;
    }
}

void ModeManager::autoModeDetectionLoop() {
    while (auto_mode_enabled_.load()) {
        try {
            SystemMode optimal_mode = detectOptimalMode();
            
            if (optimal_mode != current_mode_) {
                std::string reason = "Detected optimal mode: " + modeToString(optimal_mode);
                notifyAutoModeDetection(optimal_mode, reason);
                
                // Auto-switch to detected mode
                switchToMode(optimal_mode);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Auto-mode detection error: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(auto_check_interval_);
    }
}

SystemMode ModeManager::detectOptimalMode() const {
    // Check for critical conditions first
    if (isLowPowerNeeded()) {
        return SystemMode::POWER_SAVING;
    }
    
    // Check for gaming activity
    if (isGamingActivity()) {
        return SystemMode::GAMING;
    }
    
    // Check for productivity activity
    if (isProductivityActivity()) {
        return SystemMode::PRODUCTIVITY;
    }
    
    // Default to balanced
    return SystemMode::BALANCED;
}

bool ModeManager::isGamingActivity() const {
    std::vector<std::string> game_indicators = {
        "steam", "game", "wine", "proton", "dota", "csgo",
        "valorant", "league", "minecraft", "unity", "unreal"
    };
    
    auto all_processes = system_monitor_->getProcesses();
    for (const auto& proc : all_processes) {
        std::string lower_name = proc.name;
        std::transform(lower_name.begin(), lower_name.end(), 
                      lower_name.begin(), ::tolower);
        
        for (const auto& indicator : game_indicators) {
            if (lower_name.find(indicator) != std::string::npos) {
                // Check if it's actually using significant resources
                if (proc.cpu_usage > 30.0 || proc.memory_usage_kb > 1024 * 1024) {
                    return true;
                }
            }
        }
    }
    
    return false;
}

bool ModeManager::isProductivityActivity() const {
    std::vector<std::string> productivity_indicators = {
        "chrome", "firefox", "code", "vscode", "sublime",
        "intellij", "eclipse", "pycharm", "libreoffice", "gimp"
    };
    
    int productivity_apps_count = 0;
    
    auto all_processes = system_monitor_->getProcesses();
    for (const auto& proc : all_processes) {
        std::string lower_name = proc.name;
        std::transform(lower_name.begin(), lower_name.end(), 
                      lower_name.begin(), ::tolower);
        
        for (const auto& indicator : productivity_indicators) {
            if (lower_name.find(indicator) != std::string::npos) {
                productivity_apps_count++;
                break;
            }
        }
    }
    
    // If multiple productivity apps are running
    return productivity_apps_count >= 2;
}

bool ModeManager::isLowPowerNeeded() const {
    // Check battery level
    if (isOnBatteryPower() && readBatteryLevel() < battery_threshold_percent_) {
        return true;
    }
    
    // Check temperature
    if (readSystemTemperature() > thermal_threshold_celsius_) {
        return true;
    }
    
    return false;
}

bool ModeManager::isOnBatteryPower() const {
    std::ifstream status_file("/sys/class/power_supply/BAT0/status");
    if (status_file.is_open()) {
        std::string status;
        status_file >> status;
        return (status == "Discharging");
    }
    return false;
}

double ModeManager::readBatteryLevel() const {
    std::ifstream capacity_file("/sys/class/power_supply/BAT0/capacity");
    if (capacity_file.is_open()) {
        int capacity;
        capacity_file >> capacity;
        return static_cast<double>(capacity);
    }
    return 100.0; // Assume full if can't read
}

double ModeManager::readSystemTemperature() const {
    // Try to read from thermal zone
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.is_open()) {
        int temp_millidegrees;
        temp_file >> temp_millidegrees;
        return temp_millidegrees / 1000.0;
    }
    return 50.0; // Assume normal temperature
}

bool ModeManager::setCpuGovernor(const std::string& governor) const {
    // Set governor for all CPUs
    int cpu_count = system_monitor_->getCpuCoreCount();
    bool success = true;
    
    for (int cpu = 0; cpu < cpu_count; ++cpu) {
        std::string governor_path = "/sys/devices/system/cpu/cpu" + 
                                   std::to_string(cpu) + 
                                   "/cpufreq/scaling_governor";
        
        std::ofstream gov_file(governor_path);
        if (gov_file.is_open()) {
            gov_file << governor;
            gov_file.close();
        } else {
            success = false;
        }
    }
    
    return success;
}

std::string ModeManager::getCurrentCpuGovernor() const {
    std::ifstream gov_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    if (gov_file.is_open()) {
        std::string governor;
        gov_file >> governor;
        return governor;
    }
    return "";
}

bool ModeManager::enableCpuTurboBoost(bool enable) const {
    // Intel turbo boost control
    std::ofstream turbo_file("/sys/devices/system/cpu/intel_pstate/no_turbo");
    if (turbo_file.is_open()) {
        turbo_file << (enable ? "0" : "1");
        turbo_file.close();
        return true;
    }
    
    // AMD boost control (alternative path)
    std::ofstream amd_boost("/sys/devices/system/cpu/cpufreq/boost");
    if (amd_boost.is_open()) {
        amd_boost << (enable ? "1" : "0");
        amd_boost.close();
        return true;
    }
    
    return false;
}

bool ModeManager::setScreenBrightness(int percentage) const {
    // Try common brightness control paths
    std::vector<std::string> brightness_paths = {
        "/sys/class/backlight/intel_backlight/brightness",
        "/sys/class/backlight/acpi_video0/brightness",
        "/sys/class/backlight/amdgpu_bl0/brightness"
    };
    
    for (const auto& path : brightness_paths) {
        // Read max brightness
        std::string max_path = path;
        size_t pos = max_path.rfind("/brightness");
        if (pos != std::string::npos) {
            max_path = max_path.substr(0, pos) + "/max_brightness";
        }
        
        std::ifstream max_file(max_path);
        if (max_file.is_open()) {
            int max_brightness;
            max_file >> max_brightness;
            
            int target_brightness = (max_brightness * percentage) / 100;
            
            std::ofstream brightness_file(path);
            if (brightness_file.is_open()) {
                brightness_file << target_brightness;
                brightness_file.close();
                return true;
            }
        }
    }
    
    return false;
}

bool ModeManager::enableSystemService(const std::string& service_name) const {
    std::string command = "systemctl start " + service_name + " 2>/dev/null";
    return (system(command.c_str()) == 0);
}

bool ModeManager::disableSystemService(const std::string& service_name) const {
    std::string command = "systemctl stop " + service_name + " 2>/dev/null";
    return (system(command.c_str()) == 0);
}

// Getter methods
ModeConfiguration ModeManager::getModeConfiguration(SystemMode mode) const {
    auto it = mode_configs_.find(mode);
    if (it != mode_configs_.end()) {
        return it->second;
    }
    return ModeConfiguration();
}

ModeMetrics ModeManager::getCurrentMetrics() const {
    return current_metrics_;
}

// Utility methods
std::string ModeManager::modeToString(SystemMode mode) const {
    switch (mode) {
        case SystemMode::GAMING: return "Gaming";
        case SystemMode::PRODUCTIVITY: return "Productivity";
        case SystemMode::POWER_SAVING: return "Power Saving";
        case SystemMode::BALANCED: return "Balanced";
        case SystemMode::CUSTOM: return "Custom";
        default: return "Unknown";
    }
}

SystemMode ModeManager::stringToMode(const std::string& mode_str) const {
    if (mode_str == "Gaming") return SystemMode::GAMING;
    if (mode_str == "Productivity") return SystemMode::PRODUCTIVITY;
    if (mode_str == "Power Saving") return SystemMode::POWER_SAVING;
    if (mode_str == "Balanced") return SystemMode::BALANCED;
    if (mode_str == "Custom") return SystemMode::CUSTOM;
    return SystemMode::BALANCED;
}

// Callback methods
void ModeManager::registerModeChangeCallback(ModeChangeCallback callback) {
    mode_change_callbacks_.push_back(callback);
}

void ModeManager::notifyModeChange(SystemMode old_mode, SystemMode new_mode) {
    for (const auto& callback : mode_change_callbacks_) {
        callback(old_mode, new_mode);
    }
}

void ModeManager::notifyAutoModeDetection(SystemMode suggested_mode, const std::string& reason) {
    for (const auto& callback : auto_mode_callbacks_) {
        callback(suggested_mode, reason);
    }
}

// Quick actions
void ModeManager::quickBoostPerformance(std::chrono::minutes duration) {
    std::cout << "Quick performance boost for " << duration.count() << " minutes" << std::endl;
    
    // Temporarily switch to gaming mode
    SystemMode original_mode = current_mode_;
    switchToMode(SystemMode::GAMING);
    
    // Schedule return to original mode
    std::thread([this, original_mode, duration]() {
        std::this_thread::sleep_for(duration);
        switchToMode(original_mode);
    }).detach();
}

void ModeManager::quickPowerSave(std::chrono::minutes duration) {
    std::cout << "Quick power save for " << duration.count() << " minutes" << std::endl;
    
    SystemMode original_mode = current_mode_;
    switchToMode(SystemMode::POWER_SAVING);
    
    std::thread([this, original_mode, duration]() {
        std::this_thread::sleep_for(duration);
        switchToMode(original_mode);
    }).detach();
}
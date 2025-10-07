#include "ConfigManager.h"
#include "Logger.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

SchedulerConfig ConfigManager::loadConfig(const std::string& file_path) {
    SchedulerConfig config;
    std::ifstream file(file_path);
    json j;
    file >> j;
    config.priority_high = j["priority_high"];
    config.priority_low = j["priority_low"];
    config.time_quantum_ms = j["time_quantum_ms"];
    config.memory_threshold_mb = j["memory_threshold_mb"];
    config.cpu_affinity_cores = j["cpu_affinity_cores"].get<std::vector<int>>();
    config.cgroup_cpu_shares = j["cgroup_cpu_shares"];
    config.cgroup_memory_limit_mb = j["cgroup_memory_limit_mb"];
    config.ipc_queue_size = j["ipc_queue_size"];
    validateConfig(config);
    Logger::log("Loaded config from " + file_path);
    return config;
}

void ConfigManager::validateConfig(const SchedulerConfig& config) {
    if (config.priority_high < -20 || config.priority_high > 19) {
        Logger::log("Invalid priority_high: " + std::to_string(config.priority_high));
        throw std::runtime_error("Invalid priority_high");
    }
    if (config.time_quantum_ms < 5 || config.time_quantum_ms > 1000) {
        Logger::log("Invalid time_quantum_ms: " + std::to_string(config.time_quantum_ms));
        throw std::runtime_error("Invalid time_quantum_ms");
    }
}

void ConfigManager::reloadConfigIfChanged(const std::string& file_path) {
    // Placeholder for dynamic reloading
    Logger::log("Checking for config changes in " + file_path);
}
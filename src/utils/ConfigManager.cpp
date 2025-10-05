#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

ConfigManager::ConfigManager() 
    : auto_save_(false) {
    setDefaultValues();
}

ConfigManager::~ConfigManager() {
    if (auto_save_ && !config_file_path_.empty()) {
        saveConfiguration();
    }
}

void ConfigManager::setDefaultValues() {
    config_values_["monitoring_interval_ms"] = "1000";
    config_values_["default_scheduling_algorithm"] = "priority";
    config_values_["default_time_slice_ms"] = "100";
    config_values_["memory_threshold_percent"] = "80.0";
    config_values_["cpu_threshold_percent"] = "90.0";
    config_values_["enable_auto_optimization"] = "true";
    config_values_["enable_auto_mode"] = "false";
    config_values_["default_mode"] = "balanced";
    config_values_["log_level"] = "info";
    config_values_["enable_console_output"] = "true";
}

bool ConfigManager::loadConfiguration(const std::string& filepath) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    config_file_path_ = filepath;
    
    if (!parseConfigFile(filepath)) {
        std::cerr << "Failed to load configuration from: " << filepath << std::endl;
        std::cerr << "Using default values" << std::endl;
        return false;
    }
    
    return true;
}

bool ConfigManager::parseConfigFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        // Remove comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        
        // Skip empty lines
        if (line.empty()) {
            continue;
        }
        
        // Parse key=value pairs
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            std::cerr << "Invalid config line " << line_number << ": " << line << std::endl;
            continue;
        }
        
        std::string key = line.substr(0, equals_pos);
        std::string value = line.substr(equals_pos + 1);
        
        // Trim key and value
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        // Remove quotes from value if present
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        
        config_values_[key] = value;
    }
    
    file.close();
    return true;
}

bool ConfigManager::saveConfiguration(const std::string& filepath) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    return writeConfigFile(filepath);
}

bool ConfigManager::saveConfiguration() const {
    if (config_file_path_.empty()) {
        return false;
    }
    
    return saveConfiguration(config_file_path_);
}

bool ConfigManager::writeConfigFile(const std::string& filepath) const {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << "# Smart Resource Scheduler Configuration\n";
    file << "# Generated automatically\n\n";
    
    for (const auto& [key, value] : config_values_) {
        file << key << " = " << value << "\n";
    }
    
    file.close();
    return true;
}

std::string ConfigManager::getString(const std::string& key, const std::string& default_value) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    auto it = config_values_.find(key);
    if (it != config_values_.end()) {
        return it->second;
    }
    
    return default_value;
}

int ConfigManager::getInt(const std::string& key, int default_value) const {
    std::string str_value = getString(key);
    if (str_value.empty()) {
        return default_value;
    }
    
    try {
        return std::stoi(str_value);
    } catch (...) {
        return default_value;
    }
}

double ConfigManager::getDouble(const std::string& key, double default_value) const {
    std::string str_value = getString(key);
    if (str_value.empty()) {
        return default_value;
    }
    
    try {
        return std::stod(str_value);
    } catch (...) {
        return default_value;
    }
}

bool ConfigManager::getBool(const std::string& key, bool default_value) const {
    std::string str_value = getString(key);
    if (str_value.empty()) {
        return default_value;
    }
    
    std::transform(str_value.begin(), str_value.end(), str_value.begin(), ::tolower);
    
    return (str_value == "true" || str_value == "1" || str_value == "yes" || str_value == "on");
}

void ConfigManager::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_values_[key] = value;
    
    if (auto_save_ && !config_file_path_.empty()) {
        writeConfigFile(config_file_path_);
    }
}

void ConfigManager::set(const std::string& key, int value) {
    set(key, std::to_string(value));
}

void ConfigManager::set(const std::string& key, double value) {
    set(key, std::to_string(value));
}

void ConfigManager::set(const std::string& key, bool value) {
    set(key, value ? "true" : "false");
}

bool ConfigManager::hasKey(const std::string& key) const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_values_.find(key) != config_values_.end();
}

void ConfigManager::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_values_.erase(key);
}

void ConfigManager::clear() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_values_.clear();
    setDefaultValues();
}

std::vector<std::string> ConfigManager::getAllKeys() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    
    std::vector<std::string> keys;
    keys.reserve(config_values_.size());
    
    for (const auto& [key, value] : config_values_) {
        keys.push_back(key);
    }
    
    return keys;
}
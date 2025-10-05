#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>

class ConfigManager {
private:
    std::unordered_map<std::string, std::string> config_values_;
    std::string config_file_path_;
    mutable std::mutex config_mutex_;
    bool auto_save_;
    
    bool parseConfigFile(const std::string& filepath);
    bool writeConfigFile(const std::string& filepath) const;
    void setDefaultValues();

public:
    ConfigManager();
    ~ConfigManager();
    
    bool loadConfiguration(const std::string& filepath);
    bool saveConfiguration(const std::string& filepath) const;
    bool saveConfiguration() const;
    
    // Getters
    std::string getString(const std::string& key, const std::string& default_value = "") const;
    int getInt(const std::string& key, int default_value = 0) const;
    double getDouble(const std::string& key, double default_value = 0.0) const;
    bool getBool(const std::string& key, bool default_value = false) const;
    
    // Setters
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, int value);
    void set(const std::string& key, double value);
    void set(const std::string& key, bool value);
    
    // Utility
    bool hasKey(const std::string& key) const;
    void remove(const std::string& key);
    void clear();
    
    void setAutoSave(bool enable) { auto_save_ = enable; }
    std::vector<std::string> getAllKeys() const;
};

#endif
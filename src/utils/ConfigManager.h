#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "types.h"
#include <string>

class ConfigManager {
public:
    SchedulerConfig loadConfig(const std::string& file_path);
    void validateConfig(const SchedulerConfig& config);

private:
    void reloadConfigIfChanged(const std::string& file_path);
};

#endif
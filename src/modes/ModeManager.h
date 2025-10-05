#ifndef MODE_MANAGER_H
#define MODE_MANAGER_H

#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace SmartScheduler {

enum class SystemMode {
    GAMING,
    PRODUCTIVITY,
    POWER_SAVING,
    BALANCED,
    CUSTOM
};

struct ModeConfiguration {
    int maxCpuUsage;      // percentage
    int maxMemoryUsage;   // MB
    int processPriority;  // 1-10
    // add more configurable parameters if needed
};

using ModeCallback = std::function<void(SystemMode)>;

class ModeManager {
public:
    ModeManager();

    SystemMode getCurrentMode() const;
    void setMode(SystemMode mode);
    
    void registerCallback(ModeCallback cb);

    ModeConfiguration getConfig(SystemMode mode) const;
    void setConfig(SystemMode mode, const ModeConfiguration& config);

    static std::string modeToString(SystemMode mode);

private:
    mutable std::mutex mutex_;
    SystemMode currentMode_;
    std::unordered_map<SystemMode, ModeConfiguration> configurations_;
    std::vector<ModeCallback> callbacks_;
};

} // namespace SmartScheduler

#endif // MODE_MANAGER_H

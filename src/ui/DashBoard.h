#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "SystemMonitor.h"
#include "ProcessManager.h"
#include "Scheduler.h"
#include "MemoryManager.h"
#include "ModeManager.h"
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>

enum class DashboardView {
    OVERVIEW,
    PROCESSES,
    MEMORY,
    PERFORMANCE,
    MODES
};

struct UIColors {
    static const int HEADER = 1;
    static const int NORMAL = 2;
    static const int WARNING = 3;
    static const int CRITICAL = 4;
    static const int SUCCESS = 5;
    static const int INFO = 6;
};

class Dashboard {
private:
    // Core components
    std::shared_ptr<SystemMonitor> system_monitor_;
    std::shared_ptr<ProcessManager> process_manager_;
    std::shared_ptr<Scheduler> scheduler_;
    std::shared_ptr<MemoryManager> memory_manager_;
    std::shared_ptr<ModeManager> mode_manager_;
    
    // UI state
    std::atomic<bool> running_;
    std::thread ui_thread_;
    std::thread input_thread_;
    mutable std::mutex ui_mutex_;
    
    DashboardView current_view_;
    int selected_row_;
    int scroll_offset_;
    bool show_help_;
    
    // Update interval
    std::chrono::milliseconds update_interval_;
    
    // Cached data for display
    std::vector<ProcessInfo> cached_processes_;
    SystemStats cached_system_stats_;
    std::string status_message_;
    
    // UI dimensions
    int screen_height_;
    int screen_width_;
    
    // UI methods
    void initializeUI();
    void cleanupUI();
    void uiLoop();
    void inputLoop();
    void render();
    
    // View rendering
    void renderHeader();
    void renderOverview();
    void renderProcesses();
    void renderMemory();
    void renderPerformance();
    void renderModes();
    void renderFooter();
    void renderHelp();
    
    // Helper methods
    void drawProgressBar(int y, int x, int width, double percentage, const std::string& label);
    void drawBox(int y, int x, int height, int width, const std::string& title);
    std::string formatBytes(size_t bytes) const;
    std::string formatPercentage(double value) const;
    std::string getColorForValue(double value, double warning, double critical) const;
    int getColorPair(double value, double warning, double critical) const;
    
    // Input handling
    void handleInput(int ch);
    void handleViewSwitch(DashboardView view);
    void handleProcessAction();
    void handleModeSwitch();
    
    // Data refresh
    void refreshData();
    
public:
    Dashboard(std::shared_ptr<SystemMonitor> monitor,
              std::shared_ptr<ProcessManager> process_manager,
              std::shared_ptr<Scheduler> scheduler,
              std::shared_ptr<MemoryManager> memory_manager,
              std::shared_ptr<ModeManager> mode_manager);
    ~Dashboard();
    
    void show();
    void exec();
    void hide();
    bool isRunning() const { return running_.load(); }
    
    void setUpdateInterval(std::chrono::milliseconds interval);
    void setStatusMessage(const std::string& message);
};

#endif
#include "Dashboard.h"
#include <ncurses.h>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

Dashboard::Dashboard(std::shared_ptr<SystemMonitor> monitor,
                     std::shared_ptr<ProcessManager> process_manager,
                     std::shared_ptr<Scheduler> scheduler,
                     std::shared_ptr<MemoryManager> memory_manager,
                     std::shared_ptr<ModeManager> mode_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , scheduler_(scheduler)
    , memory_manager_(memory_manager)
    , mode_manager_(mode_manager)
    , running_(false)
    , current_view_(DashboardView::OVERVIEW)
    , selected_row_(0)
    , scroll_offset_(0)
    , show_help_(false)
    , update_interval_(std::chrono::milliseconds(1000))
    , screen_height_(0)
    , screen_width_(0) {
}

Dashboard::~Dashboard() {
    hide();
}

void Dashboard::show() {
    if (running_.load()) {
        return;
    }
    
    running_.store(true);
    initializeUI();
    
    // Start UI update thread
    ui_thread_ = std::thread(&Dashboard::uiLoop, this);
    
    // Start input handling thread
    input_thread_ = std::thread(&Dashboard::inputLoop, this);
}

void Dashboard::exec() {
    show();
    
    // Wait for threads to finish
    if (ui_thread_.joinable()) {
        ui_thread_.join();
    }
    if (input_thread_.joinable()) {
        input_thread_.join();
    }
}

void Dashboard::hide() {
    if (running_.load()) {
        running_.store(false);
        
        if (ui_thread_.joinable()) {
            ui_thread_.join();
        }
        if (input_thread_.joinable()) {
            input_thread_.join();
        }
        
        cleanupUI();
    }
}

void Dashboard::initializeUI() {
    // Initialize ncurses
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    
    // Initialize colors
    if (has_colors()) {
        start_color();
        init_pair(UIColors::HEADER, COLOR_CYAN, COLOR_BLACK);
        init_pair(UIColors::NORMAL, COLOR_WHITE, COLOR_BLACK);
        init_pair(UIColors::WARNING, COLOR_YELLOW, COLOR_BLACK);
        init_pair(UIColors::CRITICAL, COLOR_RED, COLOR_BLACK);
        init_pair(UIColors::SUCCESS, COLOR_GREEN, COLOR_BLACK);
        init_pair(UIColors::INFO, COLOR_BLUE, COLOR_BLACK);
    }
    
    // Get screen dimensions
    getmaxyx(stdscr, screen_height_, screen_width_);
}

void Dashboard::cleanupUI() {
    endwin();
}

void Dashboard::uiLoop() {
    while (running_.load()) {
        try {
            // Refresh data
            refreshData();
            
            // Render UI
            {
                std::lock_guard<std::mutex> lock(ui_mutex_);
                render();
            }
            
            // Sleep for update interval
            std::this_thread::sleep_for(update_interval_);
            
        } catch (const std::exception& e) {
            setStatusMessage("Error: " + std::string(e.what()));
        }
    }
}

void Dashboard::inputLoop() {
    while (running_.load()) {
        int ch = getch();
        
        if (ch != ERR) {
            std::lock_guard<std::mutex> lock(ui_mutex_);
            handleInput(ch);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Dashboard::render() {
    clear();
    
    // Update screen dimensions
    getmaxyx(stdscr, screen_height_, screen_width_);
    
    // Render header
    renderHeader();
    
    // Render current view
    switch (current_view_) {
        case DashboardView::OVERVIEW:
            renderOverview();
            break;
        case DashboardView::PROCESSES:
            renderProcesses();
            break;
        case DashboardView::MEMORY:
            renderMemory();
            break;
        case DashboardView::PERFORMANCE:
            renderPerformance();
            break;
        case DashboardView::MODES:
            renderModes();
            break;
    }
    
    // Render footer
    renderFooter();
    
    // Render help if shown
    if (show_help_) {
        renderHelp();
    }
    
    refresh();
}

void Dashboard::renderHeader() {
    attron(COLOR_PAIR(UIColors::HEADER) | A_BOLD);
    
    // Title
    mvprintw(0, (screen_width_ - 30) / 2, "Smart Resource Scheduler v1.0");
    
    // Current time
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::stringstream time_ss;
    time_ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    mvprintw(0, screen_width_ - 25, "%s", time_ss.str().c_str());
    
    // View tabs
    int tab_x = 2;
    mvprintw(1, tab_x, "[1]Overview");
    tab_x += 13;
    mvprintw(1, tab_x, "[2]Processes");
    tab_x += 14;
    mvprintw(1, tab_x, "[3]Memory");
    tab_x += 11;
    mvprintw(1, tab_x, "[4]Performance");
    tab_x += 16;
    mvprintw(1, tab_x, "[5]Modes");
    
    // Highlight current view
    int highlight_pos = 2;
    switch (current_view_) {
        case DashboardView::OVERVIEW: highlight_pos = 2; break;
        case DashboardView::PROCESSES: highlight_pos = 15; break;
        case DashboardView::MEMORY: highlight_pos = 29; break;
        case DashboardView::PERFORMANCE: highlight_pos = 40; break;
        case DashboardView::MODES: highlight_pos = 56; break;
    }
    
    attron(A_REVERSE);
    mvprintw(1, highlight_pos, "   ");
    attroff(A_REVERSE);
    
    // Draw separator line
    mvhline(2, 0, ACS_HLINE, screen_width_);
    
    attroff(COLOR_PAIR(UIColors::HEADER) | A_BOLD);
}

void Dashboard::renderOverview() {
    int y = 4;
    int col1_x = 2;
    int col2_x = screen_width_ / 2 + 2;
    
    // Current mode
    attron(COLOR_PAIR(UIColors::INFO) | A_BOLD);
    mvprintw(y, col1_x, "Current Mode:");
    attroff(COLOR_PAIR(UIColors::INFO) | A_BOLD);
    
    std::string mode_name;
    switch (mode_manager_->getCurrentMode()) {
        case SystemMode::GAMING: mode_name = "Gaming"; break;
        case SystemMode::PRODUCTIVITY: mode_name = "Productivity"; break;
        case SystemMode::POWER_SAVING: mode_name = "Power Saving"; break;
        case SystemMode::BALANCED: mode_name = "Balanced"; break;
        default: mode_name = "Custom"; break;
    }
    
    attron(COLOR_PAIR(UIColors::SUCCESS));
    mvprintw(y, col1_x + 15, "%s", mode_name.c_str());
    attroff(COLOR_PAIR(UIColors::SUCCESS));
    
    y += 2;
    
    // CPU Usage
    double cpu_usage = cached_system_stats_.cpu_usage_total;
    mvprintw(y, col1_x, "CPU Usage:");
    drawProgressBar(y, col1_x + 12, 30, cpu_usage, formatPercentage(cpu_usage));
    
    // CPU Cores
    mvprintw(y, col2_x, "CPU Cores: %d", cached_system_stats_.cpu_core_count);
    
    y += 2;
    
    // Memory Usage
    double memory_usage = 100.0 * cached_system_stats_.memory_used_kb / 
                         cached_system_stats_.memory_total_kb;
    mvprintw(y, col1_x, "Memory:");
    drawProgressBar(y, col1_x + 12, 30, memory_usage, formatPercentage(memory_usage));
    
    // Total Memory
    mvprintw(y, col2_x, "Total: %s", 
             formatBytes(cached_system_stats_.memory_total_kb * 1024).c_str());
    
    y += 2;
    
    // Load Average
    mvprintw(y, col1_x, "Load Avg: %.2f, %.2f, %.2f", 
             cached_system_stats_.load_average_1min,
             cached_system_stats_.load_average_5min,
             cached_system_stats_.load_average_15min);
    
    // Process Count
    mvprintw(y, col2_x, "Processes: %zu", cached_processes_.size());
    
    y += 2;
    
    // Managed Processes
    mvprintw(y, col1_x, "Managed: %zu", process_manager_->getManagedProcessCount());
    mvprintw(y, col2_x, "Suspended: %zu", process_manager_->getSuspendedProcessCount());
    
    y += 3;
    
    // Top CPU Processes
    drawBox(y, col1_x, 10, screen_width_ / 2 - 4, "Top CPU Processes");
    
    auto top_cpu = system_monitor_->getTopCpuProcesses(5);
    int py = y + 2;
    for (size_t i = 0; i < std::min(size_t(5), top_cpu.size()); ++i) {
        int color = top_cpu[i].cpu_usage > 80 ? UIColors::CRITICAL : 
                   top_cpu[i].cpu_usage > 50 ? UIColors::WARNING : UIColors::NORMAL;
        
        attron(COLOR_PAIR(color));
        mvprintw(py++, col1_x + 2, "%-20.20s %6.1f%%", 
                top_cpu[i].name.c_str(), top_cpu[i].cpu_usage);
        attroff(COLOR_PAIR(color));
    }
    
    // Top Memory Processes
    drawBox(y, col2_x, 10, screen_width_ / 2 - 4, "Top Memory Processes");
    
    auto top_mem = system_monitor_->getTopMemoryProcesses(5);
    py = y + 2;
    for (size_t i = 0; i < std::min(size_t(5), top_mem.size()); ++i) {
        mvprintw(py++, col2_x + 2, "%-20.20s %s", 
                top_mem[i].name.c_str(), 
                formatBytes(top_mem[i].memory_usage_kb * 1024).c_str());
    }
}

void Dashboard::renderProcesses() {
    int y = 4;
    
    // Process list header
    attron(COLOR_PAIR(UIColors::HEADER) | A_BOLD);
    mvprintw(y, 2, "%-8s %-25s %8s %10s %6s %8s", 
             "PID", "Name", "CPU%", "Memory", "State", "Priority");
    attroff(COLOR_PAIR(UIColors::HEADER) | A_BOLD);
    
    mvhline(y + 1, 0, ACS_HLINE, screen_width_);
    
    y += 2;
    
    // Display processes
    int max_rows = screen_height_ - y - 3;
    size_t start_idx = scroll_offset_;
    size_t end_idx = std::min(start_idx + max_rows, cached_processes_.size());
    
    for (size_t i = start_idx; i < end_idx; ++i) {
        const auto& proc = cached_processes_[i];
        
        // Highlight selected row
        if (i == selected_row_) {
            attron(A_REVERSE);
        }
        
        // Color based on resource usage
        int color = proc.cpu_usage > 80 ? UIColors::CRITICAL :
                   proc.cpu_usage > 50 ? UIColors::WARNING : UIColors::NORMAL;
        attron(COLOR_PAIR(color));
        
        char state_char = proc.state;
        std::string state_str(1, state_char);
        
        mvprintw(y, 2, "%-8d %-25.25s %7.1f%% %10s %6s %8d", 
                 proc.pid,
                 proc.name.c_str(),
                 proc.cpu_usage,
                 formatBytes(proc.memory_usage_kb * 1024).c_str(),
                 state_str.c_str(),
                 proc.priority);
        
        attroff(COLOR_PAIR(color));
        
        if (i == selected_row_) {
            attroff(A_REVERSE);
        }
        
        y++;
    }
    
    // Scroll indicator
    if (cached_processes_.size() > max_rows) {
        attron(COLOR_PAIR(UIColors::INFO));
        mvprintw(screen_height_ - 2, screen_width_ - 30, 
                "Showing %zu-%zu of %zu", 
                start_idx + 1, end_idx, cached_processes_.size());
        attroff(COLOR_PAIR(UIColors::INFO));
    }
}

void Dashboard::renderMemory() {
    int y = 4;
    int x = 5;
    
    auto mem_info = memory_manager_->getSystemMemoryInfo();
    
    // Memory overview
    drawBox(y, x, 12, screen_width_ - 10, "Memory Overview");
    
    y += 2;
    x += 2;
    
    // Total memory
    mvprintw(y++, x, "Total Memory:     %s", 
             formatBytes(mem_info.total_memory_kb * 1024).c_str());
    
    // Used memory
    double used_percent = 100.0 * mem_info.used_memory_kb / mem_info.total_memory_kb;
    mvprintw(y++, x, "Used Memory:      %s (%.1f%%)", 
             formatBytes(mem_info.used_memory_kb * 1024).c_str(), used_percent);
    
    // Available memory
    mvprintw(y++, x, "Available Memory: %s", 
             formatBytes(mem_info.available_memory_kb * 1024).c_str());
    
    // Cached memory
    mvprintw(y++, x, "Cached Memory:    %s", 
             formatBytes(mem_info.cached_memory_kb * 1024).c_str());
    
    // Buffered memory
    mvprintw(y++, x, "Buffered Memory:  %s", 
             formatBytes(mem_info.buffered_memory_kb * 1024).c_str());
    
    y += 2;
    
    // Swap information
    if (mem_info.total_swap_kb > 0) {
        mvprintw(y++, x, "Total Swap:       %s", 
                 formatBytes(mem_info.total_swap_kb * 1024).c_str());
        
        double swap_used_percent = 100.0 * mem_info.used_swap_kb / mem_info.total_swap_kb;
        mvprintw(y++, x, "Used Swap:        %s (%.1f%%)", 
                 formatBytes(mem_info.used_swap_kb * 1024).c_str(), swap_used_percent);
    }
    
    y += 2;
    
    // Memory pressure
    std::string pressure_str;
    int pressure_color;
    switch (mem_info.pressure_level) {
        case MemoryPressureLevel::LOW:
            pressure_str = "LOW";
            pressure_color = UIColors::SUCCESS;
            break;
        case MemoryPressureLevel::MEDIUM:
            pressure_str = "MEDIUM";
            pressure_color = UIColors::WARNING;
            break;
        case MemoryPressureLevel::HIGH:
            pressure_str = "HIGH";
            pressure_color = UIColors::WARNING;
            break;
        case MemoryPressureLevel::CRITICAL:
            pressure_str = "CRITICAL";
            pressure_color = UIColors::CRITICAL;
            break;
    }
    
    mvprintw(y, x, "Memory Pressure:  ");
    attron(COLOR_PAIR(pressure_color) | A_BOLD);
    mvprintw(y++, x + 18, "%s", pressure_str.c_str());
    attroff(COLOR_PAIR(pressure_color) | A_BOLD);
    
    // Visual memory bar
    y += 2;
    mvprintw(y, x, "Memory Usage:");
    drawProgressBar(y + 1, x, screen_width_ - 15, used_percent, "");
}

void Dashboard::renderPerformance() {
    int y = 4;
    
    auto sched_stats = scheduler_->getSchedulingStatistics();
    
    // Scheduler statistics
    drawBox(y, 5, 10, screen_width_ - 10, "Scheduler Statistics");
    
    y += 2;
    int x = 7;
    
    mvprintw(y++, x, "Algorithm:         %s", 
             sched_stats.current_algorithm == SchedulingAlgorithm::PRIORITY_BASED ? "Priority" :
             sched_stats.current_algorithm == SchedulingAlgorithm::ROUND_ROBIN ? "Round Robin" :
             sched_stats.current_algorithm == SchedulingAlgorithm::COMPLETELY_FAIR ? "CFS" : "Multilevel");
    
    mvprintw(y++, x, "Context Switches:  %zu", sched_stats.total_context_switches);
    mvprintw(y++, x, "Preemptions:       %zu", sched_stats.total_preemptions);
    mvprintw(y++, x, "Avg Response Time: %.2f ms", sched_stats.average_response_time);
    mvprintw(y++, x, "CPU Utilization:   %.1f%%", sched_stats.cpu_utilization);
    mvprintw(y++, x, "Active Processes:  %d", sched_stats.active_processes);
    
    y += 3;
    
    // Performance metrics over time would go here
    // For now, show system statistics
    mvprintw(y++, x, "System Uptime:     Running");
    mvprintw(y++, x, "Total Optimizations: %zu", memory_manager_->getTotalProcessesOptimized());
    mvprintw(y++, x, "Memory Freed:      %s", 
             formatBytes(memory_manager_->getTotalMemoryFreed() * 1024).c_str());
}

void Dashboard::renderModes() {
    int y = 4;
    int x = 10;
    
    drawBox(y, x, 18, screen_width_ - 20, "System Modes");
    
    y += 2;
    x += 2;
    
    SystemMode current = mode_manager_->getCurrentMode();
    
    // Mode options
    const char* modes[] = {"Gaming", "Productivity", "Power Saving", "Balanced"};
    SystemMode mode_enums[] = {
        SystemMode::GAMING, 
        SystemMode::PRODUCTIVITY, 
        SystemMode::POWER_SAVING, 
        SystemMode::BALANCED
    };
    
    for (int i = 0; i < 4; ++i) {
        if (mode_enums[i] == current) {
            attron(COLOR_PAIR(UIColors::SUCCESS) | A_BOLD | A_REVERSE);
            mvprintw(y, x, " >> %-20s << [ACTIVE]", modes[i]);
            attroff(COLOR_PAIR(UIColors::SUCCESS) | A_BOLD | A_REVERSE);
        } else {
            mvprintw(y, x, "    %-20s    [Press %d to activate]", modes[i], i + 1);
        }
        y += 2;
    }
    
    y += 2;
    
    // Mode descriptions
    mvprintw(y++, x, "Current Mode Details:");
    y++;
    
    switch (current) {
        case SystemMode::GAMING:
            mvprintw(y++, x + 2, "- Maximum CPU performance");
            mvprintw(y++, x + 2, "- High priority for game processes");
            mvprintw(y++, x + 2, "- Minimal background activity");
            mvprintw(y++, x + 2, "- Turbo boost enabled");
            break;
        case SystemMode::PRODUCTIVITY:
            mvprintw(y++, x + 2, "- Balanced performance");
            mvprintw(y++, x + 2, "- Fair CPU scheduling");
            mvprintw(y++, x + 2, "- Optimized for multitasking");
            break;
        case SystemMode::POWER_SAVING:
            mvprintw(y++, x + 2, "- Reduced CPU frequency");
            mvprintw(y++, x + 2, "- Aggressive memory cleanup");
            mvprintw(y++, x + 2, "- Background tasks suspended");
            mvprintw(y++, x + 2, "- Extended battery life");
            break;
        case SystemMode::BALANCED:
            mvprintw(y++, x + 2, "- General purpose mode");
            mvprintw(y++, x + 2, "- Balanced resource allocation");
            mvprintw(y++, x + 2, "- Adaptive scheduling");
            break;
        default:
            break;
    }
}

void Dashboard::renderFooter() {
    int y = screen_height_ - 2;
    
    attron(COLOR_PAIR(UIColors::HEADER));
    mvhline(y, 0, ACS_HLINE, screen_width_);
    
    // Status message or help hint
    if (!status_message_.empty()) {
        attron(COLOR_PAIR(UIColors::INFO));
        mvprintw(y + 1, 2, "%s", status_message_.c_str());
        attroff(COLOR_PAIR(UIColors::INFO));
    } else {
        mvprintw(y + 1, 2, "Press '?' for help | 'q' to quit");
    }
    
    attroff(COLOR_PAIR(UIColors::HEADER));
}

void Dashboard::renderHelp() {
    int height = 20;
    int width = 60;
    int y = (screen_height_ - height) / 2;
    int x = (screen_width_ - width) / 2;
    
    // Draw help box
    attron(COLOR_PAIR(UIColors::INFO));
    for (int i = 0; i < height; ++i) {
        mvhline(y + i, x, ' ', width);
    }
    
    // Border
    mvhline(y, x, ACS_HLINE, width);
    mvhline(y + height - 1, x, ACS_HLINE, width);
    mvvline(y, x, ACS_VLINE, height);
    mvvline(y, x + width - 1, ACS_VLINE, height);
    
    // Corners
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + width - 1, ACS_URCORNER);
    mvaddch(y + height - 1, x, ACS_LLCORNER);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    
    attron(A_BOLD);
    mvprintw(y + 1, x + (width - 15) / 2, "KEYBOARD SHORTCUTS");
    attroff(A_BOLD);
    
    int help_y = y + 3;
    mvprintw(help_y++, x + 2, "1-5      : Switch views");
    mvprintw(help_y++, x + 2, "↑/↓      : Navigate list");
    mvprintw(help_y++, x + 2, "PgUp/PgDn: Scroll page");
    mvprintw(help_y++, x + 2, "g/p/s/b  : Gaming/Productivity/PowerSave/Balanced mode");
    mvprintw(help_y++, x + 2, "k        : Kill selected process");
    mvprintw(help_y++, x + 2, "t        : Terminate selected process");
    mvprintw(help_y++, x + 2, "r        : Resume selected process");
    mvprintw(help_y++, x + 2, "m        : Optimize memory");
    mvprintw(help_y++, x + 2, "c        : Clear caches");
    mvprintw(help_y++, x + 2, "?        : Toggle this help");
    mvprintw(help_y++, x + 2, "q        : Quit");
    
    mvprintw(y + height - 2, x + (width - 25) / 2, "Press any key to close");
    
    attroff(COLOR_PAIR(UIColors::INFO));
}

void Dashboard::drawProgressBar(int y, int x, int width, double percentage, const std::string& label) {
    // Clamp percentage
    percentage = std::clamp(percentage, 0.0, 100.0);
    
    int filled = static_cast<int>((percentage / 100.0) * width);
    
    // Draw bar
    mvaddch(y, x, '[');
    
    int color = getColorPair(percentage, 70.0, 90.0);
    attron(COLOR_PAIR(color));
    
    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            mvaddch(y, x + 1 + i, ACS_CKBOARD);
        } else {
            mvaddch(y, x + 1 + i, ' ');
        }
    }
    
    attroff(COLOR_PAIR(color));
    mvaddch(y, x + width + 1, ']');
    
    // Draw label
    if (!label.empty()) {
        mvprintw(y, x + width + 3, "%s", label.c_str());
    }
}

void Dashboard::drawBox(int y, int x, int height, int width, const std::string& title) {
    // Draw border
    mvhline(y, x, ACS_HLINE, width);
    mvhline(y + height - 1, x, ACS_HLINE, width);
    mvvline(y, x, ACS_VLINE, height);
    mvvline(y, x + width - 1, ACS_VLINE, height);
    
    // Corners
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + width - 1, ACS_URCORNER);
    mvaddch(y + height - 1, x, ACS_LLCORNER);
    mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
    
    // Title
    if (!title.empty()) {
        attron(A_BOLD);
        mvprintw(y, x + 2, " %s ", title.c_str());
        attroff(A_BOLD);
    }
}

std::string Dashboard::formatBytes(size_t bytes) const {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double size = static_cast<double>(bytes);
    
    while (size >= 1024.0 && unit_idx < 4) {
        size /= 1024.0;
        unit_idx++;
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << size << " " << units[unit_idx];
    return ss.str();
}

std::string Dashboard::formatPercentage(double value) const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << value << "%";
    return ss.str();
}

int Dashboard::getColorPair(double value, double warning, double critical) const {
    if (value >= critical) {
        return UIColors::CRITICAL;
    } else if (value >= warning) {
        return UIColors::WARNING;
    } else {
        return UIColors::SUCCESS;
    }
}

void Dashboard::handleInput(int ch) {
    switch (ch) {
        // View switching
        case '1':
            current_view_ = DashboardView::OVERVIEW;
            break;
        case '2':
            current_view_ = DashboardView::PROCESSES;
            selected_row_ = 0;
            scroll_offset_ = 0;
            break;
        case '3':
            current_view_ = DashboardView::MEMORY;
            break;
        case '4':
            current_view_ = DashboardView::PERFORMANCE;
            break;
        case '5':
            current_view_ = DashboardView::MODES;
            break;
        
        // Navigation
        case KEY_UP:
            if (selected_row_ > 0) {
                selected_row_--;
                if (selected_row_ < scroll_offset_) {
                    scroll_offset_ = selected_row_;
                }
            }
            break;
        
        case KEY_DOWN:
            if (selected_row_ < cached_processes_.size() - 1) {
                selected_row_++;
                int max_visible = screen_height_ - 10;
                if (selected_row_ >= scroll_offset_ + max_visible) {
                    scroll_offset_ = selected_row_ - max_visible + 1;
                }
            }
            break;
        
        case KEY_PPAGE: // Page Up
            scroll_offset_ = std::max(0, scroll_offset_ - 10);
            selected_row_ = scroll_offset_;
            break;
        
        case KEY_NPAGE: // Page Down
            scroll_offset_ = std::min(static_cast<int>(cached_processes_.size()) - 1, 
                                     scroll_offset_ + 10);
            selected_row_ = scroll_offset_;
            break;
        
        // Mode switching
        case 'g':
        case 'G':
            mode_manager_->switchToMode(SystemMode::GAMING);
            setStatusMessage("Switched to Gaming mode");
            break;
        
        case 'p':
        case 'P':
            mode_manager_->switchToMode(SystemMode::PRODUCTIVITY);
            setStatusMessage("Switched to Productivity mode");
            break;
        
        case 's':
        case 'S':
            mode_manager_->switchToMode(SystemMode::POWER_SAVING);
            setStatusMessage("Switched to Power Saving mode");
            break;
        
        case 'b':
        case 'B':
            mode_manager_->switchToMode(SystemMode::BALANCED);
            setStatusMessage("Switched to Balanced mode");
            break;
        
        // Process actions
        case 'k':
        case 'K':
            if (current_view_ == DashboardView::PROCESSES && 
                selected_row_ < cached_processes_.size()) {
                int pid = cached_processes_[selected_row_].pid;
                if (process_manager_->pauseProcess(pid)) {
                    setStatusMessage("Process paused: PID " + std::to_string(pid));
                } else {
                    setStatusMessage("Failed to pause process");
                }
            }
            break;
        
        case 't':
        case 'T':
            if (current_view_ == DashboardView::PROCESSES && 
                selected_row_ < cached_processes_.size()) {
                int pid = cached_processes_[selected_row_].pid;
                if (process_manager_->terminateProcess(pid)) {
                    setStatusMessage("Process terminated: PID " + std::to_string(pid));
                } else {
                    setStatusMessage("Failed to terminate process");
                }
            }
            break;
        
        case 'r':
        case 'R':
            if (current_view_ == DashboardView::PROCESSES && 
                selected_row_ < cached_processes_.size()) {
                int pid = cached_processes_[selected_row_].pid;
                if (process_manager_->resumeProcess(pid)) {
                    setStatusMessage("Process resumed: PID " + std::to_string(pid));
                } else {
                    setStatusMessage("Failed to resume process");
                }
            }
            break;
        
        // Memory actions
        case 'm':
        case 'M':
            {
                size_t freed = memory_manager_->optimizeSystemMemory();
                setStatusMessage("Memory optimized, freed: " + formatBytes(freed * 1024));
            }
            break;
        
        case 'c':
        case 'C':
            {
                size_t freed = memory_manager_->clearAllCaches();
                setStatusMessage("Caches cleared, freed: " + formatBytes(freed * 1024));
            }
            break;
        
        // Help
        case '?':
            show_help_ = !show_help_;
            break;
        
        // Quit
        case 'q':
        case 'Q':
        case 27: // ESC
            running_.store(false);
            break;
    }
}

void Dashboard::refreshData() {
    // Get system statistics
    cached_system_stats_ = system_monitor_->getSystemStatistics();
    
    // Get process list
    cached_processes_ = system_monitor_->getProcesses();
    
    // Sort processes by CPU usage
    std::sort(cached_processes_.begin(), cached_processes_.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.cpu_usage > b.cpu_usage;
              });
}

void Dashboard::setUpdateInterval(std::chrono::milliseconds interval) {
    update_interval_ = interval;
}

void Dashboard::setStatusMessage(const std::string& message) {
    status_message_ = message;
    
    // Clear status message after 5 seconds
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        status_message_.clear();
    }).detach();
}

// ========================================
// Alternative: Simple Console Dashboard (No ncurses)
// ========================================

#ifdef NO_NCURSES

// SimpleDashboard.h
class SimpleDashboard {
private:
    std::shared_ptr<SystemMonitor> system_monitor_;
    std::shared_ptr<ProcessManager> process_manager_;
    std::shared_ptr<ModeManager> mode_manager_;
    std::atomic<bool> running_;
    
public:
    SimpleDashboard(std::shared_ptr<SystemMonitor> monitor,
                    std::shared_ptr<ProcessManager> process_manager,
                    std::shared_ptr<ModeManager> mode_manager);
    
    void show();
    void displayOverview();
    void displayProcesses();
    void clearScreen();
};

// SimpleDashboard.cpp
SimpleDashboard::SimpleDashboard(std::shared_ptr<SystemMonitor> monitor,
                                 std::shared_ptr<ProcessManager> process_manager,
                                 std::shared_ptr<ModeManager> mode_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , mode_manager_(mode_manager)
    , running_(true) {
}

void SimpleDashboard::clearScreen() {
    // ANSI escape codes to clear screen
    std::cout << "\033[2J\033[1;1H";
}

void SimpleDashboard::displayOverview() {
    clearScreen();
    
    auto stats = system_monitor_->getSystemStatistics();
    
    std::cout << "╔════════════════════════════════════════════════╗\n";
    std::cout << "║     Smart Resource Scheduler - Overview       ║\n";
    std::cout << "╚════════════════════════════════════════════════╝\n\n";
    
    // Current Mode
    std::cout << "Current Mode: ";
    switch (mode_manager_->getCurrentMode()) {
        case SystemMode::GAMING: std::cout << "Gaming\n"; break;
        case SystemMode::PRODUCTIVITY: std::cout << "Productivity\n"; break;
        case SystemMode::POWER_SAVING: std::cout << "Power Saving\n"; break;
        default: std::cout << "Balanced\n"; break;
    }
    
    std::cout << "\n";
    
    // System Stats
    std::cout << "CPU Usage:    " << std::fixed << std::setprecision(1) 
              << stats.cpu_usage_total << "%\n";
    
    double mem_usage = 100.0 * stats.memory_used_kb / stats.memory_total_kb;
    std::cout << "Memory Usage: " << std::fixed << std::setprecision(1) 
              << mem_usage << "%\n";
    
    std::cout << "CPU Cores:    " << stats.cpu_core_count << "\n";
    std::cout << "Load Average: " << stats.load_average_1min << ", "
              << stats.load_average_5min << ", "
              << stats.load_average_15min << "\n";
    
    std::cout << "\n";
    
    // Top Processes
    std::cout << "Top CPU Processes:\n";
    std::cout << "──────────────────────────────────────────────\n";
    
    auto top_cpu = system_monitor_->getTopCpuProcesses(5);
    for (const auto& proc : top_cpu) {
        std::cout << std::setw(25) << std::left << proc.name
                  << std::setw(10) << std::right 
                  << std::fixed << std::setprecision(1) << proc.cpu_usage << "%\n";
    }
    
    std::cout << "\n";
    std::cout << "Press 'q' to quit, 'h' for help\n";
}

void SimpleDashboard::show() {
    while (running_.load()) {
        displayOverview();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

#endif
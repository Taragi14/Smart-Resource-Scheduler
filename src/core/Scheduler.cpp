#include "Scheduler.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <cmath>

Scheduler::Scheduler(std::shared_ptr<SystemMonitor> monitor, 
                     std::shared_ptr<ProcessManager> process_manager)
    : system_monitor_(monitor)
    , process_manager_(process_manager)
    , current_algorithm_(SchedulingAlgorithm::PRIORITY_BASED)
    , scheduler_active_(false)
    , default_time_slice_(std::chrono::milliseconds(100))
    , min_time_slice_(std::chrono::milliseconds(10))
    , max_time_slice_(std::chrono::milliseconds(500))
    , max_queue_levels_(5)
    , priority_boost_threshold_(5.0)
    , adaptive_scheduling_enabled_(true)
    , load_balancing_enabled_(false)
    , priority_inheritance_enabled_(true)
    , priority_boosting_enabled_(true)
    , scheduling_debug_enabled_(false)
    , preemption_threshold_(0.8)
    , starvation_threshold_(std::chrono::milliseconds(5000))
    , priority_queue_([](const auto& a, const auto& b) { 
        return a->dynamic_priority < b->dynamic_priority; 
      })
    , cfs_tree_([](const auto& a, const auto& b) { 
        return a->virtual_runtime < b->virtual_runtime; 
      }) {
    
    // Initialize statistics
    std::memset(&current_stats_, 0, sizeof(SchedulingStats));
    current_stats_.current_algorithm = current_algorithm_;
    current_stats_.measurement_start = std::chrono::steady_clock::now();
    
    // Initialize per-class configurations
    initializeQueues();
    
    // Default class configurations
    class_configs_[ProcessClass::INTERACTIVE] = {
        SchedulingAlgorithm::PRIORITY_BASED,
        std::chrono::milliseconds(50),
        -10, 10
    };
    
    class_configs_[ProcessClass::BATCH] = {
        SchedulingAlgorithm::ROUND_ROBIN,
        std::chrono::milliseconds(200),
        5, 15
    };
    
    class_configs_[ProcessClass::REAL_TIME] = {
        SchedulingAlgorithm::PRIORITY_BASED,
        std::chrono::milliseconds(20),
        -20, -10
    };
}

Scheduler::~Scheduler() {
    stopScheduler();
}

bool Scheduler::startScheduler() {
    if (scheduler_active_.load()) {
        return false;
    }
    
    scheduler_active_.store(true);
    scheduler_thread_ = std::thread(&Scheduler::schedulingLoop, this);
    
    logSchedulingDecision("Scheduler started");
    return true;
}

void Scheduler::stopScheduler() {
    if (scheduler_active_.load()) {
        scheduler_active_.store(false);
        if (scheduler_thread_.joinable()) {
            scheduler_thread_.join();
        }
        logSchedulingDecision("Scheduler stopped");
    }
}

void Scheduler::schedulingLoop() {
    auto last_schedule = std::chrono::steady_clock::now();
    
    while (scheduler_active_.load()) {
        auto cycle_start = std::chrono::steady_clock::now();
        
        try {
            // Update process information from system monitor
            auto all_processes = system_monitor_->getProcesses();
            
            // Add new processes to scheduler
            for (const auto& proc_info : all_processes) {
                if (scheduled_processes_.find(proc_info.pid) == scheduled_processes_.end()) {
                    addProcess(proc_info.pid);
                }
            }
            
            // Remove dead processes
            auto it = scheduled_processes_.begin();
            while (it != scheduled_processes_.end()) {
                if (!system_monitor_->isProcessRunning(it->first)) {
                    it = scheduled_processes_.erase(it);
                } else {
                    ++it;
                }
            }
            
            // Update process states
            for (auto& [pid, proc] : scheduled_processes_) {
                updateProcessState(proc);
            }
            
            // Age priorities to prevent starvation
            if (priority_boosting_enabled_) {
                boostStarvingProcesses();
            }
            
            // Select and schedule next process
            auto next_process = selectNextProcess();
            if (next_process) {
                // Apply scheduling decision
                if (current_running_process_ && 
                    current_running_process_->pid != next_process->pid) {
                    
                    // Context switch needed
                    preemptCurrentProcess();
                    recordContextSwitch();
                }
                
                current_running_process_ = next_process;
                
                // Apply priority to actual process
                process_manager_->setProcessPriority(
                    next_process->pid, 
                    static_cast<ProcessPriority>(next_process->dynamic_priority)
                );
                
                next_process->last_scheduled = std::chrono::steady_clock::now();
                next_process->schedule_count++;
                
                logSchedulingDecision("Scheduled process", next_process->pid);
                notifyScheduleEvent(next_process->pid, "scheduled");
            }
            
            // Adaptive scheduling adjustments
            if (adaptive_scheduling_enabled_) {
                adaptToSystemLoad();
            }
            
            // Update statistics
            updateSchedulingStats();
            
            // Load balancing (if multi-core)
            if (load_balancing_enabled_) {
                balanceProcessLoad();
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Scheduler error: " << e.what() << std::endl;
        }
        
        // Calculate sleep time based on scheduling frequency
        auto cycle_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            cycle_end - cycle_start);
        
        auto sleep_time = std::chrono::milliseconds(50) - elapsed;
        if (sleep_time > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(sleep_time);
        }
    }
}

std::shared_ptr<ScheduledProcess> Scheduler::selectNextProcess() {
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    
    // Check for real-time processes first
    for (const auto& [pid, rt_priority] : realtime_processes_) {
        auto it = scheduled_processes_.find(pid);
        if (it != scheduled_processes_.end() && 
            system_monitor_->isProcessRunning(pid)) {
            return it->second;
        }
    }
    
    // Select based on current algorithm
    switch (current_algorithm_) {
        case SchedulingAlgorithm::PRIORITY_BASED:
            return getNextPriorityProcess();
        
        case SchedulingAlgorithm::ROUND_ROBIN:
            return getNextRoundRobinProcess();
        
        case SchedulingAlgorithm::MULTILEVEL_FEEDBACK:
            return getNextMultilevelProcess();
        
        case SchedulingAlgorithm::COMPLETELY_FAIR:
            return getNextCFSProcess();
        
        case SchedulingAlgorithm::CUSTOM_HYBRID:
            return getNextPriorityProcess(); // Fallback to priority
        
        default:
            return nullptr;
    }
}

std::shared_ptr<ScheduledProcess> Scheduler::getNextPriorityProcess() {
    if (scheduled_processes_.empty()) {
        return nullptr;
    }
    
    // Find process with highest dynamic priority
    std::shared_ptr<ScheduledProcess> best_process = nullptr;
    int highest_priority = std::numeric_limits<int>::min();
    
    for (auto& [pid, proc] : scheduled_processes_) {
        if (system_monitor_->isProcessRunning(pid)) {
            int priority = calculateDynamicPriority(*proc);
            if (priority > highest_priority) {
                highest_priority = priority;
                best_process = proc;
            }
        }
    }
    
    return best_process;
}

std::shared_ptr<ScheduledProcess> Scheduler::getNextRoundRobinProcess() {
    if (round_robin_queue_.empty()) {
        // Rebuild queue from scheduled processes
        for (auto& [pid, proc] : scheduled_processes_) {
            if (system_monitor_->isProcessRunning(pid)) {
                round_robin_queue_.push_back(proc);
            }
        }
    }
    
    if (round_robin_queue_.empty()) {
        return nullptr;
    }
    
    // Get next process from queue
    auto next = round_robin_queue_.front();
    round_robin_queue_.pop_front();
    
    // Add back to end of queue
    round_robin_queue_.push_back(next);
    
    return next;
}

std::shared_ptr<ScheduledProcess> Scheduler::getNextMultilevelProcess() {
    // Check queues from highest to lowest priority
    for (int level = 0; level < max_queue_levels_; ++level) {
        if (!multilevel_queues_[level].empty()) {
            auto next = multilevel_queues_[level].front();
            multilevel_queues_[level].pop_front();
            
            // Move to lower priority queue if it has used its time slice
            if (next->schedule_count > (level + 1) * 3) {
                int new_level = std::min(level + 1, max_queue_levels_ - 1);
                multilevel_queues_[new_level].push_back(next);
                next->queue_level = new_level;
            } else {
                // Put back in same queue
                multilevel_queues_[level].push_back(next);
            }
            
            return next;
        }
    }
    
    return nullptr;
}

std::shared_ptr<ScheduledProcess> Scheduler::getNextCFSProcess() {
    if (scheduled_processes_.empty()) {
        return nullptr;
    }
    
    // Find process with minimum virtual runtime
    std::shared_ptr<ScheduledProcess> min_vruntime_proc = nullptr;
    double min_vruntime = std::numeric_limits<double>::max();
    
    for (auto& [pid, proc] : scheduled_processes_) {
        if (system_monitor_->isProcessRunning(pid)) {
            double vruntime = calculateVirtualRuntime(*proc);
            if (vruntime < min_vruntime) {
                min_vruntime = vruntime;
                min_vruntime_proc = proc;
            }
        }
    }
    
    // Update virtual runtime
    if (min_vruntime_proc) {
        auto proc_info = system_monitor_->getProcess(min_vruntime_proc->pid);
        min_vruntime_proc->virtual_runtime += proc_info.cpu_usage * 0.1;
    }
    
    return min_vruntime_proc;
}

void Scheduler::addProcess(int pid) {
    if (scheduled_processes_.find(pid) != scheduled_processes_.end()) {
        return;
    }
    
    auto proc_info = system_monitor_->getProcess(pid);
    if (proc_info.pid == -1) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    
    auto scheduled_proc = std::make_shared<ScheduledProcess>(pid, proc_info.name);
    scheduled_proc->base_priority = proc_info.priority;
    scheduled_proc->dynamic_priority = proc_info.priority;
    scheduled_proc->nice_value = proc_info.priority;
    
    classifyProcess(scheduled_proc);
    calculateTimeSlice(scheduled_proc);
    
    scheduled_processes_[pid] = scheduled_proc;
    
    // Add to appropriate queue based on algorithm
    switch (current_algorithm_) {
        case SchedulingAlgorithm::ROUND_ROBIN:
            round_robin_queue_.push_back(scheduled_proc);
            break;
        
        case SchedulingAlgorithm::MULTILEVEL_FEEDBACK:
            multilevel_queues_[0].push_back(scheduled_proc);
            scheduled_proc->queue_level = 0;
            break;
        
        default:
            break;
    }
    
    logSchedulingDecision("Added process to scheduler", pid);
}

void Scheduler::removeProcess(int pid) {
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    
    auto it = scheduled_processes_.find(pid);
    if (it != scheduled_processes_.end()) {
        // Remove from current running if needed
        if (current_running_process_ && current_running_process_->pid == pid) {
            current_running_process_ = nullptr;
        }
        
        scheduled_processes_.erase(it);
        logSchedulingDecision("Removed process from scheduler", pid);
    }
}

void Scheduler::classifyProcess(std::shared_ptr<ScheduledProcess> process) {
    auto proc_info = system_monitor_->getProcess(process->pid);
    if (proc_info.pid == -1) {
        process->process_class = ProcessClass::BATCH;
        return;
    }
    
    // Classify based on name and behavior
    process->process_class = classifyProcessByName(proc_info.name);
    
    // Adjust based on CPU usage patterns
    if (proc_info.cpu_usage > 80.0) {
        process->process_class = ProcessClass::BATCH;
    } else if (proc_info.cpu_usage < 5.0) {
        process->process_class = ProcessClass::IDLE;
    }
}

ProcessClass Scheduler::classifyProcessByName(const std::string& name) const {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    // System processes
    if (lower_name.find("systemd") != std::string::npos ||
        lower_name.find("kernel") != std::string::npos) {
        return ProcessClass::SYSTEM;
    }
    
    // Interactive processes
    if (lower_name.find("x") != std::string::npos ||
        lower_name.find("gnome") != std::string::npos ||
        lower_name.find("kde") != std::string::npos ||
        lower_name.find("browser") != std::string::npos ||
        lower_name.find("game") != std::string::npos) {
        return ProcessClass::INTERACTIVE;
    }
    
    // Batch processes
    if (lower_name.find("build") != std::string::npos ||
        lower_name.find("compile") != std::string::npos ||
        lower_name.find("backup") != std::string::npos) {
        return ProcessClass::BATCH;
    }
    
    return ProcessClass::INTERACTIVE; // Default
}

void Scheduler::calculateTimeSlice(std::shared_ptr<ScheduledProcess> process) {
    // Base time slice on process class
    switch (process->process_class) {
        case ProcessClass::REAL_TIME:
            process->time_slice = std::chrono::milliseconds(20);
            break;
        
        case ProcessClass::INTERACTIVE:
            process->time_slice = std::chrono::milliseconds(50);
            break;
        
        case ProcessClass::BATCH:
            process->time_slice = std::chrono::milliseconds(200);
            break;
        
        case ProcessClass::SYSTEM:
            process->time_slice = std::chrono::milliseconds(100);
            break;
        
        case ProcessClass::IDLE:
            process->time_slice = std::chrono::milliseconds(500);
            break;
    }
    
    // Clamp to min/max
    process->time_slice = std::clamp(process->time_slice, 
                                     min_time_slice_, max_time_slice_);
}

int Scheduler::calculateDynamicPriority(const ScheduledProcess& process) const {
    int priority = process.base_priority;
    
    // Boost interactive processes
    if (process.process_class == ProcessClass::INTERACTIVE) {
        priority += 5;
    }
    
    // Penalize CPU hogs
    auto proc_info = system_monitor_->getProcess(process.pid);
    if (proc_info.cpu_usage > 80.0) {
        priority -= 3;
    }
    
    // Boost starving processes
    auto now = std::chrono::steady_clock::now();
    auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - process.last_scheduled);
    
    if (wait_time > starvation_threshold_) {
        priority += 10; // Significant boost
    }
    
    return priority;
}

double Scheduler::calculateVirtualRuntime(const ScheduledProcess& process) const {
    double base_vruntime = process.virtual_runtime;
    
    // Weight by nice value (higher nice = lower priority = higher vruntime growth)
    double weight = 1.0 / (1.0 + process.nice_value / 20.0);
    
    return base_vruntime * weight;
}

void Scheduler::updateProcessState(std::shared_ptr<ScheduledProcess> process) {
    auto proc_info = system_monitor_->getProcess(process->pid);
    if (proc_info.pid == -1) {
        return;
    }
    
    // Update CPU usage history
    process->cpu_usage_history.push_back(proc_info.cpu_usage);
    if (process->cpu_usage_history.size() > 10) {
        process->cpu_usage_history.pop_front();
    }
    
    // Update priorities periodically
    process->dynamic_priority = calculateDynamicPriority(*process);
}

void Scheduler::boostStarvingProcesses() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto& [pid, proc] : scheduled_processes_) {
        auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - proc->last_scheduled);
        
        if (wait_time > starvation_threshold_) {
            proc->dynamic_priority = std::min(proc->dynamic_priority + 5, 19);
            logSchedulingDecision("Boosted starving process", pid);
        }
    }
}

void Scheduler::preemptCurrentProcess() {
    if (!current_running_process_) {
        return;
    }
    
    current_running_process_->preemption_count++;
    recordPreemption();
    
    logSchedulingDecision("Preempted process", current_running_process_->pid);
}

void Scheduler::initializeQueues() {
    multilevel_queues_.resize(max_queue_levels_);
    round_robin_queue_.clear();
}

void Scheduler::updateSchedulingStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    current_stats_.active_processes = scheduled_processes_.size();
    current_stats_.current_algorithm = current_algorithm_;
    
    auto system_stats = system_monitor_->getSystemStatistics();
    current_stats_.cpu_utilization = system_stats.cpu_usage_total;
    
    // Calculate average metrics
    if (!scheduled_processes_.empty()) {
        double total_response_time = 0.0;
        for (const auto& [pid, proc] : scheduled_processes_) {
            total_response_time += proc->average_response_time;
        }
        current_stats_.average_response_time = 
            total_response_time / scheduled_processes_.size();
    }
}

void Scheduler::recordContextSwitch() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    current_stats_.total_context_switches++;
}

void Scheduler::recordPreemption() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    current_stats_.total_preemptions++;
}

void Scheduler::adaptToSystemLoad() {
    auto system_stats = system_monitor_->getSystemStatistics();
    
    // If system is heavily loaded, reduce time slices for better responsiveness
    if (system_stats.cpu_usage_total > 80.0) {
        for (auto& [pid, proc] : scheduled_processes_) {
            if (proc->process_class != ProcessClass::REAL_TIME) {
                proc->time_slice = std::max(
                    min_time_slice_,
                    proc->time_slice * 80 / 100
                );
            }
        }
    }
}

void Scheduler::setSchedulingAlgorithm(SchedulingAlgorithm algorithm) {
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    
    current_algorithm_ = algorithm;
    clearQueues();
    initializeQueues();
    
    // Re-add all processes to new queues
    for (auto& [pid, proc] : scheduled_processes_) {
        if (algorithm == SchedulingAlgorithm::MULTILEVEL_FEEDBACK) {
            multilevel_queues_[0].push_back(proc);
            proc->queue_level = 0;
        } else if (algorithm == SchedulingAlgorithm::ROUND_ROBIN) {
            round_robin_queue_.push_back(proc);
        }
    }
    
    logSchedulingDecision("Changed scheduling algorithm");
}

void Scheduler::clearQueues() {
    round_robin_queue_.clear();
    for (auto& queue : multilevel_queues_) {
        queue.clear();
    }
}

void Scheduler::optimizeForMode(const std::string& mode) {
    if (mode == "gaming") {
        setSchedulingAlgorithm(SchedulingAlgorithm::PRIORITY_BASED);
        setDefaultTimeSlice(std::chrono::milliseconds(50));
        priority_boosting_enabled_.store(true);
        
    } else if (mode == "productivity") {
        setSchedulingAlgorithm(SchedulingAlgorithm::COMPLETELY_FAIR);
        setDefaultTimeSlice(std::chrono::milliseconds(100));
        
    } else if (mode == "power_saving") {
        setSchedulingAlgorithm(SchedulingAlgorithm::ROUND_ROBIN);
        setDefaultTimeSlice(std::chrono::milliseconds(200));
    }
    
    logSchedulingDecision("Optimized for mode: " + mode);
}

SchedulingStats Scheduler::getSchedulingStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return current_stats_;
}

void Scheduler::logSchedulingDecision(const std::string& decision, int pid) {
    if (!scheduling_debug_enabled_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(debug_mutex_);
    debug_log_ << "[" << std::chrono::system_clock::now().time_since_epoch().count() << "] ";
    debug_log_ << decision;
    if (pid != -1) {
        debug_log_ << " (PID: " << pid << ")";
    }
    debug_log_ << std::endl;
}

void Scheduler::notifyScheduleEvent(int pid, const std::string& event) {
    for (const auto& callback : schedule_callbacks_) {
        callback(pid, event);
    }
}

void Scheduler::registerScheduleCallback(ScheduleCallback callback) {
    schedule_callbacks_.push_back(callback);
}

void Scheduler::setDefaultTimeSlice(std::chrono::milliseconds time_slice) {
    default_time_slice_ = time_slice;
}

void Scheduler::balanceProcessLoad() {
    // Simple load balancing - distribute processes evenly across cores
    // This is a simplified implementation
    int cpu_count = system_monitor_->getCpuCoreCount();
    if (cpu_count <= 1) {
        return;
    }
    
    // Would implement actual core affinity setting here
    // For now, just log that we're balancing
    logSchedulingDecision("Load balancing across " + std::to_string(cpu_count) + " cores");
}
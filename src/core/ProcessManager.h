#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include "types.h"
#include <vector>
#include <string>

struct ProcessInfo {
    int pid;
    std::string name;
    double cpu_usage;
    long memory_usage;
    int group_id;
};

class ProcessManager {
public:
    void adjustPriorities(const SchedulerConfig& config);
    void pauseProcess(int pid);
    void resumeProcess(int pid);
    void terminateProcess(int pid);
    void setCPUAffinity(int pid, const std::vector<int>& cores);
    void assignToCgroup(int pid, const SchedulerConfig& config);
    std::vector<ProcessInfo> getRunningProcesses();
    void createProcessGroup(int group_id);

private:
    void setPriority(int pid, int priority);
    double calculateCPUUsage(int pid);
    long getProcessMemory(int pid);
};

#endif
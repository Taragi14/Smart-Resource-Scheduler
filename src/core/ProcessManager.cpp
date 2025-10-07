#include "ProcessManager.h"
#include "Logger.h"
#include "ProcessLock.h"
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <signal.h>
#include <sched.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

void ProcessManager::adjustPriorities(const SchedulerConfig& config) {
    ProcessLock lock;
    auto processes = getRunningProcesses();
    for (const auto& proc : processes) {
        lock.lock(proc.pid);
        int priority = (proc.cpu_usage > 50.0) ? config.priority_high : config.priority_low;
        setPriority(proc.pid, priority);
        setCPUAffinity(proc.pid, config.cpu_affinity_cores);
        assignToCgroup(proc.pid, config);
        lock.unlock(proc.pid);
        Logger::log("Adjusted PID " + std::to_string(proc.pid) + " priority to " + std::to_string(priority));
    }
}

void ProcessManager::setPriority(int pid, int priority) {
    if (setpriority(PRIO_PROCESS, pid, priority) != -1) {
        Logger::log("Set priority of PID " + std::to_string(pid) + " to " + std::to_string(priority));
    }
}

void ProcessManager::setCPUAffinity(int pid, const std::vector<int>& cores) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int core : cores) {
        CPU_SET(core, &cpuset);
    }
    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) == 0) {
        Logger::log("Set CPU affinity for PID " + std::to_string(pid));
    }
}

void ProcessManager::assignToCgroup(int pid, const SchedulerConfig& config) {
    std::string cgroup_path = "/sys/fs/cgroup/cpu/smart_scheduler";
    mkdir(cgroup_path.c_str(), 0755);
    std::ofstream cpu_shares(cgroup_path + "/cpu.shares");
    cpu_shares << config.cgroup_cpu_shares;
    cpu_shares.close();
    std::ofstream tasks(cgroup_path + "/tasks");
    tasks << pid;
    tasks.close();
    Logger::log("Assigned PID " + std::to_string(pid) + " to cgroup with " + std::to_string(config.cgroup_cpu_shares) + " shares");
}

void ProcessManager::pauseProcess(int pid) {
    ProcessLock lock;
    lock.lock(pid);
    kill(pid, SIGSTOP);
    lock.unlock(pid);
    Logger::log("Paused PID " + std::to_string(pid));
}

void ProcessManager::resumeProcess(int pid) {
    ProcessLock lock;
    lock.lock(pid);
    kill(pid, SIGCONT);
    lock.unlock(pid);
    Logger::log("Resumed PID " + std::to_string(pid));
}

void ProcessManager::terminateProcess(int pid) {
    ProcessLock lock;
    lock.lock(pid);
    kill(pid, SIGTERM);
    lock.unlock(pid);
    Logger::log("Terminated PID " + std::to_string(pid));
}

void ProcessManager::createProcessGroup(int group_id) {
    std::string cgroup_path = "/sys/fs/cgroup/cpu/smart_scheduler_group_" + std::to_string(group_id);
    mkdir(cgroup_path.c_str(), 0755);
    Logger::log("Created process group: " + std::to_string(group_id));
}

std::vector<ProcessInfo> ProcessManager::getRunningProcesses() {
    std::vector<ProcessInfo> processes;
    DIR* dir = opendir("/proc");
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (std::isdigit(ent->d_name[0])) {
            int pid = std::stoi(ent->d_name);
            ProcessInfo info;
            info.pid = pid;
            info.name = ent->d_name;
            info.cpu_usage = calculateCPUUsage(pid);
            info.memory_usage = getProcessMemory(pid);
            info.group_id = 0; // Simplified group ID
            processes.push_back(info);
        }
    }
    closedir(dir);
    return processes;
}

double ProcessManager::calculateCPUUsage(int pid) {
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat.is_open()) return 0.0;
    std::string line;
    std::getline(stat, line);
    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string token;
    while (iss >> token) tokens.push_back(token);
    long utime = std::stol(tokens[13]);
    long stime = std::stol(tokens[14]);
    stat.close();
    return (utime + stime) / 100.0; // Simplified CPU usage
}

long ProcessManager::getProcessMemory(int pid) {
    std::ifstream statm("/proc/" + std::to_string(pid) + "/statm");
    long size;
    statm >> size;
    statm.close();
    return size * 4; // Pages to KB
}
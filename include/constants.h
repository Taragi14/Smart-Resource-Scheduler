#ifndef CONSTANTS_H
#define CONSTANTS_H

const int MAX_PROCESSES = 1000;
const int MAX_THREADS = 8;
const int MAX_LOG_ENTRIES = 10000;
const std::string LOG_PATH = "logs/performance.log";
const std::string CGROUP_BASE_PATH = "/sys/fs/cgroup/cpu/smart_scheduler";

#endif
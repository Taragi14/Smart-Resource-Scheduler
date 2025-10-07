#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>

struct SchedulerConfig {
    int priority_high;
    int priority_low;
    int time_quantum_ms;
    int memory_threshold_mb;
    std::vector<int> cpu_affinity_cores;
    int cgroup_cpu_shares;
    int cgroup_memory_limit_mb;
    int ipc_queue_size;
};

#endif

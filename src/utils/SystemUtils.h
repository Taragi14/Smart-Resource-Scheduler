#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

#include <string>
#include <vector>

class SystemUtils {
public:
    static std::vector<int> getAvailableCPUCores();
    static std::string getProcessName(int pid);
};

#endif
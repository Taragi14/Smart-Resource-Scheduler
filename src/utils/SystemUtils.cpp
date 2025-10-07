#include "SystemUtils.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <dirent.h>

std::vector<int> SystemUtils::getAvailableCPUCores() {
    std::vector<int> cores;
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    int core_id = 0;
    while (std::getline(cpuinfo, line)) {
        if (line.find("processor") != std::string::npos) {
            cores.push_back(core_id++);
        }
    }
    cpuinfo.close();
    Logger::log("Detected " + std::to_string(cores.size()) + " CPU cores");
    return cores;
}

std::string SystemUtils::getProcessName(int pid) {
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    std::getline(comm, name);
    comm.close();
    return name;
}
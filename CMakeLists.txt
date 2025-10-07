cmake_minimum_required(VERSION 3.10)
project(SmartResourceScheduler)

set(CMAKE_CXX_STANDARD 17)
find_package(Qt5 COMPONENTS Widgets REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

include_directories(src/core src/modes src/synchronization src/logging src/utils src/ui include)
add_executable(scheduler
    src/main.cpp
    src/core/Scheduler.cpp
    src/core/ProcessManager.cpp
    src/core/MemoryManager.cpp
    src/core/SystemMonitor.cpp
    src/core/IPCManager.cpp
    src/modes/ModeManager.cpp
    src/modes/GamingMode.cpp
    src/synchronization/ProcessLock.cpp
    src/synchronization/ThreadPool.cpp
    src/synchronization/Semaphore.cpp
    src/logging/Logger.cpp
    src/logging/PerformanceTracker.cpp
    src/utils/ConfigManager.cpp
    src/ui/Dashboard.cpp
)
target_link_libraries(scheduler Qt5::Widgets ${JSONCPP_LIBRARIES} rt)
add_custom_target(run
    COMMAND ./scheduler
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
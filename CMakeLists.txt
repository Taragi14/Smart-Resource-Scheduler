cmake_minimum_required(VERSION 3.16)
project(SmartResourceScheduler VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Find required packages
find_package(Threads REQUIRED)

# Optional: Find Qt or GTK for UI
option(USE_QT "Build with Qt UI support" OFF)
option(USE_GTK "Build with GTK UI support" OFF)

if(USE_QT)
    find_package(Qt5 COMPONENTS Widgets Core REQUIRED)
    add_definitions(-DUSE_QT)
endif()

if(USE_GTK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    add_definitions(-DUSE_GTK)
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Source files
set(CORE_SOURCES
    src/core/SystemMonitor.cpp
    src/core/ProcessManager.cpp
    src/core/Scheduler.cpp
    src/core/MemoryManager.cpp
)

set(MODE_SOURCES
    src/modes/ModeManager.cpp
)

set(LOGGING_SOURCES
    src/logging/Logger.cpp
    src/logging/PerformanceTracker.cpp
)

set(UTILS_SOURCES
    src/utils/ConfigManager.cpp
)

# Combine all sources
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${MODE_SOURCES}
    ${LOGGING_SOURCES}
    ${UTILS_SOURCES}
    src/main.cpp
)

# Create executable
add_executable(smart_scheduler ${ALL_SOURCES})

# Link libraries
target_link_libraries(smart_scheduler
    Threads::Threads
    rt
)

if(USE_QT)
    target_link_libraries(smart_scheduler Qt5::Widgets Qt5::Core)
endif()

if(USE_GTK)
    target_include_directories(smart_scheduler PRIVATE ${GTK3_INCLUDE_DIRS})
    target_link_libraries(smart_scheduler ${GTK3_LIBRARIES})
endif()

# Installation
install(TARGETS smart_scheduler
    RUNTIME DESTINATION bin
)

install(DIRECTORY config/
    DESTINATION etc/smart_scheduler
)

# Create systemd service file
configure_file(
    ${CMAKE_SOURCE_DIR}/scripts/smart_scheduler.service.in
    ${CMAKE_BINARY_DIR}/smart_scheduler.service
    @ONLY
)

install(FILES ${CMAKE_BINARY_DIR}/smart_scheduler.service
    DESTINATION /etc/systemd/system
)

# Testing
option(BUILD_TESTS "Build unit tests" OFF)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    
    # Test sources
    file(GLOB TEST_SOURCES tests/*.cpp)
    
    add_executable(test_runner ${TEST_SOURCES})
    target_link_libraries(test_runner
        ${ALL_SOURCES}
        GTest::GTest
        GTest::Main
        Threads::Threads
        rt
    )
    
    add_test(NAME SmartSchedulerTests COMMAND test_runner)
endif()

# Documentation
option(BUILD_DOCS "Build documentation" OFF)

if(BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_IN ${CMAKE_SOURCE_DIR}/docs/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_BINARY_DIR}/Doxyfile)
        
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()

# Package creation
set(CPACK_PACKAGE_NAME "smart-resource-scheduler")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Smart Resource Scheduler for Personal PCs")
set(CPACK_PACKAGE_VENDOR "Smart Scheduler Team")
set(CPACK_PACKAGE_CONTACT "team@smartscheduler.com")

# Debian package
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")

# RPM package
set(CPACK_RPM_PACKAGE_REQUIRES "glibc, libstdc++")
set(CPACK_RPM_PACKAGE_GROUP "System Environment/Base")

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "Smart Resource Scheduler Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt UI: ${USE_QT}")
message(STATUS "  GTK UI: ${USE_GTK}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build docs: ${BUILD_DOCS}")
message(STATUS "")
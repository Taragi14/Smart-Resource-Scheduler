// Logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL
};

class Logger {
private:
    std::string log_file_path_;
    std::ofstream log_file_;
    std::mutex log_mutex_;
    LogLevel min_log_level_;
    bool console_output_;
    bool timestamp_enabled_;
    
    std::string getCurrentTimestamp() const;
    std::string levelToString(LogLevel level) const;
    void writeLog(LogLevel level, const std::string& message);

public:
    Logger(const std::string& log_file = "application.log", 
           LogLevel min_level = LogLevel::INFO,
           bool console = true);
    ~Logger();
    
    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void critical(const std::string& message);
    
    void log(LogLevel level, const std::string& message);
    
    void setMinLogLevel(LogLevel level) { min_log_level_ = level; }
    void setConsoleOutput(bool enable) { console_output_ = enable; }
    void setTimestampEnabled(bool enable) { timestamp_enabled_ = enable; }
    
    void flush();
};

#endif
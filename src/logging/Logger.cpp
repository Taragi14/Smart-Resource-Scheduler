#include "Logger.h"
#include <iostream>

Logger::Logger(const std::string& log_file, LogLevel min_level, bool console)
    : log_file_path_(log_file)
    , min_log_level_(min_level)
    , console_output_(console)
    , timestamp_enabled_(true) {
    
    log_file_.open(log_file_path_, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Failed to open log file: " << log_file_path_ << std::endl;
    }
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

std::string Logger::getCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    
    return ss.str();
}

std::string Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void Logger::writeLog(LogLevel level, const std::string& message) {
    if (level < min_log_level_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::stringstream log_entry;
    
    if (timestamp_enabled_) {
        log_entry << "[" << getCurrentTimestamp() << "] ";
    }
    
    log_entry << "[" << levelToString(level) << "] " << message;
    
    // Write to file
    if (log_file_.is_open()) {
        log_file_ << log_entry.str() << std::endl;
    }
    
    // Write to console
    if (console_output_) {
        if (level >= LogLevel::ERROR) {
            std::cerr << log_entry.str() << std::endl;
        } else {
            std::cout << log_entry.str() << std::endl;
        }
    }
}

void Logger::debug(const std::string& message) {
    writeLog(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
    writeLog(LogLevel::INFO, message);
}

void Logger::warning(const std::string& message) {
    writeLog(LogLevel::WARNING, message);
}

void Logger::error(const std::string& message) {
    writeLog(LogLevel::ERROR, message);
}

void Logger::critical(const std::string& message) {
    writeLog(LogLevel::CRITICAL, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    writeLog(level, message);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    if (log_file_.is_open()) {
        log_file_.flush();
    }
}
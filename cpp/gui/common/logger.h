#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

// ============================================================================
// Logger - 单例日志系统
// ============================================================================
class Logger {
public:
    enum Level { LOG_LVL_DEBUG, LOG_LVL_INFO, LOG_LVL_WARNING, LOG_LVL_ERROR };
    
    static Logger& instance();
    
    void setLevel(Level level) { m_level = level; }
    void setFile(const std::string& path);
    void log(Level level, const char* module, const char* fmt, ...);
    void logSimple(Level level, const char* msg);
    void checkRotate();
    
private:
    Logger();
    ~Logger();
    void rotateLogFile();
    
    std::mutex m_mutex;
    std::ofstream m_file;
    std::string m_filePath;
    Level m_level = LOG_LVL_INFO;
};

// Convenience macros for logging
#define LOG_DEBUG(module, fmt, ...) Logger::instance().log(Logger::LOG_LVL_DEBUG, module, fmt, ##__VA_ARGS__)
#define LOG_INFO(module, fmt, ...) Logger::instance().log(Logger::LOG_LVL_INFO, module, fmt, ##__VA_ARGS__)
#define LOG_WARNING(module, fmt, ...) Logger::instance().log(Logger::LOG_LVL_WARNING, module, fmt, ##__VA_ARGS__)
#define LOG_ERROR(module, fmt, ...) Logger::instance().log(Logger::LOG_LVL_ERROR, module, fmt, ##__VA_ARGS__)

#include "logger.h"
#include <cstdarg>
#include <ctime>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) m_file.close();
    m_file.open(path, std::ios::app);
    m_filePath = path;
}

void Logger::log(Level level, const char* module, const char* fmt, ...) {
    if (level < m_level) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Timestamp
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &t);
    
    // Format message
    char msg[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, args);
    va_end(args);
    
    // Output format: [timestamp] [LEVEL] [module] message
    char line[8192];
    const char* levelStr[] = {"DEBUG", "INFO", "WARNING", "ERROR"};
    sprintf_s(line, "[%s] [%s] [%s] %s", timestamp, levelStr[level], module, msg);
    
    // Write to file
    if (m_file.is_open()) {
        m_file << line << std::endl;
        m_file.flush();
    }
    OutputDebugStringA(line);
}

void Logger::logSimple(Level level, const char* msg) {
    log(level, "Main", "%s", msg);
}

void Logger::checkRotate() {
    std::lock_guard<std::mutex> lock(m_mutex);
    rotateLogFile();
}

Logger::Logger() {
    // Default output to %TEMP%\vrclayrics_debug.log
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\vrclayrics_debug.log");
    m_filePath = tempPath;
    m_file.open(m_filePath, std::ios::app);
    rotateLogFile();
}

Logger::~Logger() {
    if (m_file.is_open()) m_file.close();
}

void Logger::rotateLogFile() {
    static const long long MAX_LOG_SIZE = 10 * 1024 * 1024;  // 10MB
    static const int MAX_LOG_FILES = 5;
    
    HANDLE hFile = CreateFileA(m_filePath.c_str(), 
        FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart >= MAX_LOG_SIZE) {
            CloseHandle(hFile);
            if (m_file.is_open()) m_file.close();
            
            // Delete oldest log file (_5)
            char oldPath[MAX_PATH];
            sprintf_s(oldPath, "%s_5", m_filePath.c_str());
            DeleteFileA(oldPath);
            
            // Rotate log files: _4 -> _5, _3 -> _4, _2 -> _3, _1 -> _2
            for (int i = 4; i >= 1; i--) {
                char srcPath[MAX_PATH];
                char dstPath[MAX_PATH];
                sprintf_s(srcPath, "%s_%d", m_filePath.c_str(), i);
                sprintf_s(dstPath, "%s_%d", m_filePath.c_str(), i + 1);
                MoveFileA(srcPath, dstPath);
            }
            
            // Move current log to _1
            char rotatedPath[MAX_PATH];
            sprintf_s(rotatedPath, "%s_1", m_filePath.c_str());
            MoveFileA(m_filePath.c_str(), rotatedPath);
            
            // Reopen file
            m_file.open(m_filePath, std::ios::app);
        } else {
            CloseHandle(hFile);
        }
    }
}

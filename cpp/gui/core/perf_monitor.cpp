// perf_monitor.cpp - 性能监控实现
// Windows 头文件包含顺序很重要
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dxgi1_4.h>
#include <wbemidl.h>
#include <psapi.h>

#include "perf_monitor.h"
#include "../common/logger.h"

// 全局退出标志（用于线程同步）
extern std::atomic<bool> g_threadRunning[];

// ============================================================================
// PerformanceMonitor 方法实现
// ============================================================================

void PerformanceMonitor::start() {
    if (m_running.exchange(true)) {
        return;  // 已经在运行
    }
    
    m_thread = std::thread(&PerformanceMonitor::workerThread, this);
    LOG_INFO("PerformanceMonitor", "Started");
}

void PerformanceMonitor::stop() {
    if (!m_running.exchange(false)) {
        return;  // 已经停止
    }
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    // 清理资源
    if (m_pDXGIAdapter) {
        m_pDXGIAdapter->Release();
        m_pDXGIAdapter = nullptr;
    }
    if (m_pDXGIFactory) {
        m_pDXGIFactory->Release();
        m_pDXGIFactory = nullptr;
    }
    if (m_pLhmServices) {
        m_pLhmServices->Release();
        m_pLhmServices = nullptr;
    }
    if (m_pWmiServices) {
        m_pWmiServices->Release();
        m_pWmiServices = nullptr;
    }
    if (m_pWmiLocator) {
        m_pWmiLocator->Release();
        m_pWmiLocator = nullptr;
    }
    
    LOG_INFO("PerformanceMonitor", "Stopped");
}

void PerformanceMonitor::workerThread() {
    // 这个方法目前未实现，使用全局函数 WorkerThread_PerfMonitor 代替
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ============================================================================
// 线程安全的数据获取方法
// ============================================================================

int PerformanceMonitor::getCpuUsage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.cpuUsage;
}

int PerformanceMonitor::getRamUsage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.ramUsage;
}

int PerformanceMonitor::getGpuUsage() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.gpuUsage;
}

int PerformanceMonitor::getCpuTemp() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.cpuTemp;
}

DWORD64 PerformanceMonitor::getRamUsed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.ramUsed;
}

DWORD64 PerformanceMonitor::getRamTotal() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.ramTotal;
}

DWORD64 PerformanceMonitor::getGpuVramUsed() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.gpuVramUsed;
}

DWORD64 PerformanceMonitor::getGpuVramTotal() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.gpuVramTotal;
}

bool PerformanceMonitor::isCpuTempAvailable() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.cpuTempValid && m_data.cpuTemp > 0;
}

bool PerformanceMonitor::isGpuUsageAvailable() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data.gpuUsageValid;
}

PerfData PerformanceMonitor::getSnapshot() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_data;
}

// ============================================================================
// 更新数据方法
// ============================================================================

void PerformanceMonitor::updateCpuUsage(int usage) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.cpuUsage = usage;
}

void PerformanceMonitor::updateRamData(DWORD64 used, DWORD64 total) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.ramUsed = used;
    m_data.ramTotal = total;
    m_data.ramUsage = total > 0 ? (int)((total - used) * 100 / total) : 0;
}

void PerformanceMonitor::updateGpuData(int usage, bool valid, DWORD64 vramUsed, DWORD64 vramTotal) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.gpuUsage = usage;
    m_data.gpuUsageValid = valid;
    if (vramTotal > 0) m_data.gpuVramTotal = vramTotal;
    if (vramUsed > 0) m_data.gpuVramUsed = vramUsed;
}

void PerformanceMonitor::updateCpuTemp(int temp, bool valid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_data.cpuTemp = temp;
    m_data.cpuTempValid = valid;
}

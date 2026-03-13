#pragma once

// Windows 头文件包含顺序很重要
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <dxgi1_4.h>
#include <wbemidl.h>

#include <thread>
#include <mutex>
#include <atomic>
#include "../common/types.h"

#pragma comment(lib, "wbemuuid.lib")

// ============================================================================
// PerformanceMonitor - 统一性能监控管理
// ============================================================================
class PerformanceMonitor {
public:
    static PerformanceMonitor& instance() {
        static PerformanceMonitor pm;
        return pm;
    }
    
    // 生命周期
    void start();
    void stop();
    
    // 线程安全的数据获取
    int getCpuUsage();
    int getRamUsage();
    int getGpuUsage();
    int getCpuTemp();
    DWORD64 getRamUsed();
    DWORD64 getRamTotal();
    DWORD64 getGpuVramUsed();
    DWORD64 getGpuVramTotal();
    
    // 可用性查询
    bool isCpuTempAvailable();
    bool isGpuUsageAvailable();
    
    // 获取完整数据快照
    PerfData getSnapshot();
    
    // 更新数据（内部使用）
    void updateCpuUsage(int usage);
    void updateRamData(DWORD64 used, DWORD64 total);
    void updateGpuData(int usage, bool valid, DWORD64 vramUsed = 0, DWORD64 vramTotal = 0);
    void updateCpuTemp(int temp, bool valid);
    
    bool isRunning() const { return m_running; }
    
private:
    PerformanceMonitor() = default;
    ~PerformanceMonitor() { stop(); }
    
    void workerThread();
    
    std::thread m_thread;
    std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    
    PerfData m_data;
    
    // DXGI 资源
    IDXGIFactory1* m_pDXGIFactory = nullptr;
    IDXGIAdapter3* m_pDXGIAdapter = nullptr;
    bool m_dxgiInitialized = false;
    
    // WMI 资源
    IWbemLocator* m_pWmiLocator = nullptr;
    IWbemServices* m_pWmiServices = nullptr;
    IWbemServices* m_pLhmServices = nullptr;
    bool m_wmiInitialized = false;
    bool m_lhmInitialized = false;
};

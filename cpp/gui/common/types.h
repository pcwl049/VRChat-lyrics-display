#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cmath>

// ============================================================================
// PerfData - 性能数据结构
// ============================================================================
struct PerfData {
    // CPU
    int cpuUsage = 0;
    int cpuTemp = 0;
    bool cpuTempValid = false;

    // RAM
    int ramUsage = 0;
    DWORD64 ramUsed = 0;
    DWORD64 ramTotal = 0;

    // GPU
    int gpuUsage = 0;
    bool gpuUsageValid = false;
    DWORD64 gpuVramUsed = 0;
    DWORD64 gpuVramTotal = 0;
};

// ============================================================================
// Animation - 动画辅助结构
// ============================================================================
struct Animation {
    double value = 0.0, target = 0.0, speed = 0.25;
    void update() { 
        double diff = target - value;
        value += diff * speed; 
        if (fabs(diff) < 0.001) value = target; 
    }
    void setTarget(double t) { target = t; }
    bool isActive() const { return fabs(target - value) > 0.001; }
    void setFromTo(double from, double to) { value = from; target = to; }
};

// ============================================================================
// SmoothValue - 平滑值过渡（用于颜色等）
// ============================================================================
struct SmoothValue {
    double value = 0.0, target = 0.0, speed = 0.15;
    SmoothValue() = default;
    SmoothValue(double v) : value(v), target(v) {}
    void update() { value += (target - value) * speed; }
    void setTarget(double t) { target = t; }
    void setImmediate(double v) { value = target = v; }
    bool isActive() const { return fabs(target - value) > 0.001; }
};

// ============================================================================
// PlatformInfo - 音乐平台信息
// ============================================================================
struct PlatformInfo {
    std::wstring name;           // 显示名称
    std::wstring connectMethod;  // 连接方式: HTTP, SMTC 等
    bool connected = false;
    bool hover = false;
    DWORD lastPlayTime = 0;
};

// ============================================================================
// SubModuleInfo - 性能模块子项信息
// ============================================================================
enum SubModuleType {
    SUBMOD_USAGE = 0,    // 使用率
    SUBMOD_TEMP = 1,     // 温度
    SUBMOD_VRAM = 2      // 显存
};

struct SubModuleInfo {
    int type;                   // SubModuleType
    std::wstring name;          // 显示名称
    bool available;             // 是否可用
    bool enabled;               // 是否启用
};

// ============================================================================
// DisplayModule - 显示模块配置
// ============================================================================
struct DisplayModule {
    std::wstring key;          // cpu, gpu, ram
    std::wstring name;         // 显示名称
    bool enabled;              // 是否启用
    bool expanded;             // 是否展开（子菜单）
    std::vector<SubModuleInfo> subModules;
    int enabledCount;          // 已启用的子项数量
};

// ThemeColors 结构体已移至 common/theme.h

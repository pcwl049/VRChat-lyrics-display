#pragma once

#include <windows.h>
#include <vector>

// ============================================================================
// OSC 暂停覆盖层窗口 - 进度条、粒子效果、动画系统
// ============================================================================
// 注：由于此模块与 main_gui.cpp 紧密耦合（依赖大量全局状态），
// 类型定义在此头文件中，实现保留在 main_gui.cpp

// 粒子结构体
struct Particle {
    float x, y;           // 位置
    float vx, vy;         // 速度
    float life;           // 生命值 (0-1)
    float maxLife;        // 最大生命
    int size;             // 大小
    COLORREF color;       // 颜色
};

// 沙漏粒子
struct SandParticle {
    float x, y;
    float vy;
    int size;
    COLORREF color;
};

// 窗口管理函数
void CreateOverlayWindow();
void DestroyOverlayWindow();

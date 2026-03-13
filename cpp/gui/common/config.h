#pragma once

#include <windows.h>
#include <string>
#include "types.h"
#include "theme.h"

// ============================================================================
// 全局配置变量声明
// ============================================================================

// OSC 配置
extern std::wstring g_oscIp;
extern int g_oscPort;
extern int g_moekoePort;
extern bool g_oscEnabled;

// 窗口配置
extern bool g_minimizeToTray;
extern bool g_startMinimized;
extern bool g_showPerfOnPause;
extern bool g_autoUpdate;
extern bool g_showPlatform;
extern bool g_autoStart;
extern bool g_runAsAdmin;

// 显示配置
extern int g_performanceMode;
extern bool g_minimalMode;
extern int g_winW, g_winH;
extern int g_winX, g_winY;

// 设备名称
extern std::wstring g_cpuDisplayName;
extern std::wstring g_ramDisplayName;
extern std::wstring g_gpuDisplayName;

// OSC 暂停热键
extern UINT g_oscPauseHotkey;
extern UINT g_oscPauseHotkeyMods;

// 更新相关
extern std::wstring g_skipVersion;

// 显示模块配置
extern std::vector<DisplayModule> g_displayModules;

// 配置文件路径
extern wchar_t g_configPath[];

// ============================================================================
// 配置函数声明
// ============================================================================
void LoadConfig(const wchar_t* path);
void SaveConfig(const wchar_t* path);
void InitDefaultDisplayModules();

// 自动启动检查
bool CheckAutoStart();
void SetAutoStart(bool enable);

#pragma once

#include <string>
#include <vector>
#include "types.h"

// ============================================================================
// 配置管理函数
// ============================================================================

// 加载配置文件
void LoadConfig(const wchar_t* path);

// 保存配置文件
void SaveConfig(const wchar_t* path);

// 初始化默认显示模块配置
void InitDefaultDisplayModules();

// JSON 字符串转义
std::string JsonEscape(const std::wstring& wstr);

// 加载无歌词提示消息
std::vector<std::wstring> LoadNoLyricMessages(const wchar_t* configPath);

// 自动启动管理
bool CheckAutoStart();
void SetAutoStart(bool enable);

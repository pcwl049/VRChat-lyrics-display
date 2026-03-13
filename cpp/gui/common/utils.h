#pragma once

#include <string>

// ============================================================================
// 通用工具函数
// ============================================================================

// 解析版本号字符串 (如 "1.2.3" -> 10203)
int ParseVersion(const std::wstring& ver);

// 计算 SHA256 哈希值
std::string CalculateSHA256(const wchar_t* filePath);

// 从 Git 配置自动检测仓库地址
std::string GetRepoFromGitConfig();

// 获取 GitHub API URL
std::string GetGitHubApiUrl(const std::string& repo);

// 检测网易云音乐进程是否运行
bool IsNeteaseRunning();

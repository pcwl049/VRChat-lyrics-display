#pragma once

#include <windows.h>
#include <string>

// ============================================================================
// 更新检查模块
// ============================================================================

// 更新相关全局变量
extern std::wstring g_latestVersion;
extern std::wstring g_downloadUrl;
extern std::wstring g_downloadSha256Url;
extern std::wstring g_downloadSha256;
extern std::wstring g_latestChangelog;
extern bool g_updateAvailable;
extern bool g_checkingUpdate;
extern bool g_downloadingUpdate;
extern bool g_manualCheckUpdate;
extern bool g_updateCheckComplete;
extern DWORD g_lastUpdateCheck;
extern int g_downloadProgress;

// 常量
const DWORD UPDATE_CHECK_INTERVAL = 3600000;  // 1 hour

// 更新检查函数
bool CheckForUpdate(bool manualCheck = false);
bool DownloadAndInstallUpdate();

// 辅助函数
std::string GetRepoFromGitConfig();
std::string GetGitHubApiUrl(const std::string& repo);

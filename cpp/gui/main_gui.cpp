// VRChat Lyrics Display - Modern Settings GUI (Borderless + Animations)
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_IE 0x0600

// Version info
#define APP_VERSION "0.4.0-beta"
#define APP_VERSION_NUM 400  // 0.4.0-beta -> 0*10000 + 4*100 + 0 = 400
#define GITHUB_REPO "pcwl049/VRChat-lyrics-display"
#define GITHUB_API_URL "https://api.github.com/repos/pcwl049/VRChat-lyrics-display/releases/latest"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <dwmapi.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wbemidl.h>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <thread>
#pragma comment(lib, "wbemuuid.lib")

// GDI+ for anti-aliased rendering
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

// Log levels
enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
};

static const char* LOG_LEVEL_STR[] = {"DEBUG", "INFO", "WARNING", "ERROR"};

// Log rotation settings
static const long long MAX_LOG_SIZE = 10 * 1024 * 1024;  // 10MB
static const int MAX_LOG_FILES = 5;

// Rotate log files if current log exceeds MAX_LOG_SIZE
static void RotateLogFile(const char* basePath) {
    HANDLE hFile = CreateFileA(basePath, 
        FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart >= MAX_LOG_SIZE) {
            CloseHandle(hFile);
            
            // Delete oldest log file (_5)
            char oldPath[MAX_PATH];
            sprintf_s(oldPath, "%s_5", basePath);
            DeleteFileA(oldPath);
            
            // Rotate log files: _4 -> _5, _3 -> _4, _2 -> _3, _1 -> _2
            for (int i = 4; i >= 1; i--) {
                char srcPath[MAX_PATH];
                char dstPath[MAX_PATH];
                sprintf_s(srcPath, "%s_%d", basePath, i);
                sprintf_s(dstPath, "%s_%d", basePath, i + 1);
                MoveFileA(srcPath, dstPath);
            }
            
            // Move current log to _1
            char rotatedPath[MAX_PATH];
            sprintf_s(rotatedPath, "%s_1", basePath);
            MoveFileA(basePath, rotatedPath);
        } else {
            CloseHandle(hFile);
        }
    }
}

// Debug log using Windows API - write to user temp directory with timestamp and level
static void MainDebugLog(const char* msg, LogLevel level = LOG_INFO) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\vrclayrics_debug.log");
    
    // Check and rotate log file if needed
    RotateLogFile(tempPath);
    
    // Get current timestamp
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &t);
    
    // Format: [2026-03-10 15:30:45] [INFO] message
    char formattedMsg[4096];
    sprintf_s(formattedMsg, "[%s] [%s] %s", timestamp, LOG_LEVEL_STR[level], msg);
    
    HANDLE hFile = CreateFileA(tempPath, 
        FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, formattedMsg, (DWORD)strlen(formattedMsg), &written, NULL);
        WriteFile(hFile, "\r\n", 2, &written, NULL);
        CloseHandle(hFile);
    }
    OutputDebugStringA(formattedMsg);
}

// Convenience macros for logging
#define LOG_DEBUG(msg) MainDebugLog(msg, LOG_DEBUG)
#define LOG_INFO(msg) MainDebugLog(msg, LOG_INFO)
#define LOG_WARNING(msg) MainDebugLog(msg, LOG_WARNING)
#define LOG_ERROR(msg) MainDebugLog(msg, LOG_ERROR)
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <random>
#include <ctime>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "moekoe_ws.h"
#include "netease_ws.h"
#include "smtc_client.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

// 从Git配置自动检测仓库地址（变量定义）
std::string g_autoDetectedRepo = "";

// 从Git配置自动检测仓库地址（函数实现）
std::string GetRepoFromGitConfig() {
    std::string repoPath = ".git/config";
    
    // 读取.git/config文件
    FILE* f = fopen(repoPath.c_str(), "r");
    if (!f) {
        return GITHUB_REPO;  // 读取失败，使用默认值
    }
    
    char line[512];
    std::string originUrl = "";
    
    while (fgets(line, sizeof(line), f)) {
        std::string str = line;
        // 查找 [remote "origin"] 部分
        if (str.find("[remote \"origin\"]") != std::string::npos) {
            // 继续读取，直到找到url
            while (fgets(line, sizeof(line), f)) {
                std::string urlLine = line;
                if (urlLine.find("[") != std::string::npos) {
                    break;  // 遇到新的section，停止
                }
                if (urlLine.find("url") != std::string::npos) {
                    // 提取url
                    size_t pos = urlLine.find("=");
                    if (pos != std::string::npos) {
                        originUrl = urlLine.substr(pos + 1);
                        // 去除前后空白
                        size_t start = originUrl.find_first_not_of(" \t");
                        size_t end = originUrl.find_last_not_of(" \t\r\n");
                        if (start != std::string::npos && end != std::string::npos) {
                            originUrl = originUrl.substr(start, end - start + 1);
                        }
                    }
                    break;
                }
            }
            break;
        }
    }
    fclose(f);
    
    if (originUrl.empty()) {
        return GITHUB_REPO;  // 未找到origin，使用默认值
    }
    
    // 解析GitHub URL
    // 支持格式：
    // https://github.com/owner/repo.git
    // git@github.com:owner/repo.git
    std::string ownerRepo = "";
    
    if (originUrl.find("github.com") != std::string::npos) {
        size_t startPos = originUrl.find("github.com");
        if (startPos != std::string::npos) {
            startPos += 11;  // 跳过"github.com"
            
            // 处理https://或git@开头
            if (originUrl.find("https://") != std::string::npos) {
                // https://github.com/owner/repo.git
                startPos += 1;  // 跳过"/"
            } else if (originUrl.find("git@") != std::string::npos) {
                // git@github.com:owner/repo.git
                startPos += 1;  // 跳过":"
            } else {
                startPos += 1;  // 跳过"/"
            }
            
            // 提取 owner/repo
            ownerRepo = originUrl.substr(startPos);
            // 去除.git后缀
            size_t gitPos = ownerRepo.find(".git");
            if (gitPos != std::string::npos) {
                ownerRepo = ownerRepo.substr(0, gitPos);
            }
        }
    }
    
    if (ownerRepo.empty()) {
        return GITHUB_REPO;  // 解析失败，使用默认值
    }
    
    return ownerRepo;
}

std::string GetGitHubApiUrl(const std::string& repo) {
    return "https://api.github.com/repos/" + repo + "/releases/latest";
}

// Theme colors (支持深浅色模式)
bool g_darkMode = true;  // 固定深色模式

// Dark theme colors
struct ThemeColors {
    COLORREF bgStart, bgEnd, bg, card, cardBorder, accent, accentGlow;
    COLORREF text, textDim, border, titlebar, editBg, glassTint;
    int glassAlpha;
};

ThemeColors g_colors;

void UpdateThemeColors() {
    // 固定使用深色主题 (毛玻璃深空蓝主题)
    g_colors.bgStart = RGB(18, 18, 24);
    g_colors.bgEnd = RGB(18, 18, 24);
    g_colors.bg = RGB(18, 18, 24);
    g_colors.card = RGB(30, 40, 60);
    g_colors.cardBorder = RGB(50, 70, 100);
    g_colors.accent = RGB(80, 180, 255);
    g_colors.accentGlow = RGB(60, 140, 220);
    g_colors.text = RGB(240, 245, 255);
    g_colors.textDim = RGB(140, 150, 170);
    g_colors.border = RGB(50, 70, 100);
    g_colors.titlebar = RGB(20, 28, 45);
    g_colors.editBg = RGB(35, 45, 65);
    g_colors.glassTint = RGB(20, 30, 50);
    g_colors.glassAlpha = 100;  // 大幅降低不透明度，让毛玻璃非常明显
}

// Color access macros (保持兼容性)
#define COLOR_BG_START g_colors.bgStart
#define COLOR_BG_END g_colors.bgEnd
#define COLOR_BG g_colors.bg
#define COLOR_CARD g_colors.card
#define COLOR_CARD_BORDER g_colors.cardBorder
#define COLOR_ACCENT g_colors.accent
#define COLOR_ACCENT_GLOW g_colors.accentGlow
#define COLOR_TEXT g_colors.text
#define COLOR_TEXT_DIM g_colors.textDim
#define COLOR_BORDER g_colors.border
#define COLOR_TITLEBAR g_colors.titlebar
#define COLOR_EDIT_BG g_colors.editBg
#define COLOR_GLASS_TINT g_colors.glassTint
#define GLASS_ALPHA g_colors.glassAlpha

// Theme-dependent colors (根据主题自动选择)
#define COLOR_BTN_BG RGB(40, 40, 55)
#define COLOR_BTN_HOVER RGB(55, 55, 75)
#define COLOR_MENU_BG RGB(35, 40, 50)
#define COLOR_MENU_BORDER RGB(70, 75, 85)
#define COLOR_MENU_HOVER RGB(50, 55, 65)
#define COLOR_BOX_BG RGB(40, 45, 55)
#define COLOR_BOX_HOVER RGB(50, 55, 65)
#define COLOR_BOX_BORDER RGB(60, 65, 75)
#define COLOR_BOX_BORDER_HOVER RGB(80, 90, 110)
#define COLOR_CHECK_BG RGB(50, 50, 65)
#define COLOR_CHECK_ACCENT COLOR_ACCENT
#define COLOR_SUCCESS RGB(80, 200, 120)
#define COLOR_WARNING RGB(255, 180, 50)
#define COLOR_ERROR RGB(255, 100, 100)

// Tray icon
#define TRAY_ICON_ID 1
#define WM_TRAYICON (WM_USER + 200)

// OSC rate limits - aligned with preview refresh rate
const DWORD OSC_MIN_INTERVAL = 2000;    // 2s when playing (avoid VRChat rate limit)
const DWORD OSC_PAUSE_INTERVAL = 2000;  // 2s when paused

// Window size (scaled for high DPI)
const int WIN_W_DEFAULT = 650;
const int WIN_H_DEFAULT = 720;
const int WIN_W_MIN = 600;
const int WIN_H_MIN = 680;  // Increased to prevent settings content overlap
int g_winW = WIN_W_DEFAULT;
int g_winH = WIN_H_DEFAULT;
int g_winX = -1;  // Window position (saved on close)
int g_winY = -1;
const int TITLEBAR_H = 60;
const int CARD_PADDING = 25;

// Forward declarations
std::string WstringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWstring(const std::string& str);
int GetTextWidth(HDC hdc, const wchar_t* text, HFONT font, int length);
std::wstring BuildProgressBar(double progress, int bars);
std::wstring BuildPerformanceOSCMessage(int type);  // type: 0=CPU, 1=RAM, 2=GPU
void CreateOverlayWindow();    // OSC暂停覆盖层
void DestroyOverlayWindow();   // 销毁OSC暂停覆盖层
void MainDebugLog(const char* msg, int level);  // 调试日志

// 统一性能监控线程（合并原4个线程）
DWORD WINAPI WorkerThread_PerfMonitor(LPVOID param);

// 初始化函数
void InitializePerfMonitoring();
void ShutdownPerfMonitoring();

// Animation helpers
struct Animation {
    double value = 0.0, target = 0.0, speed = 0.25;  // 提高默认速度
    void update() { 
        // 使用更平滑的缓动：速度与剩余距离成正比
        double diff = target - value;
        value += diff * speed; 
        if (fabs(diff) < 0.001) value = target; 
    }
    void setTarget(double t) { target = t; }
    bool isActive() const { return fabs(target - value) > 0.001; }
    // 设置目标并重置value（用于需要从固定起点开始的动画）
    void setFromTo(double from, double to) { value = from; target = to; }
};

// Smooth value for color transitions
struct SmoothValue {
    double value = 0.0, target = 0.0, speed = 0.15;  // 提高默认速度
    void update() { value += (target - value) * speed; }
    void setTarget(double t) { target = t; }
    void setImmediate(double v) { value = target = v; }
    bool isActive() const { return fabs(target - value) > 0.001; }
};

// Global animation state
Animation g_windowFadeAnim;       // 窗口启动淡入
Animation g_windowScaleAnim;      // 窗口启动缩放
Animation g_tabSlideAnim;         // 标签页滑动
Animation g_menuExpandAnim;       // 下拉菜单展开
Animation g_connectPulseAnim;     // 连接状态脉冲
Animation g_lyricScrollAnim;      // 歌词滚动
SmoothValue g_themeTransition;    // 主题切换过渡

// 下拉菜单逐条淡入动画
Animation g_menuItemAnims[10];    // 最多支持10个菜单项
Animation g_arrowRotationAnim;    // 下拉箭头旋转动画
double g_updateRotation = 0.0;   // 更新按钮旋转角度
DWORD g_lastRotationTime = 0;    // 上次旋转更新时间

// 字符数进度条动画
SmoothValue g_charProgressAnim(0.0);  // 进度条平滑过渡
bool g_charProgressHover = false;     // 鼠标悬停检测
SmoothValue g_charTooltipAnim(0.0);   // tooltip淡入淡出动画

bool g_startupAnimComplete = false;
DWORD g_startupAnimStart = 0;
const DWORD STARTUP_ANIM_DURATION = 500;  // 启动动画时长(ms)

// Tab slide animation state
int g_prevTab = 0;
int g_tabSlideDirection = 0;  // -1 = left, 1 = right
Animation g_displayModeSlideAnim;  // 显示模式滑动动画
int g_prevDisplayMode = 0;  // 显示模式滑动动画的前一个模式
Animation g_systemInfoExpandAnim;  // 系统信息展开动画

// Global state
CRITICAL_SECTION g_cs;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
moekoe::OSCSender* g_osc = nullptr;
moekoe::OSCReceiver* g_oscReceiver = nullptr;  // OSC receiver for VRChat pause commands
moekoe::MoeKoeWS* g_moeKoeClient = nullptr;
moekoe::NeteaseWS* g_neteaseClient = nullptr;
smtc::SMTCClient* g_smtcClient = nullptr;  // QQ Music & other SMTC players
const wchar_t* g_platformNames[] = { L"MoeKoeMusic", L"\x7F51\x6613\x4E91\x97F3\x4E50", L"QQ音乐" };
// Platform definition - easy to add new platforms
struct PlatformInfo {
    std::wstring name;           // Display name
    std::wstring connectMethod;  // HTTP, SMTC, etc.
    bool connected = false;
    bool hover = false;
    DWORD lastPlayTime = 0;
};

// Platform list - add new platforms here
std::vector<PlatformInfo> g_platforms = {
    { L"MoeKoe", L"HTTP", false, false, 0 },
    { L"\x7F51\x6613\x4E91", L"HTTP", false, false, 0 },  // 网易云
    { L"QQ音乐", L"SMTC", false, false, 0 }
};

// Legacy compatibility macros
#define g_platformCount ((int)g_platforms.size())
#define g_moeKoeConnected g_platforms[0].connected
#define g_neteaseConnected g_platforms[1].connected
#define g_smtcConnected g_platforms[2].connected
#define g_moeKoeBoxHover g_platforms[0].hover
#define g_neteaseBoxHover g_platforms[1].hover
#define g_qqMusicBoxHover g_platforms[2].hover
#define g_moeKoeLastPlayTime g_platforms[0].lastPlayTime
#define g_neteaseLastPlayTime g_platforms[1].lastPlayTime
#define g_smtcLastPlayTime g_platforms[2].lastPlayTime

const wchar_t* g_oscPlatformNames[] = { L"\x9177\x72D7", L"\x7F51\x6613\x4E91\x97F3\x4E50", L"QQ音乐" };
int g_currentPlatform = 0;  // 0=MoeKoe, 1=Netease, 2=QQ Music (user selected)
int g_activePlatform = -1;   // Currently active platform (playing music), -1 = none
bool g_autoPlatformSwitch = false; // Auto switch platforms when playing (disabled by default)
const DWORD PLATFORM_SWITCH_DELAY = 2000; // Wait 2s before switching platforms
int g_currentTab = 0;        // 0=Main, 1=Performance, 2=Settings
bool g_tabHover[3] = {false, false, false};

// Display mode hover state
bool g_displayModeHover[2] = {false, false};  // 0=音乐, 1=性能
bool g_autoDetectHover = false;
bool g_showOrderHover = false;

// Performance display settings
int g_performanceMode = 0;  // 0=Music info, 1=System performance
std::wstring g_cpuDisplayName = L"CPU";
std::wstring g_ramDisplayName = L"RAM";
std::wstring g_gpuDisplayName = L"GPU";

// Platform selection dropdown menu
bool g_platformMenuOpen = false;

// Edit controls - inline text editing state
enum EditField { EDIT_NONE = 0, EDIT_IP = 1, EDIT_PORT = 2, EDIT_CPU_NAME = 3, EDIT_RAM_NAME = 4, EDIT_GPU_NAME = 5 };
EditField g_editingField = EDIT_NONE;
int g_cursorPos = 0;           // Cursor position in text
int g_selectStart = 0;         // Selection start (for drag selection)
int g_selectEnd = 0;           // Selection end
bool g_cursorVisible = true;   // Cursor blink state
DWORD g_lastCursorBlink = 0;   // Last cursor blink time
std::wstring g_editingText;    // Current editing text
bool g_mouseDragging = false;  // Mouse drag for selection
int g_platformMenuHover = -1;  // Which menu item is hovered (-1 = none)
bool g_platformBoxHover = false;  // Platform status box hover

// Edit controls

// Custom Dialog System
enum DialogType { DIALOG_INFO, DIALOG_CONFIRM, DIALOG_ERROR, DIALOG_UPDATE };
struct DialogConfig {
    DialogType type = DIALOG_INFO;
    std::wstring title;
    std::wstring content;      // Main content (can be multi-line with \n)
    std::wstring btn1Text;     // Primary button (accent)
    std::wstring btn2Text;     // Secondary button (border)
    std::wstring btn3Text;     // Third button (for update dialog: Skip)
    bool hasBtn2 = false;
    bool hasBtn3 = false;
};
// Dialog result: 0 = closed/cancel, 1 = btn1 (primary), 2 = btn2 (secondary), 3 = btn3 (skip)
int g_dialogResult = 0;
HWND g_dialogHwnd = nullptr;
bool g_dialogClosed = false;
int g_dialogBtnHover = -1;
DialogConfig g_dialogConfig;
int g_dialogWidth = 400;
int g_dialogHeight = 200;

// Tray menu window
HWND g_trayMenuHwnd = nullptr;
int g_trayMenuHover = -1;
bool g_trayMenuVisible = false;
bool g_trayMenuClosing = false;
bool g_trayMenuExitRequested = false;  // 退出程序请求标志
Animation g_trayMenuFadeAnim;
Animation g_trayMenuScaleAnim;

// Dialog animation state
Animation g_dialogFadeAnim;
Animation g_dialogScaleAnim;
bool g_dialogAnimComplete = false;

// Fonts
HFONT g_fontTitle = nullptr;
HFONT g_fontSubtitle = nullptr;
HFONT g_fontNormal = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontLyric = nullptr;
HFONT g_fontLabel = nullptr;

// Forward declarations for custom dialogs
bool ShowInfoDialog(const std::wstring& title, const std::wstring& content);
bool ShowErrorDialog(const std::wstring& title, const std::wstring& content);
bool ShowConfirmDialog(const std::wstring& title, const std::wstring& content, const std::wstring& btnYes = L"确定", const std::wstring& btnNo = L"取消");

// Forward declarations for auto-start
bool CheckAutoStart();
void SetAutoStart(bool enable);

// Brushes
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushCard = nullptr;
HBRUSH g_brushEditBg = nullptr;

// Dragging
bool g_dragging = false;
POINT g_dragStart = {0, 0};

// Update checking
std::wstring g_latestVersion = L"";
std::wstring g_downloadUrl = L"";
std::wstring g_downloadSha256Url = L"";  // URL to SHA256 checksum file
std::wstring g_downloadSha256 = L"";  // Expected SHA256 checksum
std::wstring g_latestChangelog = L"";  // Update changelog
std::wstring g_skipVersion = L"";      // Version to skip
bool g_updateAvailable = false;
bool g_checkingUpdate = false;
bool g_downloadingUpdate = false;
bool g_manualCheckUpdate = false;      // Is this a manual check?
bool g_updateCheckComplete = false;    // Check completed (for manual check)
DWORD g_lastUpdateCheck = 0;
const DWORD UPDATE_CHECK_INTERVAL = 3600000;  // 1 hour

// Parse version string like "1.2.3" to number
int ParseVersion(const std::wstring& ver) {
    int major = 0, minor = 0, patch = 0;
    swscanf_s(ver.c_str(), L"%d.%d.%d", &major, &minor, &patch);
    return major * 10000 + minor * 100 + patch;
}

// Calculate SHA256 hash of a file
std::string CalculateSHA256(const wchar_t* filePath) {
    std::string result;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return result;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return result;
    }
    
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return result;
    }
    
    BYTE buffer[8192];
    DWORD bytesRead;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        if (!CryptHashData(hHash, buffer, bytesRead, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return result;
        }
    }
    
    DWORD hashLen = 0;
    DWORD hashLenSize = sizeof(DWORD);
    if (CryptGetHashParam(hHash, HP_HASHSIZE, (BYTE*)&hashLen, &hashLenSize, 0) && hashLen == 32) {
        BYTE hashData[32];
        if (CryptGetHashParam(hHash, HP_HASHVAL, hashData, &hashLen, 0)) {
            char hex[65];
            for (int i = 0; i < 32; i++) {
                sprintf_s(hex + i * 2, 3, "%02x", hashData[i]);
            }
            hex[64] = '\0';
            result = hex;
        }
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// Search lyrics from Netease API for QQ Music
std::vector<moekoe::LyricLine> SearchLyricsForQQMusic(const std::wstring& title, const std::wstring& artist) {
    std::vector<moekoe::LyricLine> lyrics;
    if (title.empty()) return lyrics;
    
    std::string dbgMsg = "[QQ Music] Searching lyrics for: " + WstringToUtf8(title) + " - " + WstringToUtf8(artist);
    MainDebugLog(dbgMsg.c_str());
    
    // Build search query
    std::string query = WstringToUtf8(title);
    if (!artist.empty()) {
        query += " ";
        query += WstringToUtf8(artist);
    }
    
    // URL encode
    std::string encodedQuery;
    for (char c : query) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedQuery += c;
        } else {
            char buf[4];
            sprintf_s(buf, "%%%02X", (unsigned char)c);
            encodedQuery += buf;
        }
    }
    
    // === Step 1: Search QQ Music API ===
    // API: https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=keyword&format=json
    std::wstring whost = L"c.y.qq.com";
    std::wstring wpath = L"/soso/fcgi-bin/client_search_cp?w=" + Utf8ToWstring(encodedQuery) + L"&format=json&p=1&n=5";
    
    HINTERNET hSession = WinHttpOpen(L"VRCLyricsDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return lyrics;
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return lyrics; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, L"https://y.qq.com", WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return lyrics; }
    
    std::wstring headers = L"Referer: https://y.qq.com\r\nCookie: guid=1234567890\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string searchResp;
    DWORD dwSize = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            searchResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log search response (first 500 chars)
    std::string respDbg = "[QQ Music] QQ Search response: " + searchResp.substr(0, 500);
    MainDebugLog(respDbg.c_str());
    
    // Extract songmid from search result
    // Format: "song":{"list":[{"songmid":"xxx",...}]}
    size_t listPos = searchResp.find("\"list\":[");
    if (listPos == std::string::npos) {
        MainDebugLog("[QQ Music] No song list found");
        return lyrics;
    }
    
    // Find first songmid
    size_t songmidPos = searchResp.find("\"songmid\":\"", listPos);
    if (songmidPos == std::string::npos) {
        MainDebugLog("[QQ Music] No songmid found");
        return lyrics;
    }
    
    size_t midStart = songmidPos + 11;
    size_t midEnd = searchResp.find("\"", midStart);
    if (midEnd == std::string::npos) return lyrics;
    
    std::string songmid = searchResp.substr(midStart, midEnd - midStart);
    MainDebugLog(("[QQ Music] Found songmid: " + songmid).c_str());
    
    // === Step 2: Fetch Lyrics ===
    // API: https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=xxx
    wpath = L"/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" + Utf8ToWstring(songmid) + L"&format=json";
    
    hSession = WinHttpOpen(L"VRCLyricsDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return lyrics;
    
    hConnect = WinHttpConnect(hSession, L"c.y.qq.com", INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return lyrics; }
    
    hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, L"https://y.qq.com", WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return lyrics; }
    
    headers = L"Referer: https://y.qq.com\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string lyricsResp;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            lyricsResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log lyrics response
    std::string lrcDbg = "[QQ Music] QQ Lyrics response: " + lyricsResp.substr(0, 300);
    MainDebugLog(lrcDbg.c_str());
    
    // Extract lyric - format: {"lyric":"BASE64_ENCODED_LRC"}
    size_t lyricPos = lyricsResp.find("\"lyric\":\"");
    if (lyricPos == std::string::npos) {
        MainDebugLog("[QQ Music] No lyric field found");
        return lyrics;
    }
    
    size_t lrcStart = lyricPos + 9;
    size_t lrcEnd = lyricsResp.find("\"", lrcStart);
    if (lrcEnd == std::string::npos) return lyrics;
    
    std::string lrcBase64 = lyricsResp.substr(lrcStart, lrcEnd - lrcStart);
    MainDebugLog(("[QQ Music] Base64 length: " + std::to_string(lrcBase64.length())).c_str());
    
    // Base64 decode
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string unescaped;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;
    
    int val = 0, valb = -8;
    for (unsigned char c : lrcBase64) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            unescaped.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    // Debug: log decoded content (first 200 chars)
    std::string decodedDbg = "[QQ Music] Decoded lyric: " + unescaped.substr(0, 200);
    MainDebugLog(decodedDbg.c_str());
    
    // Parse LRC format
    size_t pos = 0;
    while ((pos = unescaped.find('[', pos)) != std::string::npos) {
        size_t endBracket = unescaped.find(']', pos);
        if (endBracket == std::string::npos) { pos++; continue; }
        
        std::string timeStr = unescaped.substr(pos + 1, endBracket - pos - 1);
        
        // Skip metadata tags like [ti:], [ar:], etc.
        if (timeStr.find(':') != std::string::npos && !isdigit((unsigned char)timeStr[0])) {
            pos = endBracket + 1;
            continue;
        }
        
        // Parse time: mm:ss.xx or mm:ss:xxx
        int min = 0, sec = 0, ms = 0;
        if (sscanf_s(timeStr.c_str(), "%d:%d.%d", &min, &sec, &ms) >= 2 ||
            sscanf_s(timeStr.c_str(), "%d:%d:%d", &min, &sec, &ms) >= 2) {
            double time = min * 60.0 + sec + ms / 1000.0;
            
            // Find lyric text
            size_t nextBracket = unescaped.find('[', endBracket);
            std::string text;
            if (nextBracket != std::string::npos) {
                text = unescaped.substr(endBracket + 1, nextBracket - endBracket - 1);
            } else {
                text = unescaped.substr(endBracket + 1);
            }
            
            // Trim
            size_t start = text.find_first_not_of(" \t\r\n");
            size_t end = text.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos && start <= end) {
                text = text.substr(start, end - start + 1);
            } else {
                text = "";
            }
            
            if (!text.empty()) {
                moekoe::LyricLine line;
                line.startTime = (int)(time * 1000);  // Convert to ms
                line.text = Utf8ToWstring(text);
                lyrics.push_back(line);
            }
        }
        
        pos = endBracket + 1;
    }
    
    // Sort by time
    std::sort(lyrics.begin(), lyrics.end(), [](const moekoe::LyricLine& a, const moekoe::LyricLine& b) {
        return a.startTime < b.startTime;
    });
    
    std::string parsedDbg = "[QQ Music] Parsed " + std::to_string(lyrics.size()) + " lyric lines";
    MainDebugLog(parsedDbg.c_str());
    
    return lyrics;
}

// Check for updates from GitHub
bool CheckForUpdate(bool manualCheck = false) {
    if (g_checkingUpdate) return false;
    g_checkingUpdate = true;
    g_manualCheckUpdate = manualCheck;
    g_latestChangelog.clear();
    
    HINTERNET hSession = WinHttpOpen(L"VRChatLyricsDisplay/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { g_checkingUpdate = false; g_updateCheckComplete = true; return false; }
    
    // Set timeouts: 5s connect, 10s receive
    DWORD timeout = 5000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); g_checkingUpdate = false; g_updateCheckComplete = true; return false; }
    
    // 使用自动检测的仓库地址
    std::string repo = g_autoDetectedRepo.empty() ? GITHUB_REPO : g_autoDetectedRepo;
    std::string path = "/repos/" + repo + "/releases/latest";
    std::wstring wpath(path.begin(), path.end());
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); g_checkingUpdate = false; g_updateCheckComplete = true; return false; }
    
    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResult) bResult = WinHttpReceiveResponse(hRequest, NULL);
    
    if (bResult) {
        DWORD dwSize = 0;
        std::string response;
        do {
            DWORD dwDownloaded = 0;
            char buffer[4096];
            if (WinHttpReadData(hRequest, buffer, sizeof(buffer), &dwDownloaded)) {
                response.append(buffer, dwDownloaded);
            }
        } while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0);
        
        // Parse JSON for tag_name and assets
        size_t tagPos = response.find("\"tag_name\"");
        if (tagPos != std::string::npos) {
            size_t start = response.find("\"", tagPos + 11) + 1;
            size_t end = response.find("\"", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string tag = response.substr(start, end - start);
                // Remove 'v' prefix if present
                if (!tag.empty() && tag[0] == 'v') tag = tag.substr(1);
                
                int len = MultiByteToWideChar(CP_UTF8, 0, tag.c_str(), -1, NULL, 0);
                g_latestVersion.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, tag.c_str(), -1, &g_latestVersion[0], len);
                g_latestVersion.resize(len - 1);
                
                int latestVer = ParseVersion(g_latestVersion);
                // Check if newer and not skipped
                g_updateAvailable = (latestVer > APP_VERSION_NUM) && (g_latestVersion != g_skipVersion);
            }
        }
        
        // Parse release body (changelog)
        size_t bodyPos = response.find("\"body\"");
        if (bodyPos != std::string::npos) {
            size_t start = response.find("\"", bodyPos + 7) + 1;
            size_t end = response.find("\",\"", start);
            if (end == std::string::npos) end = response.find("\"}", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string body = response.substr(start, end - start);
                // Unescape JSON string
                std::string unescaped;
                for (size_t i = 0; i < body.size(); i++) {
                    if (body[i] == '\\' && i + 1 < body.size()) {
                        char next = body[i + 1];
                        if (next == 'n') { unescaped += '\n'; i++; }
                        else if (next == 'r') { unescaped += '\r'; i++; }
                        else if (next == 't') { unescaped += '\t'; i++; }
                        else if (next == '"') { unescaped += '"'; i++; }
                        else if (next == '\\') { unescaped += '\\'; i++; }
                        else if (next == 'u' && i + 5 < body.size()) {
                            std::string hex = body.substr(i + 2, 4);
                            try {
                                int cp = std::stoi(hex, nullptr, 16);
                                if (cp < 0x80) {
                                    unescaped += (char)cp;
                                } else if (cp < 0x800) {
                                    unescaped += (char)(0xC0 | (cp >> 6));
                                    unescaped += (char)(0x80 | (cp & 0x3F));
                                } else {
                                    unescaped += (char)(0xE0 | (cp >> 12));
                                    unescaped += (char)(0x80 | ((cp >> 6) & 0x3F));
                                    unescaped += (char)(0x80 | (cp & 0x3F));
                                }
                                i += 5;
                            } catch (...) { unescaped += body[i]; }
                        } else { unescaped += body[i]; }
                    } else {
                        unescaped += body[i];
                    }
                }
                int len = MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, NULL, 0);
                g_latestChangelog.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, &g_latestChangelog[0], len);
                g_latestChangelog.resize(len - 1);
            }
        }
        
        // Find download URL for .exe and SHA256
        size_t assetsPos = response.find("\"assets\"");
        if (assetsPos != std::string::npos) {
            // Find .exe download URL
            size_t exePos = response.find(".exe\"", assetsPos);
            if (exePos != std::string::npos) {
                size_t urlStart = response.rfind("\"browser_download_url\"", exePos);
                if (urlStart != std::string::npos) {
                    urlStart = response.find("\"", urlStart + 22) + 1;
                    size_t urlEnd = response.find("\"", urlStart);
                    if (urlStart != std::string::npos && urlEnd != std::string::npos) {
                        std::string url = response.substr(urlStart, urlEnd - urlStart);
                        int len = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, NULL, 0);
                        g_downloadUrl.resize(len - 1);
                        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &g_downloadUrl[0], len);
                    }
                }
            }
            // Find SHA256 checksum file URL
            g_downloadSha256Url.clear();
            size_t sha256Pos = response.find(".sha256\"", assetsPos);
            if (sha256Pos != std::string::npos) {
                size_t shaUrlStart = response.rfind("\"browser_download_url\"", sha256Pos);
                if (shaUrlStart != std::string::npos && shaUrlStart > assetsPos) {
                    shaUrlStart = response.find("\"", shaUrlStart + 22) + 1;
                    size_t shaUrlEnd = response.find("\"", shaUrlStart);
                    if (shaUrlStart != std::string::npos && shaUrlEnd != std::string::npos) {
                        std::string shaUrl = response.substr(shaUrlStart, shaUrlEnd - shaUrlStart);
                        int len = MultiByteToWideChar(CP_UTF8, 0, shaUrl.c_str(), -1, NULL, 0);
                        g_downloadSha256Url.resize(len - 1);
                        MultiByteToWideChar(CP_UTF8, 0, shaUrl.c_str(), -1, &g_downloadSha256Url[0], len);
                    }
                }
            }
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    g_checkingUpdate = false;
    g_updateCheckComplete = true;
    g_lastUpdateCheck = GetTickCount();
    return true;
}

// Auto-update: download and replace
int g_downloadProgress = 0;

bool DownloadAndInstallUpdate() {
    if (g_downloadUrl.empty()) return false;
    
    g_downloadingUpdate = true;
    g_downloadProgress = 0;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    
    // Get temp path
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wchar_t tempFile[MAX_PATH];
    swprintf_s(tempFile, L"%sVRCLyricsDisplay_new.exe", tempPath);
    
    // Download file
    HINTERNET hSession = WinHttpOpen(L"VRChatLyricsDisplay/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { g_downloadingUpdate = false; return false; }
    
    // Set timeouts: 10s connect, 60s receive (for large files)
    DWORD timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 60000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    
    // Parse URL
    std::string url = WstringToUtf8(g_downloadUrl);
    std::string host, path;
    size_t pos = url.find("://");
    if (pos != std::string::npos) {
        std::string afterProto = url.substr(pos + 3);
        size_t slashPos = afterProto.find("/");
        if (slashPos != std::string::npos) {
            host = afterProto.substr(0, slashPos);
            path = afterProto.substr(slashPos);
        } else {
            host = afterProto;
            path = "/";
        }
    }
    
    std::wstring whost = Utf8ToWstring(host);
    std::wstring wpath = Utf8ToWstring(path);
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); g_downloadingUpdate = false; return false; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); g_downloadingUpdate = false; return false; }
    
    BOOL bResult = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResult) bResult = WinHttpReceiveResponse(hRequest, NULL);
    
    if (!bResult) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        g_downloadingUpdate = false;
        return false;
    }
    
    // Get file size
    DWORD contentLength = 0;
    DWORD sizeLen = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &sizeLen, WINHTTP_NO_HEADER_INDEX);
    
    // Download to file
    FILE* f = nullptr;
    if (_wfopen_s(&f, tempFile, L"wb") != 0 || !f) {
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        g_downloadingUpdate = false;
        return false;
    }
    
    DWORD totalRead = 0;
    DWORD dwSize = 0;
    do {
        DWORD dwDownloaded = 0;
        char buffer[8192];
        if (WinHttpReadData(hRequest, buffer, sizeof(buffer), &dwDownloaded)) {
            fwrite(buffer, 1, dwDownloaded, f);
            totalRead += dwDownloaded;
            if (contentLength > 0) {
                g_downloadProgress = (int)(totalRead * 100 / contentLength);
                if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
            }
        }
    } while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0);
    
    fclose(f);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    g_downloadingUpdate = false;
    
    if (totalRead < 10000) {
        // Download too small, probably failed
        DeleteFileW(tempFile);
        return false;
    }
    
    // Verify SHA256 if available
    if (!g_downloadSha256Url.empty()) {
        // Download SHA256 checksum file
        std::string sha256Content;
        HINTERNET hShaSession = WinHttpOpen(L"VRChatLyricsDisplay/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (hShaSession) {
            DWORD timeout = 5000;
            WinHttpSetOption(hShaSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
            WinHttpSetOption(hShaSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
            
            std::string shaUrl = WstringToUtf8(g_downloadSha256Url);
            size_t pos = shaUrl.find("://");
            if (pos != std::string::npos) {
                std::string afterProto = shaUrl.substr(pos + 3);
                size_t slashPos = afterProto.find("/");
                if (slashPos != std::string::npos) {
                    std::string host = afterProto.substr(0, slashPos);
                    std::string path = afterProto.substr(slashPos);
                    std::wstring whost = Utf8ToWstring(host);
                    std::wstring wpath = Utf8ToWstring(path);
                    
                    HINTERNET hShaConnect = WinHttpConnect(hShaSession, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
                    if (hShaConnect) {
                        HINTERNET hShaRequest = WinHttpOpenRequest(hShaConnect, L"GET", wpath.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                        if (hShaRequest && WinHttpSendRequest(hShaRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(hShaRequest, NULL)) {
                            DWORD dwSize = 0;
                            do {
                                DWORD dwDownloaded = 0;
                                char buffer[1024];
                                if (WinHttpReadData(hShaRequest, buffer, sizeof(buffer), &dwDownloaded)) {
                                    sha256Content.append(buffer, dwDownloaded);
                                }
                            } while (WinHttpQueryDataAvailable(hShaRequest, &dwSize) && dwSize > 0);
                            WinHttpCloseHandle(hShaRequest);
                        }
                        WinHttpCloseHandle(hShaConnect);
                    }
                }
            }
            WinHttpCloseHandle(hShaSession);
        }
        
        // Parse SHA256 (first 64 hex characters)
        if (sha256Content.length() >= 64) {
            std::string expectedSha256 = sha256Content.substr(0, 64);
            // Convert to lowercase
            for (char& c : expectedSha256) { if (c >= 'A' && c <= 'F') c += 32; }
            
            // Calculate actual SHA256
            std::string actualSha256 = CalculateSHA256(tempFile);
            
            if (actualSha256 != expectedSha256) {
                // SHA256 mismatch - delete downloaded file
                DeleteFileW(tempFile);
                return false;
            }
        }
    }
    
    // Get current exe path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    // Create update batch script
    wchar_t batchPath[MAX_PATH];
    swprintf_s(batchPath, L"%supdate_vrclayrics.bat", tempPath);
    FILE* batch = nullptr;
    if (_wfopen_s(&batch, batchPath, L"w") != 0 || !batch) {
        DeleteFileW(tempFile);
        return false;
    }
    
    // Batch script: wait for program to exit, then replace exe
    fprintf(batch, "@echo off\n");
    fprintf(batch, "echo Updating VRCLyricsDisplay...\n");
    fprintf(batch, "echo Waiting for program to close...\n");
    fprintf(batch, ":waitloop\n");
    fprintf(batch, "tasklist /FI \"IMAGENAME eq VRCLyricsDisplay.exe\" 2>NUL | find /I \"VRCLyricsDisplay.exe\">NUL\n");
    fprintf(batch, "if \"%%ERRORLEVEL%%\"==\"0\" (\n");
    fprintf(batch, "    timeout /t 1 /nobreak >NUL\n");
    fprintf(batch, "    goto waitloop\n");
    fprintf(batch, ")\n");
    fprintf(batch, "echo Replacing executable...\n");
    fprintf(batch, "move /Y \"%ls\" \"%ls\"\n", tempFile, exePath);
    fprintf(batch, "if \"%%ERRORLEVEL%%\"==\"0\" (\n");
    fprintf(batch, "    echo Update successful! Starting program...\n");
    fprintf(batch, "    start \"\" \"%ls\"\n", exePath);
    fprintf(batch, ") else (\n");
    fprintf(batch, "    echo Update failed. Please update manually.\n");
    fprintf(batch, "    pause\n");
    fprintf(batch, ")\n");
    fprintf(batch, "del \"%%~f0\"\n");  // Self-delete batch file
    fclose(batch);
    
    // Run batch script
    ShellExecuteW(NULL, L"open", batchPath, NULL, tempPath, SW_SHOWNORMAL);
    
    return true;
}

// Button animations
Animation g_btnConnectAnim, g_btnApplyAnim, g_btnCloseAnim, g_btnMinAnim, g_btnUpdateAnim, g_btnLaunchAnim, g_btnExportLogAnim, g_btnThemeAnim, g_btnAutoDetectAnim, g_btnShowOrderAnim;
bool g_btnThemeHover = false;
bool g_btnConnectHover = false, g_btnApplyHover = false;
bool g_btnCloseHover = false, g_btnMinHover = false, g_btnUpdateHover = false, g_btnLaunchHover = false, g_btnExportLogHover = false, g_btnAdminHover = false, g_hotkeyBoxHover = false;

// Checkbox hover states for settings tab
bool g_checkboxHover[7] = {false, false, false, false, false, false, false};  // 暂停统计, 自动更新, 显示平台, 最小化到托盘, 启动最小化, 开机自启, 管理员启动

// Song data
std::wstring g_pendingTitle, g_pendingArtist, g_pendingTime;
double g_pendingProgress = 0;
double g_pendingCurrentTime = 0;
double g_pendingDuration = 0;
bool g_pendingIsPlaying = true;
std::vector<moekoe::LyricLine> g_pendingLyrics;
DWORD g_lastOscSendTime = 0;
DWORD g_lastTimerTick = 0;  // For lag detection
DWORD g_systemResumeTime = 0;  // Time when system recovered from lag
std::wstring g_lastOscMessage;
bool g_lastIsPlaying = false;
bool g_playStateChanged = false;

// QQ Music lyrics search state
std::wstring g_qqMusicLastTitle;
std::wstring g_qqMusicLastArtist;
std::thread g_lyricsSearchThread;
std::atomic<bool> g_lyricsSearchRunning{false};

// Preview cache (to avoid rapid flickering)
std::wstring g_cachedPreviewMsg;
DWORD g_lastPreviewUpdate = 0;
const DWORD PREVIEW_UPDATE_INTERVAL = 500;  // Update preview every 500ms

// Redraw optimization
bool g_needsRedraw = true;         // Dirty flag for redraw
DWORD g_lastContentChange = 0;     // Last time content actually changed
const DWORD IDLE_REDRAW_INTERVAL = 1500;  // Redraw every 1.5s when idle (matches OSC send rate)

// Stats
int g_todayPlays = 0;
int g_totalSongs = 0;
std::wstring g_lastSongKey;
bool g_hasPlayedSong = false;
DWORD g_startTime = 0;

// System performance (real system-wide stats)
double g_sysCpuUsage = 0;
SIZE_T g_sysMemUsed = 0;     // KB
SIZE_T g_sysMemTotal = 0;    // KB
int g_batteryPercent = -1;   // -1 = no battery
bool g_batteryCharging = false;
int g_listenMinutes = 0;     // Today's total listen minutes
DWORD g_lastListenUpdate = 0;
SIZE_T g_diskFreeGB = 0;     // Free disk space in GB
int g_quoteIndex = 0;        // Current quote index

// === 异步性能检测架构 ===

// 性能数据结构
struct PerfData {
    // CPU
    int cpuUsage = 0;         // 原生API (必有)
    int cpuTemp = 0;          // LibreHardwareMonitor (可能没有)
    bool cpuTempValid = false;

    // RAM
    int ramUsage = 0;         // 原生API (必有)
    DWORD64 ramUsed = 0;      // 原生API (必有)
    DWORD64 ramTotal = 0;     // 原生API (必有)

    // GPU
    int gpuUsage = 0;         // NVML/ADL/LibreHardwareMonitor (可能没有)
    bool gpuUsageValid = false;
    DWORD64 gpuVramUsed = 0;  // DXGI (必有)
    DWORD64 gpuVramTotal = 0; // DXGI (必有)
};

// 共享数据（线程安全）
PerfData g_latestPerfData;
std::mutex g_perfDataMutex;

// 线程同步
std::atomic<bool> g_threadRunning[4] = {false, false, false, false};
HANDLE g_workerThreads[4] = {nullptr, nullptr, nullptr, nullptr};

// 库可用性标志
bool g_nvmlAvailable = false;
bool g_adlAvailable = false;
bool g_lhmAvailable = false;

// NVML函数指针（NVIDIA）
// NVML返回值枚举
enum nvmlReturn_t {
    NVML_SUCCESS = 0,
    NVML_ERROR_UNINITIALIZED = 1,
    NVML_ERROR_INVALID_ARGUMENT = 2,
    NVML_ERROR_NOT_SUPPORTED = 3,
    NVML_ERROR_NO_PERMISSION = 4,
    NVML_ERROR_ALREADY_INITIALIZED = 5,
    NVML_ERROR_NOT_FOUND = 6,
    NVML_ERROR_INSUFFICIENT_SIZE = 7,
    NVML_ERROR_INSUFFICIENT_POWER = 8,
    NVML_ERROR_DRIVER_NOT_LOADED = 9,
    NVML_ERROR_TIMEOUT = 10,
    NVML_ERROR_IRQ_ISSUE = 11,
    NVML_ERROR_LIBRARY_NOT_FOUND = 12,
    NVML_ERROR_FUNCTION_NOT_FOUND = 13,
    NVML_ERROR_CORRUPTED_INFOROM = 14,
    NVML_ERROR_GPU_IS_LOST = 15,
    NVML_ERROR_RESET_REQUIRED = 16,
    NVML_ERROR_OPERATING_SYSTEM = 17,
    NVML_ERROR_LIB_RM_VERSION_MISMATCH = 18,
    NVML_ERROR_IN_USE = 19,
    NVML_ERROR_MEMORY = 20,
    NVML_ERROR_NO_DATA = 21,
    NVML_ERROR_VGPU_ECC_NOT_READY = 22,
    NVML_ERROR_UNKNOWN = 999
};

typedef int (*nvmlInit_t)();
typedef int (*nvmlShutdown_t)();
typedef int (*nvmlDeviceGetHandleByIndex_t)(unsigned int, void**);

// NVML Utilization结构体
typedef struct nvmlUtilization_st {
    unsigned int gpu;       // GPU使用率
    unsigned int memory;    // 内存使用率
} nvmlUtilization_t;

// NVML Memory结构体
typedef struct nvmlMemory_st {
    unsigned long long total;    // 总显存（字节）
    unsigned long long free;     // 空闲显存（字节）
    unsigned long long used;     // 已用显存（字节）
} nvmlMemory_t;

typedef int (*nvmlDeviceGetUtilizationRates_t)(void*, nvmlUtilization_t*);
typedef int (*nvmlDeviceGetMemoryInfo_t)(void*, nvmlMemory_t*);
typedef int (*nvmlDeviceGetName_t)(void*, char*, unsigned int);

static HMODULE g_nvmlDll = nullptr;
static nvmlInit_t nvmlInit = nullptr;
static nvmlShutdown_t nvmlShutdown = nullptr;
static nvmlDeviceGetHandleByIndex_t nvmlDeviceGetHandleByIndex = nullptr;
static nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates = nullptr;
static nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo = nullptr;
static nvmlDeviceGetName_t nvmlDeviceGetName = nullptr;
static void* g_nvmlDevice = nullptr;

// ADL函数指针（AMD）
typedef int (*ADL_Main_Control_Create_t)(void*, int);
typedef int (*ADL_Main_Control_Destroy_t)();
typedef int (*ADL_Adapter_NumberOfAdapters_Get_t)(int*);
typedef int (*ADL_Adapter_AdapterInfo_Get_t)(void*, int);
typedef int (*ADL_Overdrive5_CurrentActivity_Get_t)(int, int*, int*, int*, int*);

static HMODULE g_adlDll = nullptr;
static ADL_Main_Control_Create_t ADL_Main_Control_Create = nullptr;
static ADL_Main_Control_Destroy_t ADL_Main_Control_Destroy = nullptr;
static ADL_Adapter_NumberOfAdapters_Get_t ADL_Adapter_NumberOfAdapters_Get = nullptr;
static ADL_Adapter_AdapterInfo_Get_t ADL_Adapter_AdapterInfo_Get = nullptr;
static ADL_Overdrive5_CurrentActivity_Get_t ADL_Overdrive5_CurrentActivity_Get = nullptr;

// 系统环境
enum GPUVendor { GPU_UNKNOWN, GPU_NVIDIA, GPU_AMD, GPU_INTEL };
GPUVendor g_gpuVendor = GPU_UNKNOWN;

// 传统兼容性变量（用于UI显示）
double g_cpuUsage = 0.0;     // CPU usage percentage
double g_ramUsage = 0.0;     // RAM usage percentage
SIZE_T g_ramTotal = 0;       // Total RAM in bytes
size_t g_gpuMemUsed = 0;     // GPU memory used (MB)
size_t g_gpuMemTotal = 0;    // GPU memory total (MB)

// Config file path (absolute path to exe directory)
wchar_t g_configPath[MAX_PATH] = {0};
wchar_t g_noLyricConfigPath[MAX_PATH] = {0};

// Music quotes - max 6 Chinese chars (18 bytes) to fit 144 limit
const wchar_t* g_musicQuotes[] = {
    L"\x97F3\x4E50\x662F\x7075\x9B42\x7684\x8BED\x8A00",
    L"\x97F3\x4E50\x70B9\x4EAE\x5FC3\x7075",
    L"\x7EAF\x97F3\x4E50\xFF0C\x54FC\x5531",
    L"\x611F\x53D7\x8282\x594F\x7684\x7F8E",
};
constexpr int g_numQuotes = 5;

// Performance tracking
FILETIME g_lastIdleTime = {0}, g_lastKernelTime = {0}, g_lastUserTime = {0};

// Config
std::wstring g_oscIp = L"127.0.0.1";
int g_oscPort = 9000;
int g_moekoePort = 6520;
bool g_oscEnabled = true;
bool g_minimizeToTray = true;
bool g_startMinimized = false;  // Start minimized to tray
bool g_showPerfOnPause = true;  // Show performance stats when paused
bool g_autoUpdate = true;       // Auto check for updates on startup
bool g_showPlatform = true;     // Show platform name in OSC message
bool g_autoStart = false;       // Auto start with Windows
bool g_runAsAdmin = false;      // Run as administrator on startup
bool g_isConnected = false;
bool g_isConnecting = false;    // 防止重复连接
HANDLE g_mutex = nullptr;       // Single instance mutex
std::vector<std::wstring> g_noLyricMsgs;
int g_lastNoLyricIdx = -1;

// OSC暂停功能（快捷键触发，30秒内不发送消息）
bool g_oscPaused = false;
DWORD g_oscPauseEndTime = 0;    // 暂停结束时间（GetTickCount）
const int OSC_PAUSE_DURATION = 30;  // 暂停时长（秒）
const int HOTKEY_OSC_PAUSE = 1;     // 热键ID
UINT g_oscPauseHotkey = VK_F10;     // 默认F10
UINT g_oscPauseHotkeyMods = 0;      // 修饰符（MOD_ALT=1, MOD_CONTROL=2, MOD_SHIFT=4, MOD_WIN=8）
bool g_editingHotkey = false;       // 是否正在编辑热键
HHOOK g_keyboardHook = nullptr;     // 低级键盘钩子（用于VRChat全屏模式）

// 暂停覆盖层窗口
HWND g_overlayHwnd = nullptr;
bool g_overlayActive = false;
float g_overlayExpandAnim = 0.0f;  // 展开动画进度 (0=收缩, 1=完全展开)
bool g_overlayClosing = false;     // 是否正在关闭（播放收缩动画）

// 粒子系统
struct Particle {
    float x, y;           // 位置
    float vx, vy;         // 速度
    float life;           // 生命值 (0-1)
    float maxLife;        // 最大生命
    int size;             // 大小
    COLORREF color;       // 颜色
};
std::vector<Particle> g_particles;
bool g_particleBurst = false;  // 触发粒子爆发

// 沙漏粒子
struct SandParticle {
    float x, y;
    float vy;
    int size;
    COLORREF color;
};
std::vector<SandParticle> g_sandParticles;

// Last displayed values (for reducing redraws)
std::wstring g_lastDisplayTitle, g_lastDisplayArtist, g_lastDisplayLyric;
double g_lastDisplayProgress = -1;

std::wstring TruncateStr(const std::wstring& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    if (maxLen <= 2) return s.substr(0, maxLen);
    return s.substr(0, maxLen - 2) + L"..";
}

std::wstring FormatTime(double seconds) {
    int m = (int)seconds / 60;
    int s = (int)seconds % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%d:%02d", m, s);
    return buf;
}

std::wstring BuildProgressBar(double progress, int bars) {
    std::wstring bar;
    int filled = (int)(progress * bars);
    for (int i = 0; i < bars; i++) bar += (i < filled) ? L"\x2588" : L"\x2591";
    return bar;
}

void UpdatePerfStats() {
    // 从异步线程获取最新数据
    PerfData perfData;
    {
        std::lock_guard<std::mutex> lock(g_perfDataMutex);
        perfData = g_latestPerfData;
    }

    // 更新传统兼容性变量（用于UI显示）
    g_cpuUsage = perfData.cpuUsage;
    g_ramUsage = perfData.ramUsage;
    g_ramTotal = perfData.ramTotal;
    g_gpuMemUsed = perfData.gpuVramUsed / (1024 * 1024);  // 转换为MB
    g_gpuMemTotal = perfData.gpuVramTotal / (1024 * 1024);  // 转换为MB

    // 同时更新系统变量（保持兼容性）
    g_sysCpuUsage = g_cpuUsage;
    g_sysMemUsed = perfData.ramUsed / 1024;  // 转换为KB
    g_sysMemTotal = perfData.ramTotal / 1024;  // 转换为KB
    
    // === Battery Status ===
    SYSTEM_POWER_STATUS powerStatus;
    if (GetSystemPowerStatus(&powerStatus)) {
        g_batteryPercent = (int)powerStatus.BatteryLifePercent;
        if (g_batteryPercent > 100) g_batteryPercent = -1; // No battery
        g_batteryCharging = (powerStatus.ACLineStatus == 1);
    }
    
    // === Disk Space ===
    ULONGLONG freeBytes = 0;
    if (GetDiskFreeSpaceExW(nullptr, (PULARGE_INTEGER)&freeBytes, nullptr, nullptr)) {
        g_diskFreeGB = (SIZE_T)(freeBytes / (1024 * 1024 * 1024));
    }
    
    // === Listen Time Tracking ===
    DWORD now = GetTickCount();
    if (g_lastListenUpdate > 0 && now - g_lastListenUpdate >= 60000) {
        g_listenMinutes += (now - g_lastListenUpdate) / 60000;
        g_lastListenUpdate = now;
    }
}

// 发送系统消息（启动/暂停/关闭），确保不触发VRChat限流
// 返回是否成功发送
bool SendSystemOSCMessage(const std::wstring& message) {
    if (!g_osc || !g_oscEnabled) {
        return false;
    }
    
    DWORD now = GetTickCount();
    DWORD timeSinceLastSend = now - g_lastOscSendTime;
    
    // 如果距离上次发送不足2秒，等待剩余时间
    if (timeSinceLastSend < OSC_MIN_INTERVAL) {
        Sleep(OSC_MIN_INTERVAL - timeSinceLastSend);
    }
    
    // 发送消息
    bool result = g_osc->sendChatbox(message);
    
    // 更新时间戳
    g_lastOscSendTime = GetTickCount();
    
    MainDebugLog("[SystemOSC] Sent system message");
    
    return result;
}

std::string WstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

int GetTextWidth(HDC hdc, const wchar_t* text, HFONT font, int length) {
    if (!text || length <= 0) return 0;

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE size;
    GetTextExtentPoint32W(hdc, text, length, &size);
    SelectObject(hdc, oldFont);

    return size.cx;
}

std::wstring BuildPerformanceOSCMessage(int type) {
    // 一次性显示所有硬件信息（新格式）
    std::wstring msg;
    
    // === CPU信息 ===
    msg += L"⚡" + g_cpuDisplayName + L" ";
    wchar_t cpuBuf[32];
    swprintf_s(cpuBuf, L"%.0f%%", g_cpuUsage);
    msg += cpuBuf;
    if (g_latestPerfData.cpuTempValid && g_latestPerfData.cpuTemp > 0) {
        wchar_t tempBuf[16];
        swprintf_s(tempBuf, L" 🌡️%d°C", g_latestPerfData.cpuTemp);
        msg += tempBuf;
    }
    msg += L"\n";
    
    // CPU进度条（双标记：占用率+温度）
    {
        int cpuFilled = (int)(g_cpuUsage / 100.0 * 7);  // 7格进度条
        int tempPos = -1;
        if (g_latestPerfData.cpuTempValid && g_latestPerfData.cpuTemp > 0) {
            tempPos = (int)(g_latestPerfData.cpuTemp / 100.0 * 7);  // 温度按100°C上限
            if (tempPos > 7) tempPos = 7;
        }
        msg += L"[";
        for (int i = 0; i < 7; i++) {
            if (i == tempPos && tempPos >= 0) {
                msg += L"│";  // 温度标记
            } else if (i < cpuFilled) {
                msg += L"█";  // 占用率填充
            } else {
                msg += L"░";  // 空格
            }
        }
        msg += L"]\n";
    }
    
    // === RAM信息 ===
    double ramUsedGB = (double)g_latestPerfData.ramUsed / 1024.0 / 1024.0 / 1024.0;
    
    msg += L"💾RAM 📊";
    if (g_latestPerfData.ramUsed > 0) {
        wchar_t ramBuf[32];
        swprintf_s(ramBuf, L"%.1fG", ramUsedGB);
        msg += ramBuf;
    } else {
        msg += L"N/A";
    }
    msg += L"\n";
    
    // RAM进度条（单标记）
    {
        int ramFilled = (int)(g_ramUsage / 100.0 * 7);
        msg += L"[";
        for (int i = 0; i < 7; i++) {
            msg += (i < ramFilled) ? L"█" : L"░";
        }
        msg += L"]\n";
    }
    
    // === GPU信息 ===
    msg += L"🎮" + g_gpuDisplayName + L" ";
    if (g_latestPerfData.gpuUsageValid) {
        wchar_t gpuBuf[32];
        swprintf_s(gpuBuf, L"%d%%", g_latestPerfData.gpuUsage);
        msg += gpuBuf;
    } else {
        msg += L"N/A";
    }
    msg += L" 🎞️";
    double vramUsedGB = (double)g_gpuMemUsed / 1024.0;
    if (g_gpuMemUsed > 0) {
        wchar_t gpuMemBuf[32];
        swprintf_s(gpuMemBuf, L"%.1fG", vramUsedGB);
        msg += gpuMemBuf;
    } else {
        msg += L"N/A";
    }
    msg += L"\n";
    
    // GPU进度条（双标记：占用率+显存）
    if (g_latestPerfData.gpuUsageValid) {
        int gpuFilled = (int)(g_latestPerfData.gpuUsage / 100.0 * 7);
        int vramPos = -1;
        if (g_gpuMemTotal > 0 && g_gpuMemUsed > 0) {
            double vramPercent = (double)g_gpuMemUsed / (double)g_gpuMemTotal * 100.0;
            vramPos = (int)(vramPercent / 100.0 * 7);
            if (vramPos > 7) vramPos = 7;
        }
        msg += L"[";
        for (int i = 0; i < 7; i++) {
            if (i == vramPos && vramPos >= 0) {
                msg += L"│";  // 显存标记
            } else if (i < gpuFilled) {
                msg += L"█";  // 占用率填充
            } else {
                msg += L"░";  // 空格
            }
        }
        msg += L"]\n";
    } else {
        msg += L"[░░░░░░░]\n";
    }
    
    return msg;
}

std::vector<std::wstring> LoadNoLyricMessages(const wchar_t* configPath) {
    std::vector<std::wstring> msgs;
    FILE* f = _wfopen(configPath, L"rb");
    if (!f) return msgs;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::string content(len, 0);
    fread(&content[0], 1, len, f);
    fclose(f);
    
    size_t pos = content.find("\"no_lyric_messages\"");
    if (pos == std::string::npos) return msgs;
    pos = content.find('[', pos);
    if (pos == std::string::npos) return msgs;
    size_t end = content.find(']', pos);
    if (end == std::string::npos) return msgs;
    
    std::string arr = content.substr(pos, end - pos + 1);
    size_t start = 0;
    while ((start = arr.find('"', start)) != std::string::npos) {
        size_t endQ = arr.find('"', start + 1);
        if (endQ == std::string::npos) break;
        std::string msg8 = arr.substr(start + 1, endQ - start - 1);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, msg8.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wmsg(wlen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, msg8.c_str(), -1, &wmsg[0], wlen);
            msgs.push_back(wmsg);
        }
        start = endQ + 1;
    }
    return msgs;
}

void LoadConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"rb");
    if (!f) return;
    
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::string content(len, 0);
    fread(&content[0], 1, len, f);
    fclose(f);
    
    auto getStr = [&](const char* key) -> std::wstring {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return L"";
        pos = content.find('"', pos + search.size());
        if (pos == std::string::npos) return L"";
        size_t end = content.find('"', pos + 1);
        if (end == std::string::npos) return L"";
        std::string val = content.substr(pos + 1, end - pos - 1);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring wval(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(), -1, &wval[0], wlen);
        return wval;
    };
    
    auto getInt = [&](const char* key, int def) -> int {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return def;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return def;
        return atoi(content.c_str() + pos + 1);
    };
    
    auto getBool = [&](const char* key, bool def) -> bool {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return def;
        return content.find("true", pos) < content.find("false", pos);
    };
    
    std::wstring ip = getStr("ip");
    if (!ip.empty()) g_oscIp = ip;
    int port = getInt("port", 0);
    if (port > 0) g_oscPort = port;
    g_moekoePort = getInt("moekoe_port", 6520);
    g_oscEnabled = getBool("osc_enabled", true);
    g_minimizeToTray = getBool("minimize_to_tray", true);
    g_startMinimized = getBool("start_minimized", false);
    g_showPerfOnPause = getBool("show_perf_on_pause", true);
    g_autoUpdate = getBool("auto_update", true);
    g_showPlatform = getBool("show_platform", true);
    g_performanceMode = getInt("performance_mode", 0);
    // g_darkMode固定为true，不再从配置加载

    // Apply theme
    UpdateThemeColors();

    // Auto-start: sync with registry (registry is source of truth)
    g_autoStart = CheckAutoStart();
    // Run as admin: load from config
    g_runAsAdmin = getBool("run_as_admin", false);
    
    // Load window size and position
    int winW = getInt("win_width", 0);
    int winH = getInt("win_height", 0);
    if (winW >= WIN_W_MIN) g_winW = winW;
    if (winH >= WIN_H_MIN) g_winH = winH;
    g_winX = getInt("win_x", -1);
    g_winY = getInt("win_y", -1);
    
    // Load skipped version
    g_skipVersion = getStr("skip_version");
    
    // Load hotkey config
    g_oscPauseHotkey = getInt("osc_pause_hotkey", VK_F10);
    g_oscPauseHotkeyMods = getInt("osc_pause_hotkey_mods", 0);
    
    // Initialize system info expand animation based on performance mode
    if (g_performanceMode == 1) {
        // Performance mode: fully expanded
        g_systemInfoExpandAnim.value = 1.0;
        g_systemInfoExpandAnim.target = 1.0;
    } else {
        // Music mode: fully collapsed
        g_systemInfoExpandAnim.value = 0.0;
        g_systemInfoExpandAnim.target = 0.0;
    }
}

void SaveConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fprintf(f, "{\n  \"osc\": {\n    \"ip\": \"%ls\",\n    \"port\": %d\n  },\n", g_oscIp.c_str(), g_oscPort);
    fprintf(f, "  \"moekoe_port\": %d,\n  \"osc_enabled\": %s,\n  \"minimize_to_tray\": %s,\n  \"start_minimized\": %s,\n  \"show_perf_on_pause\": %s,\n  \"auto_update\": %s,\n  \"show_platform\": %s,\n  \"dark_mode\": %s,\n  \"auto_start\": %s,\n  \"run_as_admin\": %s,\n  \"performance_mode\": %d,\n",
            g_moekoePort, g_oscEnabled ? "true" : "false", g_minimizeToTray ? "true" : "false",
            g_startMinimized ? "true" : "false", g_showPerfOnPause ? "true" : "false",
            g_autoUpdate ? "true" : "false", g_showPlatform ? "true" : "false",
            "true", g_autoStart ? "true" : "false", g_runAsAdmin ? "true" : "false", g_performanceMode);  // g_darkMode固定为true
    fprintf(f, "  \"win_width\": %d,\n  \"win_height\": %d,\n  \"win_x\": %d,\n  \"win_y\": %d,\n",
            g_winW, g_winH, g_winX, g_winY);
    fprintf(f, "  \"osc_pause_hotkey\": %d,\n  \"osc_pause_hotkey_mods\": %d,\n",
            g_oscPauseHotkey, g_oscPauseHotkeyMods);
    fprintf(f, "  \"skip_version\": \"%ls\"\n}\n", g_skipVersion.c_str());
    fclose(f);
}

// Auto-start with Windows (Registry)
bool CheckAutoStart() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                      0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t value[MAX_PATH] = {0};
    DWORD size = MAX_PATH * sizeof(wchar_t);
    LRESULT result = RegQueryValueExW(hKey, L"VRCLyricsDisplay", nullptr, nullptr, 
                                       (LPBYTE)value, &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS && wcslen(value) > 0;
}

void SetAutoStart(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 
                      0, KEY_WRITE, &hKey) != ERROR_SUCCESS) {
        return;
    }
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        RegSetValueExW(hKey, L"VRCLyricsDisplay", 0, REG_SZ, 
                       (const BYTE*)exePath, (DWORD)(wcslen(exePath) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(hKey, L"VRCLyricsDisplay");
    }
    RegCloseKey(hKey);
}

// Forward declaration
bool IsRunningAsAdmin();

// Export logs to a file for bug reporting
void ExportLogs(HWND hwnd) {
    // Generate default filename with timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t defaultName[MAX_PATH];
    swprintf_s(defaultName, L"VRCLyricsDisplay_Logs_%04d%02d%02d_%02d%02d%02d.zip", 
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    // Setup OPENFILENAME struct
    wchar_t filePath[MAX_PATH] = {0};
    wcscpy_s(filePath, defaultName);
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"ZIP \x538B\x7F29\x5305 (*.zip)\0*.zip\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"zip";
    ofn.lpstrTitle = L"\x5BFC\x51FA\x65E5\x5FD7";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (!GetSaveFileNameW(&ofn)) return;  // User cancelled
    
    // Collect logs
    std::string allLogs;
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    
    // Add header
    allLogs += "=== VRCLyricsDisplay Debug Logs ===\n";
    char timeStr[64];
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", t);
    allLogs += std::string("Exported: ") + timeStr + "\n\n";
    
    // Add system info
    allLogs += "=== System Info ===\n";
    allLogs += "App Version: " + std::string(APP_VERSION) + "\n";
    char computerName[MAX_PATH];
    DWORD size = MAX_PATH;
    GetComputerNameA(computerName, &size);
    allLogs += std::string("Computer: ") + computerName + "\n";
    OSVERSIONINFOA osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    #pragma warning(push)
    #pragma warning(disable: 4996)
    GetVersionExA(&osvi);
    #pragma warning(pop)
    char osStr[64];
    sprintf_s(osStr, "Windows %d.%d.%d", osvi.dwMajorVersion, osvi.dwMinorVersion, osvi.dwBuildNumber);
    allLogs += std::string("OS: ") + osStr + "\n";
    
    // Memory info
    MEMORYSTATUSEX memInfo = {0};
    memInfo.dwLength = sizeof(memInfo);
    GlobalMemoryStatusEx(&memInfo);
    char memStr[128];
    sprintf_s(memStr, "Memory: %llu MB / %llu MB (%d%% used)\n", 
              memInfo.ullAvailPhys / 1024 / 1024,
              memInfo.ullTotalPhys / 1024 / 1024,
              memInfo.dwMemoryLoad);
    allLogs += memStr;
    allLogs += "\n";
    
    // Add runtime status
    allLogs += "=== Runtime Status ===\n";
    allLogs += std::string("OSC Enabled: ") + (g_oscEnabled ? "Yes" : "No") + "\n";
    allLogs += std::string("OSC IP: ") + WstringToUtf8(g_oscIp) + ":" + std::to_string(g_oscPort) + "\n";
    allLogs += std::string("MoeKoeMusic Connected: ") + (g_moeKoeConnected ? "Yes" : "No") + "\n";
    allLogs += std::string("Netease Connected: ") + (g_neteaseConnected ? "Yes" : "No") + "\n";
    allLogs += std::string("Active Platform: ") + (g_activePlatform >= 0 ? WstringToUtf8(g_platformNames[g_activePlatform]) : "None") + "\n";
    allLogs += std::string("Running as Admin: ") + (IsRunningAsAdmin() ? "Yes" : "No") + "\n";
    
    // Current song info
    EnterCriticalSection(&g_cs);
    if (!g_pendingTitle.empty()) {
        allLogs += std::string("Current Song: ") + WstringToUtf8(g_pendingTitle);
        if (!g_pendingArtist.empty()) {
            allLogs += " - " + WstringToUtf8(g_pendingArtist);
        }
        allLogs += std::string(" [") + (g_pendingIsPlaying ? "Playing" : "Paused") + "]\n";
        char durStr[64];
        sprintf_s(durStr, "Position: %.1f / %.1f seconds\n", g_pendingCurrentTime, g_pendingDuration);
        allLogs += durStr;
    } else {
        allLogs += "Current Song: None\n";
    }
    LeaveCriticalSection(&g_cs);
    allLogs += "\n";
    
    // Read vrclayrics_debug.log (last 400 lines)
    allLogs += "=== vrclayrics_debug.log (last 400 lines) ===\n";
    char vrclayricsLogPath[MAX_PATH];
    sprintf_s(vrclayricsLogPath, "%svrclayrics_debug.log", tempPath);
    FILE* mf = fopen(vrclayricsLogPath, "r");
    if (mf) {
        char buf[4096];
        std::vector<std::string> allLines;
        while (fgets(buf, sizeof(buf), mf)) {
            allLines.push_back(buf);
        }
        fclose(mf);
        
        // Get last 400 lines
        int startLine = (int)allLines.size() - 400;
        if (startLine < 0) startLine = 0;
        for (int i = startLine; i < (int)allLines.size(); i++) {
            allLogs += allLines[i];
        }
        allLogs += "... (" + std::to_string(allLines.size()) + " total lines, showing last 400)\n";
    } else {
        allLogs += "(log file not found)\n";
    }
    allLogs += "\n";
    
    // Read netease_debug.log (last 400 lines)
    allLogs += "=== netease_debug.log (last 400 lines) ===\n";
    char neteaseLogPath[MAX_PATH];
    sprintf_s(neteaseLogPath, "%snetease_debug.log", tempPath);
    FILE* nf = fopen(neteaseLogPath, "r");
    if (nf) {
        char buf[4096];
        std::vector<std::string> allLines;
        while (fgets(buf, sizeof(buf), nf)) {
            allLines.push_back(buf);
        }
        fclose(nf);
        
        // Get last 400 lines
        int startLine = (int)allLines.size() - 400;
        if (startLine < 0) startLine = 0;
        for (int i = startLine; i < (int)allLines.size(); i++) {
            allLogs += allLines[i];
        }
        allLogs += "... (" + std::to_string(allLines.size()) + " total lines, showing last 400)\n";
    } else {
        allLogs += "(log file not found)\n";
    }
    allLogs += "\n";
    
    // Read smtc_debug.log (last 400 lines)
    allLogs += "=== smtc_debug.log (last 400 lines) ===\n";
    char smtcLogPath[MAX_PATH];
    sprintf_s(smtcLogPath, "%ssmtc_debug.log", tempPath);
    FILE* sf = fopen(smtcLogPath, "r");
    if (sf) {
        char buf[4096];
        std::vector<std::string> allLines;
        while (fgets(buf, sizeof(buf), sf)) {
            allLines.push_back(buf);
        }
        fclose(sf);
        
        // Get last 400 lines
        int startLine = (int)allLines.size() - 400;
        if (startLine < 0) startLine = 0;
        for (int i = startLine; i < (int)allLines.size(); i++) {
            allLogs += allLines[i];
        }
        allLogs += "... (" + std::to_string(allLines.size()) + " total lines, showing last 400)\n";
    } else {
        allLogs += "(log file not found)\n";
    }
    allLogs += "\n";
    
    // Collect recent errors (last 50 lines containing error keywords)
    allLogs += "=== Recent Errors (from all logs) ===\n";
    std::vector<std::string> errorLines;
    
    // Helper to extract error lines
    auto extractErrors = [&](const char* logPath, const char* logName) {
        FILE* f = fopen(logPath, "r");
        if (!f) return;
        char buf[4096];
        std::vector<std::string> allLines;
        while (fgets(buf, sizeof(buf), f)) {
            allLines.push_back(buf);
        }
        fclose(f);
        
        // Get last 50 lines that contain error keywords
        for (int i = (int)allLines.size() - 1; i >= 0 && errorLines.size() < 50; i--) {
            std::string line = allLines[i];
            std::string lowerLine = line;
            for (char& c : lowerLine) c = tolower(c);
            
            // Check for English error keywords
            bool isError = (lowerLine.find("error") != std::string::npos ||
                          lowerLine.find("fail") != std::string::npos ||
                          lowerLine.find("exception") != std::string::npos ||
                          lowerLine.find("warning") != std::string::npos);
            
            // Check for Chinese error keywords
            if (!isError) {
                // UTF-8 Chinese keywords
                const char* chineseErrors[] = {
                    "错误", "失败", "异常", "无法", "拒绝", "未找到", "不支持"
                };
                for (const char* keyword : chineseErrors) {
                    if (line.find(keyword) != std::string::npos) {
                        isError = true;
                        break;
                    }
                }
            }
            
            if (isError) {
                errorLines.push_back(std::string("[") + logName + "] " + line);
            }
        }
    };
    
    extractErrors(vrclayricsLogPath, "main");
    extractErrors(neteaseLogPath, "netease");
    extractErrors(smtcLogPath, "smtc");
    
    if (errorLines.empty()) {
        allLogs += "(no recent errors found)\n";
    } else {
        // Sort by most recent first (reverse order since we collected backwards)
        for (auto it = errorLines.begin(); it != errorLines.end(); ++it) {
            allLogs += *it;
        }
    }
    allLogs += "\n";
    
    // Read config_gui.json
    allLogs += "=== config_gui.json ===\n";
    wchar_t configPath[MAX_PATH];
    GetModuleFileNameW(NULL, configPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(configPath, L'\\');
    if (lastSlash) {
        wcscpy(lastSlash + 1, L"config_gui.json");
        FILE* cf = _wfopen(configPath, L"r");
        if (cf) {
            char buf[1024];
            while (fgets(buf, sizeof(buf), cf)) {
                allLogs += buf;
            }
            fclose(cf);
        } else {
            allLogs += "(config file not found)\n";
        }
    }
    allLogs += "\n";
    
    // Create a temp file for the log content
    wchar_t tempLogFile[MAX_PATH];
    swprintf_s(tempLogFile, L"%sVRCLyricsDisplay_log_temp.txt", 
               std::wstring(tempPath, tempPath + strlen(tempPath)).c_str());
    
    // Convert to wstring properly
    wchar_t wtempPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, tempPath, -1, wtempPath, MAX_PATH);
    swprintf_s(tempLogFile, L"%sVRCLyricsDisplay_log_temp.txt", wtempPath);
    
    FILE* tmpLog = _wfopen(tempLogFile, L"w");
    if (!tmpLog) {
        ShowErrorDialog(L"\x9519\x8BEF", L"\x65E0\x6CD5\x521B\x5EFA\x4E34\x65F6\x6587\x4EF6");
        return;
    }
    fwrite(allLogs.c_str(), 1, allLogs.size(), tmpLog);
    fclose(tmpLog);
    
    // Use PowerShell to create ZIP (most reliable method on Windows)
    wchar_t psCmd[2048];
    swprintf_s(psCmd, 
        L"Compress-Archive -Path '%s' -DestinationPath '%s' -Force",
        tempLogFile, filePath);
    
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};
    
    wchar_t cmdLine[4096];
    swprintf_s(cmdLine, L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"%s\"", psCmd);
    
    BOOL success = CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 
                                   CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    
    if (success) {
        WaitForSingleObject(pi.hProcess, 10000);  // Wait up to 10 seconds
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    // Delete temp file
    DeleteFileW(tempLogFile);
    
    // Check if zip was created
    if (GetFileAttributesW(filePath) != INVALID_FILE_ATTRIBUTES) {
        std::wstring successMsg = L"\x65E5\x5FD7\x5DF2\x5BFC\x51FA\x5230:\n" + std::wstring(filePath);
        ShowInfoDialog(L"\x5BFC\x51FA\x6210\x529F", successMsg);
    } else {
        // Fallback: save as txt if zip failed
        wchar_t txtPath[MAX_PATH];
        wcscpy_s(txtPath, filePath);
        wchar_t* ext = wcsrchr(txtPath, L'.');
        if (ext) wcscpy(ext, L".txt");
        
        FILE* out = _wfopen(txtPath, L"w");
        if (out) {
            fwrite(allLogs.c_str(), 1, allLogs.size(), out);
            fclose(out);
            std::wstring successMsg = L"ZIP \x521B\x5EFA\x5931\x8D25\xFF0C\x5DF2\x4FDD\x5B58\x4E3A TXT:\n" + std::wstring(txtPath);
            ShowInfoDialog(L"\x5BFC\x51FA\x6210\x529F", successMsg);
        } else {
            ShowErrorDialog(L"\x9519\x8BEF", L"\x5BFC\x51FA\x5931\x8D25\xFF0C\x8BF7\x68C0\x67E5\x6587\x4EF6\x8DEF\x5F84");
        }
    }
}

std::wstring FormatOSCMessage(const moekoe::SongInfo& info) {
    std::wstring msg;
    if (!info.hasData || info.title.empty()) {
        // 根据连接状态显示不同提示
        if (!g_isConnected) {
            return L"\x672A\x8FDE\x63A5 - \x70B9\x51FB\x5DE6\x4FA7\"\x8FDE\x63A5\"\x6309\x94AE";  // 未连接 - 点击左侧"连接"按钮
        } else {
            return L"\x7B49\x5F85\x97F3\x4E50\x64AD\x653E...";  // 等待音乐播放...
        }
    }
    
    const size_t MAX_MSG_LEN = 144;
    
    int currentLyricIdx = -1;
    if (!info.lyrics.empty()) {
        for (int i = (int)info.lyrics.size() - 1; i >= 0; i--) {
            if (info.currentTime >= info.lyrics[i].startTime / 1000.0) { currentLyricIdx = i; break; }
        }
    }
    
    if (info.isPlaying) {
        // Line 1: Song title
        msg = L"\x266B " + info.title;
        
        // Line 2: Artist + Platform
        msg += L"\n";
        if (!info.artist.empty()) {
            msg += info.artist;
        }
        if (g_showPlatform) {
            msg += L" [" + (std::wstring)g_oscPlatformNames[g_activePlatform >= 0 ? g_activePlatform : g_currentPlatform] + L"]";
        }
        
        // Line 3: Progress bar
        if (info.duration > 0) {
            msg += L"\n";
            msg += BuildProgressBar(info.currentTime / info.duration, 8);
            msg += L" " + FormatTime(info.currentTime) + L"/" + FormatTime(info.duration);
        }
        
        // Line 4: Lyrics
        if (currentLyricIdx >= 0 && currentLyricIdx < (int)info.lyrics.size()) {
            std::wstring lyric = info.lyrics[currentLyricIdx].text;
            size_t remaining = MAX_MSG_LEN - msg.length() - 3;
            if (remaining < lyric.length()) {
                lyric = TruncateStr(lyric, (int)remaining);
            }
            msg += L"\n\x25B6 " + lyric;
        } else if (info.hasData && !g_noLyricMsgs.empty()) {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, (int)g_noLyricMsgs.size() - 1);
            msg += L"\n" + g_noLyricMsgs[dis(gen)];
        }
    } else {
        // Paused state
        // Line 1: Title - Artist
        msg = L"\x23F8 ";
        std::wstring songInfo = info.title;
        if (!info.artist.empty()) {
            songInfo += L" - " + info.artist;
        }
        msg += TruncateStr(songInfo, 30);
        
        if (g_showPerfOnPause) {
            // Line 2: Progress + time (compact)
            msg += L"\n" + BuildProgressBar(info.duration > 0 ? info.currentTime / info.duration : 0, 5);
            if (info.duration > 0) {
                wchar_t tbuf[20];
                swprintf_s(tbuf, L"%.0f:%02d/%.0f:%02d", 
                    floor(info.currentTime/60), (int)info.currentTime%60,
                    floor(info.duration/60), (int)info.duration%60);
                msg += tbuf;
            }
            
            // Line 3: System info compact
            wchar_t buf3[40];
            if (g_latestPerfData.gpuUsageValid) {
                swprintf_s(buf3, L"\nC:%.0f%% R:%.0fG G:%d%%", g_sysCpuUsage, g_sysMemUsed/1024.0/1024.0, g_latestPerfData.gpuUsage);
            } else {
                swprintf_s(buf3, L"\nC:%.0f%% R:%.0fG", g_sysCpuUsage, g_sysMemUsed/1024.0/1024.0);
            }
            msg += buf3;
            
            // Line 4: Stats + clock + quote merged
            time_t n = time(nullptr);
            struct tm t;
            localtime_s(&t, &n);
            wchar_t buf4[64];
            if (g_hasPlayedSong) {
                int hrs = g_listenMinutes / 60, mins = g_listenMinutes % 60;
                swprintf_s(buf4, L"\n%d\x9996 %dh%dm %02d:%02d", g_totalSongs, hrs, mins, t.tm_hour, t.tm_min);
            } else {
                swprintf_s(buf4, L"\n%02d:%02d", t.tm_hour, t.tm_min);
            }
            msg += buf4;
            
            // Line 5: Quote (max 6 Chinese chars = 18 bytes)
            msg += L"\n";
            msg += g_musicQuotes[g_quoteIndex];
            
            // Rotate quote
            static bool lastWasPlaying = false;
            if (info.isPlaying && !lastWasPlaying) {
                g_quoteIndex = (g_quoteIndex + 1) % g_numQuotes;
            }
            lastWasPlaying = info.isPlaying;
        } else {
            // Detailed mode - show more song info
            // Line 2: Progress bar with time
            if (info.duration > 0) {
                msg += L"\n" + BuildProgressBar(info.currentTime / info.duration, 10);
                msg += L" " + FormatTime(info.currentTime) + L"/" + FormatTime(info.duration);
            }
            
            // Line 3: Current lyric preview
            if (currentLyricIdx >= 0 && currentLyricIdx < (int)info.lyrics.size()) {
                std::wstring lyric = info.lyrics[currentLyricIdx].text;
                msg += L"\n\x25B6 " + TruncateStr(lyric, 25);
            }
            
            // Line 4: Next lyric preview
            int nextIdx = currentLyricIdx + 1;
            if (nextIdx >= 0 && nextIdx < (int)info.lyrics.size()) {
                std::wstring nextLyric = info.lyrics[nextIdx].text;
                msg += L"\n  " + TruncateStr(nextLyric, 25);
            }
            
            // Line 5: Play count
            if (g_hasPlayedSong) {
                wchar_t buf[32];
                swprintf_s(buf, L"\n%d/%d \x9996", g_todayPlays, g_totalSongs);
                msg += buf;
            }
        }
    }
    
    // Final safety truncation
    // No truncation here - handled in sendChatbox with UTF-8 safety
    return msg;
}

void QueueUpdate(const moekoe::SongInfo& info, int platform) {
    EnterCriticalSection(&g_cs);
    UpdatePerfStats();
    
    DWORD now = GetTickCount();
    
    // Update last play time for this platform
    if (info.isPlaying && info.hasData && !info.title.empty()) {
        if (platform == 0) g_moeKoeLastPlayTime = now;
        else g_neteaseLastPlayTime = now;
    }
    
    // Auto-switch logic (only when auto mode is enabled)
    if (g_autoPlatformSwitch) {
        bool moeKoeRecentlyPlaying = (now - g_moeKoeLastPlayTime) < PLATFORM_SWITCH_DELAY;
        bool neteaseRecentlyPlaying = (now - g_neteaseLastPlayTime) < PLATFORM_SWITCH_DELAY;
        
        if (g_activePlatform == -1) {
            // Initial selection - pick the one currently playing
            if (info.isPlaying && info.hasData) {
                g_activePlatform = platform;
            }
        } else if (platform != g_activePlatform) {
            // Other platform is reporting - check if we should switch
            bool currentPlatformSilent = (g_activePlatform == 0 && !moeKoeRecentlyPlaying) ||
                                          (g_activePlatform == 1 && !neteaseRecentlyPlaying);
            if (currentPlatformSilent && info.isPlaying && info.hasData) {
                g_activePlatform = platform;
            }
        }
    } else {
        // Manual mode - just set initial platform if none selected
        if (g_activePlatform == -1 && info.isPlaying && info.hasData) {
            g_activePlatform = platform;
        }
    }
    
    // Only update display if this is the active platform
    if (platform != g_activePlatform && g_activePlatform != -1) {
        LeaveCriticalSection(&g_cs);
        return;
    }
    
    if (info.isPlaying != g_lastIsPlaying) { g_playStateChanged = true; g_lastIsPlaying = info.isPlaying; }
    
    std::wstring songKey = info.title + L" - " + info.artist;
    if (info.hasData && info.isPlaying && songKey != g_lastSongKey && !info.title.empty()) {
        g_lastSongKey = songKey;
        g_todayPlays++; g_totalSongs++; g_hasPlayedSong = true;
    }
    
    g_pendingTitle = info.title;
    g_pendingArtist = info.artist;
    g_pendingProgress = info.duration > 0 ? info.currentTime / info.duration : 0;
    g_pendingCurrentTime = info.currentTime;
    g_pendingDuration = info.duration;
    g_pendingIsPlaying = info.isPlaying;
    g_pendingTime = FormatTime(info.currentTime) + L" / " + FormatTime(info.duration);
    g_pendingLyrics = info.lyrics;
    
    // Track listening time
    if (info.isPlaying && info.hasData) {
        DWORD now = GetTickCount();
        if (g_lastListenUpdate == 0) g_lastListenUpdate = now;
        DWORD elapsed = now - g_lastListenUpdate;
        if (elapsed >= 60000) {  // Every minute
            g_listenMinutes += elapsed / 60000;
            g_lastListenUpdate = now;
        }
    }
    
    // 检查OSC暂停状态是否过期（在发送逻辑中的备份检查）
    DWORD currentTime = GetTickCount();
    if (g_oscPaused && g_oscPauseEndTime > 0 && currentTime >= g_oscPauseEndTime) {
        g_oscPaused = false;
        g_oscPauseEndTime = 0;
        // 开始收缩动画而不是直接销毁
        g_overlayClosing = true;
        MainDebugLog("[OSC] Pause ended naturally in send logic, closing animation");
    }
    
    if (g_osc && info.hasData && g_oscEnabled && !g_oscPaused) {
        DWORD now = GetTickCount();
        // 统一使用2秒间隔，避免VRChat限流
        DWORD minInterval = OSC_MIN_INTERVAL;
        
        // 如果系统刚从卡顿恢复，额外等待确保不会连续发送
        if (g_systemResumeTime > 0) {
            DWORD timeSinceResume = now - g_systemResumeTime;
            if (timeSinceResume < 3000) {
                // 卡顿恢复后3秒内，强制等待完整间隔
                minInterval = OSC_MIN_INTERVAL;
                // 同时重置最后发送时间
                if (timeSinceResume < 100) {
                    g_lastOscSendTime = now;
                }
            } else {
                // 已经过去足够时间，清除恢复标记
                g_systemResumeTime = 0;
            }
        }
        
        if (now - g_lastOscSendTime >= minInterval) {
            std::wstring oscMsg;
            
            // 根据显示模式选择消息类型
            if (g_performanceMode == 1) {
                // 性能模式：一次性发送所有硬件信息
                oscMsg = BuildPerformanceOSCMessage(0);
            } else {
                // 音乐模式：发送歌曲信息
                oscMsg = FormatOSCMessage(info);
            }
            
            if (oscMsg != g_lastOscMessage || g_playStateChanged) {
                g_osc->sendChatbox(oscMsg);
                g_lastOscMessage = oscMsg;
                g_lastOscSendTime = now;
                g_playStateChanged = false;
            }
        }
    }
    
    if (g_hwnd) g_needsRedraw = true;
    LeaveCriticalSection(&g_cs);
}

// Enable blur behind effect (Windows 10/11)
void EnableBlurBehind(HWND hwnd) {
    typedef struct _ACCENT_POLICY {
        int AccentState;
        int AccentFlags;
        int GradientColor;
        int AnimationId;
    } ACCENT_POLICY;
    
    HMODULE hUser = LoadLibraryW(L"user32.dll");
    if (!hUser) return;
    
    typedef BOOL(WINAPI* SetWindowCompositionAttribute_t)(HWND, void*);
    auto fn = (SetWindowCompositionAttribute_t)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    
    if (fn) {
        // 固定使用深色模式毛玻璃
        ACCENT_POLICY policy = { 4, 2, (COLOR_GLASS_TINT & 0xFFFFFF) | (GLASS_ALPHA << 24), 0 };
        struct _WINDOWCOMPOSITIONATTRIBDATA {
            int Attrib;
            void* pvData;
            int cbData;
        } data = { 19, &policy, sizeof(policy) };
        fn(hwnd, &data);
    }
    
    FreeLibrary(hUser);
}

// Create a rounded window region for the main window
HRGN CreateRoundedWindowRegion(HWND hwnd, int cornerRadius = 12) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    // 创建圆角矩形区域
    HRGN hrgn = CreateRoundRectRgn(
        rc.left, rc.top, rc.right, rc.bottom,
        cornerRadius, cornerRadius
    );
    
    return hrgn;
}

// Update window system theme (title bar color, rounded corners)
void UpdateWindowSystemTheme(HWND hwnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            // DWMWA_WINDOW_CORNER_PREFERENCE = 33
            // 0=DEFAULT, 1=DONOTROUND, 2=ROUND, 3=ROUNDSMALL
            int corner = 2;  // ROUND - 使用系统圆角
            fn(hwnd, 33, &corner, sizeof(corner));
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
            BOOL dark = TRUE;  // 固定深色模式
            fn(hwnd, 20, &dark, sizeof(dark));
        }
        FreeLibrary(hDwm);
    }
}

// Check if running as administrator
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, 
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// Restart as administrator
void RestartAsAdmin(HWND hwnd) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    
    if (ShellExecuteExW(&sei)) {
        // Release mutex before closing to allow new instance to start
        if (g_mutex) {
            ReleaseMutex(g_mutex);
            CloseHandle(g_mutex);
            g_mutex = nullptr;
        }
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

void CreateTrayIcon(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"VRChat Lyrics Display");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }
void UpdateTrayTip(const wchar_t* text) { wcscpy_s(g_nid.szTip, text); Shell_NotifyIconW(NIM_MODIFY, &g_nid); }

// 创建圆角矩形路径的辅助函数
void CreateRoundRectPath(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& rect, int radius) {
    Gdiplus::REAL d = (Gdiplus::REAL)(radius * 2);
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
}

// 使用 GDI+ 绘制抗锯齿圆角矩形
void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    // 创建圆角矩形路径
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    // 填充
    SolidBrush brush(Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void DrawRoundRectWithBorder(HDC hdc, int x, int y, int w, int h, int radius, COLORREF fillColor, COLORREF borderColor) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    // 创建圆角矩形路径
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    // 填充
    SolidBrush brush(Color(255, GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor)));
    graphics.FillPath(&brush, &path);
    
    // 边框
    Pen pen(Color(255, GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)), 2);
    graphics.DrawPath(&pen, &path);
}

// 绘制半透明圆角矩形（毛玻璃效果）
void DrawRoundRectAlpha(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color, int alpha) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    // 创建圆角矩形路径
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    // 填充（带透明度）
    SolidBrush brush(Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void DrawTextCentered(HDC hdc, const wchar_t* text, int cx, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, cx - sz.cx / 2, y, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

void DrawTextLeft(HDC hdc, const wchar_t* text, int x, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    TextOutW(hdc, x, y, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

// Draw text vertically centered in a given rect area (for checkboxes)
void DrawTextVCentered(HDC hdc, const wchar_t* text, int x, int y, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, x, y + (h - sz.cy) / 2, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

// Draw text right-aligned and vertically centered
void DrawTextVCenteredRight(HDC hdc, const wchar_t* text, int rightX, int y, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, rightX - sz.cx, y + (h - sz.cy) / 2, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

// Draw text centered both horizontally and vertically (for buttons)
void DrawTextCenteredBoth(HDC hdc, const wchar_t* text, int x, int y, int w, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, x + (w - sz.cx) / 2, y + (h - sz.cy) / 2, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

// === Custom Dialog Implementation ===
LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Enable blur behind effect for dialog
            EnableBlurBehind(hwnd);
            SetTimer(hwnd, 1, 16, nullptr);
            
            // 初始化弹窗动画
            g_dialogAnimComplete = false;
            g_dialogFadeAnim.value = 0.0;
            g_dialogFadeAnim.target = 1.0;
            g_dialogFadeAnim.speed = 0.15;
            g_dialogScaleAnim.value = 0.92;
            g_dialogScaleAnim.target = 1.0;
            g_dialogScaleAnim.speed = 0.2;
            
            // 初始透明度
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            return 0;
        }
        case WM_TIMER: {
            // 更新弹窗动画
            if (!g_dialogAnimComplete) {
                g_dialogFadeAnim.update();
                g_dialogScaleAnim.update();
                
                BYTE alpha = (BYTE)(g_dialogFadeAnim.value * 255);
                SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
                
                if (!g_dialogFadeAnim.isActive() && !g_dialogScaleAnim.isActive()) {
                    g_dialogAnimComplete = true;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            
            // 应用缩放动画（从中心向外扩展）
            double scale = g_dialogScaleAnim.value;
            int drawW = (int)(w * scale);
            int drawH = (int)(h * scale);
            int offsetX = (w - drawW) / 2;
            int offsetY = (h - drawH) / 2;
            
            // 先填充背景
            HBRUSH bgBrush = CreateSolidBrush(COLOR_BG);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);
            
            // 创建临时DC绘制原始内容
            HDC tempDC = CreateCompatibleDC(hdc);
            HBITMAP tempBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldTempBmp = (HBITMAP)SelectObject(tempDC, tempBmp);
            
            // 在临时DC上绘制原始尺寸内容
            // Card with rounded corners
            DrawRoundRect(tempDC, 0, 0, w, h, 16, COLOR_CARD);
            
            // Accent bar - color based on type
            COLORREF accentColor = COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) accentColor = COLOR_ERROR;
            else if (g_dialogConfig.type == DIALOG_CONFIRM) accentColor = COLOR_WARNING;
            DrawRoundRect(tempDC, 0, 0, 5, h, 2, accentColor);
            
            // Title
            SetTextColor(tempDC, COLOR_TEXT);
            SetBkMode(tempDC, TRANSPARENT);
            HFONT titleFont = g_fontSubtitle ? g_fontSubtitle : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(tempDC, titleFont);
            if (!g_dialogConfig.title.empty()) {
                TextOutW(tempDC, 30, 25, g_dialogConfig.title.c_str(), (int)g_dialogConfig.title.length());
            }
            SelectObject(tempDC, oldFont);
            
            // Content with word wrap
            HFONT contentFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(tempDC, contentFont);
            int lineY = 70;
            int lineH = 32;
            int maxWidth = w - 60;  // 减去左右边距
            
            std::wstring content = g_dialogConfig.content;
            size_t pos = 0;
            while (pos < content.length()) {
                size_t nextPos = content.find(L'\n', pos);
                if (nextPos == std::wstring::npos) nextPos = content.length();
                std::wstring line = content.substr(pos, nextPos - pos);
                pos = nextPos + 1;
                
                // 如果这一行太长，需要自动换行
                if (line.length() > 0) {
                    SIZE textSize;
                    GetTextExtentPoint32W(tempDC, line.c_str(), (int)line.length(), &textSize);
                    
                    if (textSize.cx > maxWidth && line.length() > 1) {
                        // 需要换行，按字符分割
                        std::wstring wrappedLine;
                        for (size_t i = 0; i < line.length(); i++) {
                            std::wstring testLine = wrappedLine + line[i];
                            GetTextExtentPoint32W(tempDC, testLine.c_str(), (int)testLine.length(), &textSize);
                            
                            if (textSize.cx > maxWidth && !wrappedLine.empty()) {
                                // 输出当前行
                                TextOutW(tempDC, 30, lineY, wrappedLine.c_str(), (int)wrappedLine.length());
                                lineY += lineH;
                                wrappedLine = line[i];
                            } else {
                                wrappedLine += line[i];
                            }
                        }
                        // 输出最后一行
                        if (!wrappedLine.empty()) {
                            TextOutW(tempDC, 30, lineY, wrappedLine.c_str(), (int)wrappedLine.length());
                            lineY += lineH;
                        }
                    } else {
                        // 行长度合适，直接输出
                        TextOutW(tempDC, 30, lineY, line.c_str(), (int)line.length());
                        lineY += lineH;
                    }
                }
            }
            SelectObject(tempDC, oldFont);
            
            // Buttons
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            // Button 1
            COLORREF btn1Bg = (g_dialogBtnHover == 0) ? RGB(110, 190, 255) : COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) btn1Bg = (g_dialogBtnHover == 0) ? RGB(255, 120, 120) : COLOR_ERROR;
            DrawRoundRect(tempDC, btn1X, btnY, btnW, btnH, 8, btn1Bg);
            HFONT btnFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(tempDC, btnFont);
            SetTextColor(tempDC, RGB(0, 0, 0));
            RECT btn1Rc = {btn1X, btnY, btn1X + btnW, btnY + btnH};
            if (!g_dialogConfig.btn1Text.empty()) {
                DrawTextW(tempDC, g_dialogConfig.btn1Text.c_str(), (int)g_dialogConfig.btn1Text.length(), &btn1Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            // Button 2
            if (g_dialogConfig.hasBtn2) {
                COLORREF btn2Bg = (g_dialogBtnHover == 1) ? COLOR_BTN_HOVER : COLOR_BTN_BG;
                DrawRoundRect(tempDC, btn2X, btnY, btnW, btnH, 8, btn2Bg);
                HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
                HPEN oldPen = (HPEN)SelectObject(tempDC, borderPen);
                SelectObject(tempDC, GetStockObject(NULL_BRUSH));
                RoundRect(tempDC, btn2X, btnY, btn2X + btnW, btnY + btnH, 16, 16);
                SelectObject(tempDC, oldPen);
                DeleteObject(borderPen);
                SetTextColor(tempDC, COLOR_TEXT);
                RECT btn2Rc = {btn2X, btnY, btn2X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn2Text.empty()) {
                    DrawTextW(tempDC, g_dialogConfig.btn2Text.c_str(), (int)g_dialogConfig.btn2Text.length(), &btn2Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            // Button 3
            if (g_dialogConfig.hasBtn3) {
                COLORREF btn3Bg = (g_dialogBtnHover == 2) ? COLOR_BTN_HOVER : COLOR_BTN_BG;
                DrawRoundRect(tempDC, btn3X, btnY, btnW, btnH, 8, btn3Bg);
                SetTextColor(tempDC, COLOR_TEXT_DIM);
                RECT btn3Rc = {btn3X, btnY, btn3X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn3Text.empty()) {
                    DrawTextW(tempDC, g_dialogConfig.btn3Text.c_str(), (int)g_dialogConfig.btn3Text.length(), &btn3Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            SelectObject(tempDC, oldFont);
            
            // 缩放并复制到目标DC（从中心扩展）
            SetStretchBltMode(memDC, HALFTONE);
            StretchBlt(memDC, offsetX, offsetY, drawW, drawH, tempDC, 0, 0, w, h, SRCCOPY);
            
            // 清理临时DC
            SelectObject(tempDC, oldTempBmp);
            DeleteObject(tempBmp);
            DeleteDC(tempDC);
            
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
                btn3X = btn2X;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            int oldHover = g_dialogBtnHover;
            if (x >= btn1X && x < btn1X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 0;
            } else if (g_dialogConfig.hasBtn2 && x >= btn2X && x < btn2X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 1;
            } else if (g_dialogConfig.hasBtn3 && x >= btn3X && x < btn3X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 2;
            } else {
                g_dialogBtnHover = -1;
            }
            if (oldHover != g_dialogBtnHover) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
                btn3X = btn2X;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            if (x >= btn1X && x < btn1X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 1;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (g_dialogConfig.hasBtn2 && x >= btn2X && x < btn2X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 2;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (g_dialogConfig.hasBtn3 && x >= btn3X && x < btn3X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 3;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                g_dialogResult = false;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (wParam == VK_RETURN) {
                g_dialogResult = true;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, 1);
            g_dialogHwnd = nullptr;
            // Don't call PostQuitMessage - it would affect the main window's message loop
            // The dialog's message loop exits via g_dialogClosed flag
            return 0;
        }
        case WM_CLOSE: {
            g_dialogResult = 0;
            g_dialogClosed = true;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ShowCustomDialog(const DialogConfig& config) {
    g_dialogConfig = config;
    g_dialogBtnHover = -1;
    
    // Calculate dialog size based on content
    int lineCount = 1;
    for (size_t i = 0; i < config.content.length(); i++) {
        if (config.content[i] == L'\n') lineCount++;
    }
    g_dialogWidth = 520;
    g_dialogHeight = 160 + lineCount * 32 + 30;
    if (g_dialogHeight < 220) g_dialogHeight = 220;
    
    // Register window class (only once)
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"VRCLyricsDialog_Class";
        RegisterClassExW(&wc);
        registered = true;
    }
    
    // Center on parent window (or screen if no parent)
    int x, y;
    if (g_hwnd && IsWindow(g_hwnd)) {
        RECT parentRect;
        GetWindowRect(g_hwnd, &parentRect);
        int parentW = parentRect.right - parentRect.left;
        int parentH = parentRect.bottom - parentRect.top;
        x = parentRect.left + (parentW - g_dialogWidth) / 2;
        y = parentRect.top + (parentH - g_dialogHeight) / 2;
    } else {
        // Fallback to screen center
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        x = (screenW - g_dialogWidth) / 2;
        y = (screenH - g_dialogHeight) / 2;
    }
    
    g_dialogHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"VRCLyricsDialog_Class",
        config.title.c_str(),
        WS_POPUP,
        x, y, g_dialogWidth, g_dialogHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    
    if (!g_dialogHwnd) {
        // Window creation failed
        return false;
    }
    
    // Rounded corners
    typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        auto fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            int corner = 2;
            fn(g_dialogHwnd, 33, &corner, sizeof(corner));
        }
        FreeLibrary(hDwm);
    }
    
    // 初始化弹窗动画
    g_dialogFadeAnim.value = 0.0;
    g_dialogFadeAnim.target = 1.0;
    g_dialogFadeAnim.speed = 0.2;
    g_dialogScaleAnim.value = 0.85;
    g_dialogScaleAnim.target = 1.0;
    g_dialogScaleAnim.speed = 0.25;
    g_dialogAnimComplete = false;
    
    SetLayeredWindowAttributes(g_dialogHwnd, 0, 0, LWA_ALPHA);  // 初始透明度为0
    ShowWindow(g_dialogHwnd, SW_SHOW);
    SetForegroundWindow(g_dialogHwnd);
    UpdateWindow(g_dialogHwnd);
    
    // Message loop - only process messages for this dialog
    MSG msg;
    g_dialogClosed = false;
    while (!g_dialogClosed) {
        // Use GetMessage with dialog hwnd to only get dialog messages
        BOOL ret = GetMessageW(&msg, g_dialogHwnd, 0, 0);
        if (ret == 0 || ret == -1) {
            // WM_QUIT received or error - exit loop
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    // Ensure dialog is destroyed
    if (g_dialogHwnd && IsWindow(g_dialogHwnd)) {
        DestroyWindow(g_dialogHwnd);
        g_dialogHwnd = nullptr;
    }
    
    return g_dialogResult;
}

// Convenience functions
bool ShowInfoDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_INFO;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x786E\x5B9A";  // 确定
    config.hasBtn2 = false;
    return ShowCustomDialog(config) > 0;
}

bool ShowErrorDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_ERROR;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x786E\x5B9A";  // 确定
    config.hasBtn2 = false;
    return ShowCustomDialog(config) > 0;
}

bool ShowConfirmDialog(const std::wstring& title, const std::wstring& content, const std::wstring& btnYes, const std::wstring& btnNo) {
    DialogConfig config;
    config.type = DIALOG_CONFIRM;
    config.title = title;
    config.content = content;
    config.btn1Text = btnYes;
    config.btn2Text = btnNo;
    config.hasBtn2 = true;
    return ShowCustomDialog(config) == 1;
}

// Update dialog with 3 buttons: Update | Cancel | Skip this version
// Returns: 0=cancel/closed, 1=update, 2=cancel, 3=skip
int ShowUpdateDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_UPDATE;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x66F4\x65B0";  // 更新
    config.btn2Text = L"\x53D6\x6D88";  // 取消
    config.btn3Text = L"\x8DF3\x8FC7";  // 跳过
    config.hasBtn2 = true;
    config.hasBtn3 = true;
    return ShowCustomDialog(config);
}

bool ShowFirstRunDialog() {
    DialogConfig config;
    config.type = DIALOG_INFO;
    config.title = L"VRChat Lyrics Display";
    
    // Check if running as admin
    bool isAdmin = IsRunningAsAdmin();
    std::wstring content;
    if (!isAdmin) {
        content = L"\x5EFA\x8BAE\x4EE5\x7BA1\x7406\x5458\x8EAB\x4EFD\x8FD0\x884C\x4EE5\x4FDD\x8BC1\x529F\x80FD\x6B63\x5E38\n\n";
    }
    content += L"\x8BF7\x9009\x62E9\x5173\x95ED\x65B9\x5F0F:\n";
    content += L"\x2022 \x6700\x5C0F\x5316\x5230\x6258\x76D8 \x2212 \x7EE7\x7EED\x540E\x53F0\x8FD0\x884C\n";
    content += L"\x2022 \x76F4\x63A5\x9000\x51FA \x2212 \x5173\x95ED\x7A0B\x5E8F\n\n";
    content += L"\x53EF\x5728\x8BBE\x7F6E\x4E2D\x968F\x65F6\x66F4\x6539";
    config.content = content;
    config.btn1Text = L"\x6258\x76D8";  // 托盘
    config.btn2Text = L"\x9000\x51FA";    // 退出
    config.hasBtn2 = true;
    int result = ShowCustomDialog(config);
    // 按钮1(托盘)返回1 -> true，按钮2(退出)返回2 -> false
    return result == 1;
}
// === End Custom Dialog ===

// === Tray Menu Window ===
const int TRAY_MENU_W = 150;
const int TRAY_MENU_H = 72;  // 刚好容纳两个选项 (32*2 + 4*2)
const int TRAY_MENU_ITEM_H = 32;

LRESULT CALLBACK TrayMenuProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Enable blur behind effect (毛玻璃)
            EnableBlurBehind(hwnd);
            // 设置圆角
            typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
            HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
            if (hDwm) {
                auto fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
                if (fn) {
                    int corner = 2;  // DWMWCP_ROUND
                    fn(hwnd, 33, &corner, sizeof(corner));
                }
                FreeLibrary(hDwm);
            }
            // 初始化显示动画
            g_trayMenuClosing = false;
            g_trayMenuFadeAnim.value = 0.0;
            g_trayMenuFadeAnim.target = 1.0;
            g_trayMenuFadeAnim.speed = 0.2;
            g_trayMenuScaleAnim.value = 0.85;
            g_trayMenuScaleAnim.target = 1.0;
            g_trayMenuScaleAnim.speed = 0.25;
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            SetTimer(hwnd, 1, 16, nullptr);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) {
                g_trayMenuFadeAnim.update();
                g_trayMenuScaleAnim.update();
                
                // 计算透明度和缩放
                BYTE alpha = (BYTE)(255 * g_trayMenuFadeAnim.value);
                SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
                
                // 检查是否完成关闭动画
                if (g_trayMenuClosing && g_trayMenuFadeAnim.value < 0.01) {
                    bool shouldExit = g_trayMenuExitRequested;
                    g_trayMenuExitRequested = false;
                    g_trayMenuVisible = false;
                    g_trayMenuHover = -1;
                    g_trayMenuClosing = false;
                    KillTimer(hwnd, 1);
                    DestroyWindow(hwnd);  // 销毁窗口，让下次可以重新创建
                    
                    // 如果请求退出程序，动画完成后发送关闭消息
                    if (shouldExit) {
                        PostMessage(g_hwnd, WM_CLOSE, 0, 0);
                    }
                    return 0;
                }
                
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_ERASEBKGND: {
            return 1;  // 防止闪烁，背景由WM_PAINT处理
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            
            // 清除背景
            {
                HBRUSH bgBrush = CreateSolidBrush(COLOR_BG);
                FillRect(memDC, &rc, bgBrush);
                DeleteObject(bgBrush);
            }
            
            // 应用缩放变换
            double scale = g_trayMenuScaleAnim.value;
            
            // 使用 GDI+ 绘制所有内容，应用统一的缩放变换
            {
                Graphics graphics(memDC);
                graphics.SetSmoothingMode(SmoothingModeHighQuality);
                graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
                
                // 设置缩放变换（以窗口中心为缩放中心）
                float centerX = w / 2.0f;
                float centerY = h / 2.0f;
                graphics.TranslateTransform(centerX, centerY);
                graphics.ScaleTransform((REAL)scale, (REAL)scale);
                graphics.TranslateTransform(-centerX, -centerY);
                
                // 绘制圆角背景
                RectF rectF(0, 0, (REAL)w, (REAL)h);
                GraphicsPath path;
                int cornerRadius = 10;
                CreateRoundRectPath(&path, rectF, cornerRadius);
                
                // 半透明背景
                int bgAlpha = (int)(220 * g_trayMenuFadeAnim.value);
                SolidBrush bgBrush(Color(bgAlpha, GetRValue(COLOR_CARD), GetGValue(COLOR_CARD), GetBValue(COLOR_CARD)));
                graphics.FillPath(&bgBrush, &path);
                
                // 发光边框
                int borderAlpha = (int)(255 * g_trayMenuFadeAnim.value);
                Pen borderPen(Color(borderAlpha, GetRValue(COLOR_BORDER), GetGValue(COLOR_BORDER), GetBValue(COLOR_BORDER)), 1.5f);
                graphics.DrawPath(&borderPen, &path);
                
                // 菜单项（文字固定位置，不抖动）
                const wchar_t* items[] = { L"\x663E\x793A\x7A97\x53E3", L"\x9000\x51FA\x7A0B\x5E8F" };  // 显示窗口, 退出程序
                int textAlpha = (int)(255 * g_trayMenuFadeAnim.value);
                
                for (int i = 0; i < 2; i++) {
                    // 固定位置（不再基于scale重新计算）
                    int itemY = 4 + i * TRAY_MENU_ITEM_H;
                    int itemX = 2;
                    int itemW = w - 4;
                    int itemH = TRAY_MENU_ITEM_H - 4;
                    
                    // 悬停背景
                    if (g_trayMenuHover == i) {
                        SolidBrush hoverBrush(Color(textAlpha, GetRValue(COLOR_BTN_HOVER), GetGValue(COLOR_BTN_HOVER), GetBValue(COLOR_BTN_HOVER)));
                        GraphicsPath hoverPath;
                        RectF hoverRect((REAL)itemX, (REAL)itemY, (REAL)itemW, (REAL)itemH);
                        CreateRoundRectPath(&hoverPath, hoverRect, 6);
                        graphics.FillPath(&hoverBrush, &hoverPath);
                    }
                    
                    // 文字（固定位置，居中显示）
                    COLORREF textColor = (i == 1) ? COLOR_ERROR : COLOR_TEXT;
                    Font font(memDC, g_fontNormal);
                    SolidBrush textBrush(Color(textAlpha, GetRValue(textColor), GetGValue(textColor), GetBValue(textColor)));
                    
                    StringFormat format;
                    format.SetAlignment(StringAlignmentCenter);
                    format.SetLineAlignment(StringAlignmentCenter);
                    RectF textRect((REAL)itemX, (REAL)itemY, (REAL)itemW, (REAL)itemH);
                    graphics.DrawString(items[i], -1, &font, textRect, &format, &textBrush);
                }
            }
            
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            int oldHover = g_trayMenuHover;
            
            // 逆变换：将屏幕坐标转换到缩放前的坐标系
            double scale = g_trayMenuScaleAnim.value;
            if (scale < 0.1) scale = 0.1;  // 避免除零
            
            int centerX = TRAY_MENU_W / 2;
            int centerY = TRAY_MENU_H / 2;
            // 逆变换：先平移到中心，再除以scale，再平移回去
            int localX = (int)((x - centerX) / scale + centerX);
            int localY = (int)((y - centerY) / scale + centerY);
            
            g_trayMenuHover = -1;
            for (int i = 0; i < 2; i++) {
                int itemY = 4 + i * TRAY_MENU_ITEM_H;
                int itemH = TRAY_MENU_ITEM_H - 4;
                if (localY >= itemY && localY < itemY + itemH) {
                    g_trayMenuHover = i;
                    break;
                }
            }
            
            if (oldHover != g_trayMenuHover) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            // 逆变换：将屏幕坐标转换到缩放前的坐标系
            double scale = g_trayMenuScaleAnim.value;
            if (scale < 0.1) scale = 0.1;  // 避免除零
            
            int centerX = TRAY_MENU_W / 2;
            int centerY = TRAY_MENU_H / 2;
            int localX = (int)((x - centerX) / scale + centerX);
            int localY = (int)((y - centerY) / scale + centerY);
            
            for (int i = 0; i < 2; i++) {
                int itemY = 4 + i * TRAY_MENU_ITEM_H;
                int itemH = TRAY_MENU_ITEM_H - 4;
                if (localY >= itemY && localY < itemY + itemH) {
                    if (i == 0) {
                        // 显示窗口
                        ShowWindow(g_hwnd, SW_SHOW);
                        SetForegroundWindow(g_hwnd);
                    } else if (i == 1) {
                        // 退出程序 - 设置标志，动画完成后再发送关闭消息
                        g_trayMenuExitRequested = true;
                    }
                    break;
                }
            }
            
            // 触发关闭动画
            g_trayMenuClosing = true;
            g_trayMenuFadeAnim.target = 0.0;
            g_trayMenuScaleAnim.target = 0.85;
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE && !g_trayMenuClosing) {
                // 失去焦点时触发关闭动画
                g_trayMenuClosing = true;
                g_trayMenuFadeAnim.target = 0.0;
                g_trayMenuScaleAnim.target = 0.85;
            }
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, 1);
            g_trayMenuHwnd = nullptr;
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowTrayMenu(int x, int y) {
    if (!g_trayMenuHwnd) {
        // 注册窗口类
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc = {0};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = TrayMenuProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"VRCLyricsTrayMenu_Class";
            RegisterClassExW(&wc);
            registered = true;
        }
        
        g_trayMenuHwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"VRCLyricsTrayMenu_Class",
            L"",
            WS_POPUP,
            x, y, TRAY_MENU_W, TRAY_MENU_H,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    
    if (g_trayMenuHwnd) {
        // 确保菜单在屏幕内
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        if (x + TRAY_MENU_W > screenW) x = screenW - TRAY_MENU_W;
        if (y + TRAY_MENU_H > screenH) y = screenH - TRAY_MENU_H;
        
        SetWindowPos(g_trayMenuHwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        g_trayMenuHover = -1;
        g_trayMenuVisible = true;
        g_trayMenuClosing = false;
        g_trayMenuExitRequested = false;  // 重置退出请求
        // 重置动画状态
        g_trayMenuFadeAnim.value = 0.0;
        g_trayMenuFadeAnim.target = 1.0;
        g_trayMenuScaleAnim.value = 0.85;
        g_trayMenuScaleAnim.target = 1.0;
        ShowWindow(g_trayMenuHwnd, SW_SHOW);
        SetForegroundWindow(g_trayMenuHwnd);
    }
}
// === End Tray Menu Window ===

void DrawTextRight(HDC hdc, const wchar_t* text, int rightX, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE size;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &size);
    TextOutW(hdc, rightX - size.cx, y, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

void DrawButtonAnim(HDC hdc, int x, int y, int w, int h, const wchar_t* text, Animation& anim, bool accent = false) {
    double hover = anim.value;
    COLORREF baseColor = accent ? COLOR_ACCENT : COLOR_BTN_BG;
    COLORREF hoverColor = accent ? RGB(110, 190, 255) : COLOR_BTN_HOVER;
    
    // 脉冲效果（当accent=true且已连接时）
    double pulse = 0.0;
    if (accent) {
        pulse = g_connectPulseAnim.value * 0.08;  // 降低到8%的微妙变化
    }
    
    int r = (int)(GetRValue(baseColor) + (GetRValue(hoverColor) - GetRValue(baseColor)) * hover + pulse * 30);
    int g = (int)(GetGValue(baseColor) + (GetGValue(hoverColor) - GetGValue(baseColor)) * hover + pulse * 30);
    int b = (int)(GetBValue(baseColor) + (GetBValue(hoverColor) - GetBValue(baseColor)) * hover + pulse * 20);
    
    r = min(255, max(0, r));
    g = min(255, max(0, g));
    b = min(255, max(0, b));
    
    if (hover > 0.1) {
        // 使用GDI+绘制抗锯齿发光边框，完全贴合按钮
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        
        int glowIntensity = (int)((hover + pulse) * 80);
        Gdiplus::Color glowColor(255, min(255, 88 + glowIntensity), min(255, 166 + glowIntensity), 255);
        Gdiplus::Pen glowPen(glowColor, 2.5f);
        
        // 绘制与按钮相同大小的圆角矩形边框
        Gdiplus::GraphicsPath path;
        int cornerRadius = 8;
        Gdiplus::RectF rectF((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
        CreateRoundRectPath(&path, rectF, cornerRadius);
        graphics.DrawPath(&glowPen, &path);
    }
    
    DrawRoundRect(hdc, x, y, w, h, 8, RGB(r, g, b));
    HPEN borderPen = CreatePen(PS_SOLID, 1, accent ? COLOR_ACCENT : COLOR_BORDER);
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, x, y, x + w, y + h, 16, 16);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
    
    // Center text both horizontally and vertically
    DrawTextCenteredBoth(hdc, text, x, y, w, h, accent ? RGB(0,0,0) : COLOR_TEXT, g_fontNormal);
}

void DrawTitleBarButton(HDC hdc, int x, int y, int size, const wchar_t* symbol, Animation& anim) {
    double hover = anim.value;
    COLORREF bgBase = RGB(35, 35, 40);
    COLORREF bgHover = RGB(60, 60, 70);
    int r = (int)(GetRValue(bgBase) + (GetRValue(bgHover) - GetRValue(bgBase)) * hover);
    int g = (int)(GetGValue(bgBase) + (GetGValue(bgHover) - GetGValue(bgBase)) * hover);
    int b = (int)(GetBValue(bgBase) + (GetBValue(bgHover) - GetBValue(bgBase)) * hover);
    DrawRoundRect(hdc, x, y, size, size, 6, RGB(r, g, b));
    // 正确居中：计算文字高度并垂直居中
    HFONT oldFont = (HFONT)SelectObject(hdc, g_fontNormal);
    SIZE sz; GetTextExtentPoint32W(hdc, symbol, (int)wcslen(symbol), &sz);
    TextOutW(hdc, x + (size - sz.cx) / 2, y + (size - sz.cy) / 2, symbol, (int)wcslen(symbol));
    SelectObject(hdc, oldFont);
}

void DrawProgressBar(HDC hdc, int x, int y, int w, int h, double progress) {
    DrawRoundRect(hdc, x, y, w, h, h/2, COLOR_BTN_BG);
    if (progress > 0) {
        int fillW = (int)((w - 4) * progress);
        if (fillW > 4) {
            HPEN pen = CreatePen(PS_SOLID, 1, COLOR_ACCENT);
            HBRUSH brush = CreateSolidBrush(COLOR_ACCENT);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            RoundRect(hdc, x + 2, y + 2, x + 2 + fillW, y + h - 2, h - 4, h - 4);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(pen);
            DeleteObject(brush);
        }
    }
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    
    // 填充背景色（窗口形状由 SetWindowRgn 控制）
    rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    HBRUSH bgBrush = CreateSolidBrush(COLOR_BG);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);
    
    // 绘制标题栏区域
    RECT titleRc; titleRc.left = 0; titleRc.top = 0; titleRc.right = w; titleRc.bottom = TITLEBAR_H + 5;
    HBRUSH titleBrush = CreateSolidBrush(COLOR_TITLEBAR);
    FillRect(memDC, &titleRc, titleBrush);
    DeleteObject(titleBrush);
    
    // 绘制边框让圆角更明显（已注释以移除淡蓝色边框）
    // HPEN borderPen = CreatePen(PS_SOLID, 2, COLOR_BORDER);
    // HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    // HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
    // RoundRect(memDC, 0, 0, w - 1, h - 1, 32, 32);
    // SelectObject(memDC, oldBrush);
    // SelectObject(memDC, oldPen);
    // DeleteObject(borderPen);
    
    // 标题栏发光线
    HPEN glowPen = CreatePen(PS_SOLID, 1, COLOR_ACCENT_GLOW);
    HPEN oldGlowPen = (HPEN)SelectObject(memDC, glowPen);
    MoveToEx(memDC, 32, TITLEBAR_H + 4, nullptr); LineTo(memDC, w - 32, TITLEBAR_H + 4);
    SelectObject(memDC, oldGlowPen);
    DeleteObject(glowPen);
    DrawTextLeft(memDC, L"\x266B \x97F3\x4E50\x663E\x793A", 25, 18, COLOR_TEXT, g_fontSubtitle);
    
    // Update button (between version and minimize)
    int updateBtnX = w - 200;
    if (g_downloadingUpdate) {
        // Show download progress
        wchar_t progressText[32];
        swprintf_s(progressText, L"\x4E0B\x8F7D %d%%", g_downloadProgress);
        DrawTextLeft(memDC, progressText, w - 200, 22, COLOR_ACCENT, g_fontSmall);
    } else {
        // Update button (square, in title bar)
        const wchar_t* updateIcon;
        if (g_updateAvailable && !g_latestVersion.empty()) {
            updateIcon = L"\U0001F504";  // 🔄
        } else if (g_checkingUpdate) {
            updateIcon = L"\U0001F50D";  // 🔍
        } else {
            updateIcon = L"\U0001F50D";  // 🔍
        }
        
        // 绘制更新按钮（带旋转效果）
        int btnX = w - 160;
        int btnY = 14;
        int btnSize = 38;
        double hover = g_btnUpdateAnim.value;
        COLORREF bgBase = RGB(35, 35, 40);
        COLORREF bgHover = RGB(60, 60, 70);
        int r = (int)(GetRValue(bgBase) + (GetRValue(bgHover) - GetRValue(bgBase)) * hover);
        int g = (int)(GetGValue(bgBase) + (GetGValue(bgHover) - GetGValue(bgBase)) * hover);
        int b = (int)(GetBValue(bgBase) + (GetBValue(bgHover) - GetBValue(bgBase)) * hover);
        DrawRoundRect(memDC, btnX, btnY, btnSize, btnSize, 6, RGB(r, g, b));
        
        // 绘制旋转的图标
        if (g_checkingUpdate) {
            // 使用GDI+绘制旋转的搜索图标
            Gdiplus::Graphics graphics(memDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            
            // 设置旋转中心并旋转
            Gdiplus::Matrix matrix;
            int centerX = btnX + btnSize / 2;
            int centerY = btnY + btnSize / 2;
            matrix.RotateAt((Gdiplus::REAL)g_updateRotation, Gdiplus::PointF((Gdiplus::REAL)centerX, (Gdiplus::REAL)centerY));
            graphics.SetTransform(&matrix);
            
            // 绘制搜索图标 (🔍)
            Gdiplus::SolidBrush textBrush(Gdiplus::Color(180, 180, 190));  // 固定深色模式颜色
            Gdiplus::FontFamily fontFamily(L"Segoe UI Emoji");
            Gdiplus::Font font(&fontFamily, 16, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF rectF((Gdiplus::REAL)btnX, (Gdiplus::REAL)btnY, (Gdiplus::REAL)btnSize, (Gdiplus::REAL)btnSize);
            graphics.DrawString(L"\U0001F50D", -1, &font, rectF, &format, &textBrush);
        } else {
            // 正常绘制图标 - 正确居中
            HFONT oldFont = (HFONT)SelectObject(memDC, g_fontNormal);
            SIZE sz; GetTextExtentPoint32W(memDC, updateIcon, (int)wcslen(updateIcon), &sz);
            TextOutW(memDC, btnX + (btnSize - sz.cx) / 2, btnY + (btnSize - sz.cy) / 2, updateIcon, (int)wcslen(updateIcon));
            SelectObject(memDC, oldFont);
        }
        
        // 新版本小绿点提示
        if (g_updateAvailable && !g_downloadingUpdate) {
            int dotSize = 10;
            int dotX = btnX + btnSize - dotSize / 2;
            int dotY = btnY - dotSize / 2 + 2;
            
            // 使用GDI+绘制发光效果的绿点
            Gdiplus::Graphics graphics(memDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
            
            // 发光效果
            for (int i = 3; i >= 0; i--) {
                int glowSize = dotSize + i * 3;
                int alpha = 80 - i * 20;
                Gdiplus::SolidBrush glowBrush(Gdiplus::Color(alpha, 80, 200, 120));
                graphics.FillEllipse(&glowBrush, dotX - glowSize/2 + dotSize/2, dotY - glowSize/2 + dotSize/2, glowSize, glowSize);
            }
            
            // 绿点
            Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, 80, 200, 120));
            graphics.FillEllipse(&dotBrush, dotX, dotY, dotSize, dotSize);
        }
    }

    // Title bar buttons (order: Theme, Update, Minimize, Close)
    // 主题切换按钮已移除，固定深色模式
    // Update button at w - 160 (drawn above)
    DrawTitleBarButton(memDC, w - 110, 14, 38, L"\u2500", g_btnMinAnim);  // Minimize
    DrawTitleBarButton(memDC, w - 60, 14, 38, L"\u2715", g_btnCloseAnim);  // Close
    
    // === Two Column Layout ===
    int leftColW = 260;  // Left column width for settings
    int rightColX = leftColW + CARD_PADDING * 2;  // Right column start X
    int rightColW = w - rightColX - CARD_PADDING;
    int contentY = TITLEBAR_H + 20;
    
    // === LEFT COLUMN (包含标签页) ===
    int panelH = h - contentY - CARD_PADDING;
    // 绘制半透明毛玻璃面板（alpha=70，约73%透明度，比窗口毛玻璃更强）
    DrawRoundRectAlpha(memDC, CARD_PADDING, contentY, leftColW, panelH, 16, COLOR_CARD, 70);
    DrawRoundRect(memDC, CARD_PADDING, contentY, 5, 35, 2, COLOR_ACCENT);
    
    // === Tabs (在卡片内部) ===
    const wchar_t* tabNames[] = { L"\x4E3B\x9875", L"\x6027\x80FD", L"\x8BBE\x7F6E" };  // 主页, 性能, 设置
    int tabW = 70;
    int tabH = 32;
    int tabY = contentY + 10;

    // 先绘制所有非活动标签的背景
    for (int t = 0; t < 3; t++) {
        int tabX = CARD_PADDING + 18 + t * (tabW + 8);
        bool isActive = (t == g_currentTab);
        bool isHover = g_tabHover[t];
        
        // 非活动标签绘制背景（悬停或默认）
        if (!isActive || g_tabSlideAnim.isActive()) {
            COLORREF tabBg = isHover ? RGB(60, 60, 80) : RGB(40, 40, 55);
            DrawRoundRect(memDC, tabX, tabY, tabW, tabH, 8, tabBg);
        }
    }
    
    // 标签页切换动画 - 滑动的高亮背景（绘制在所有标签之上）
    if (g_tabSlideAnim.isActive()) {
        double slideProgress = g_tabSlideAnim.value;
        int prevTabX = CARD_PADDING + 18 + g_prevTab * (tabW + 8);
        int currTabX = CARD_PADDING + 18 + g_currentTab * (tabW + 8);
        
        // 插值计算当前高亮位置
        int animTabX = (int)(prevTabX + (currTabX - prevTabX) * slideProgress);
        
        // 绘制滑动的高亮背景
        COLORREF slideBg = COLOR_ACCENT;
        DrawRoundRect(memDC, animTabX, tabY, tabW, tabH, 8, slideBg);
    } else {
        // 动画结束后，绘制当前活动标签的高亮背景
        int activeTabX = CARD_PADDING + 18 + g_currentTab * (tabW + 8);
        DrawRoundRect(memDC, activeTabX, tabY, tabW, tabH, 8, COLOR_ACCENT);
    }
    
    // 最后绘制所有标签的文字（带颜色渐变动画）
    for (int t = 0; t < 3; t++) {
        int tabX = CARD_PADDING + 18 + t * (tabW + 8);
        bool isActive = (t == g_currentTab);
        bool isAnimating = g_tabSlideAnim.isActive();
        
        // 计算文字颜色（带渐变动画）
        COLORREF tabText;
        if (isAnimating) {
            double slideProgress = g_tabSlideAnim.value;
            int prevTabX = CARD_PADDING + 18 + g_prevTab * (tabW + 8);
            int currTabX = CARD_PADDING + 18 + g_currentTab * (tabW + 8);
            
            // 计算当前标签与滑动高亮块的重叠程度
            double overlap = 0.0;
            
            // 判断是否是目标标签（高亮块正在移动到这个标签）
            bool isTarget = (tabX == currTabX);
            bool isPrev = (tabX == prevTabX);
            
            if (isTarget) {
                // 目标标签：从白色渐变到黑色
                overlap = slideProgress;
            } else if (isPrev) {
                // 前一个标签：从黑色渐变到白色
                overlap = 1.0 - slideProgress;
            }
            
            // 颜色插值：黑色(高亮) <-> 浅色(非高亮)
            int r = (int)(GetRValue(COLOR_TEXT) * (1.0 - overlap) + 0 * overlap);
            int g = (int)(GetGValue(COLOR_TEXT) * (1.0 - overlap) + 0 * overlap);
            int b = (int)(GetBValue(COLOR_TEXT) * (1.0 - overlap) + 0 * overlap);
            tabText = RGB(r, g, b);
        } else {
            tabText = isActive ? RGB(0, 0, 0) : COLOR_TEXT;
        }
        
        DrawTextCenteredBoth(memDC, tabNames[t], tabX, tabY, tabW, tabH, tabText, g_fontNormal);
    }
    
    int rowY = contentY + 55;
    
    int checkboxX = CARD_PADDING + 18;
    int checkboxSize = 26;
    
    // 内容区域滑动动画 - 来回滑动效果
    int contentOffsetX = 0;
    HRGN contentClip = nullptr;
    
    if (g_tabSlideAnim.isActive()) {
        double slideProgress = g_tabSlideAnim.value;
        int slideDistance = leftColW;
        // 反转方向：向右切换时内容从左边滑入，向左切换时内容从右边滑入
        contentOffsetX = (int)(slideDistance * (1.0 - slideProgress) * -g_tabSlideDirection);
        
        // 设置剪辑区域，限制在左侧面板内
        contentClip = CreateRectRgn(CARD_PADDING, contentY + 50, leftColW + CARD_PADDING, h - CARD_PADDING);
        SelectClipRgn(memDC, contentClip);
    }
    
    if (contentOffsetX != 0) {
        OffsetViewportOrgEx(memDC, contentOffsetX, 0, nullptr);
    }
    
    if (g_currentTab == 0) {
        // === MAIN TAB: Platform Status, Connect, Launch ===
        
        // === Platform Status Box ===
        int platformBoxX = CARD_PADDING + 18;
        int platformBoxY = rowY + 22;
        int platformBoxW = leftColW - 36;
        int platformBoxH = 44;
        
        // Determine current platform display
        std::wstring platformText;
        std::wstring methodText;
        bool hasActivePlatform = (g_activePlatform >= 0 && g_activePlatform < g_platformCount);
        
        if (hasActivePlatform) {
            platformText = g_platforms[g_activePlatform].name;
            methodText = g_platforms[g_activePlatform].connectMethod;
        } else {
            // Check if any platform is connected but not playing
            int connectedPlatform = -1;
            for (int i = 0; i < g_platformCount; i++) {
                if (g_platforms[i].connected) {
                    connectedPlatform = i;
                    break;
                }
            }
            if (connectedPlatform >= 0) {
                platformText = g_platforms[connectedPlatform].name;
                methodText = g_platforms[connectedPlatform].connectMethod;
            } else {
                platformText = L"\x672A\x68C0\x6D4B\x5230\x97F3\x4E50";  // 未检测到音乐
                methodText = L"";
            }
        }
        
        // Draw platform box
        bool isHover = g_platformBoxHover;
        COLORREF boxBg = isHover ? COLOR_BOX_HOVER : COLOR_BOX_BG;
        COLORREF boxBorder = isHover ? COLOR_BOX_BORDER_HOVER : COLOR_BOX_BORDER;
        DrawRoundRectWithBorder(memDC, platformBoxX, platformBoxY, platformBoxW, platformBoxH, 8, boxBg, boxBorder);
        
        // Draw platform name (left side, vertically centered)
        DrawTextVCentered(memDC, platformText.c_str(), platformBoxX + 12, platformBoxY, platformBoxH, 
            hasActivePlatform ? COLOR_TEXT : COLOR_TEXT_DIM, g_fontNormal);
        
        // Draw method/status on right side (before arrow, vertically centered)
        if (!methodText.empty()) {
            DrawTextVCenteredRight(memDC, methodText.c_str(), platformBoxX + platformBoxW - 30, platformBoxY, platformBoxH, 
                RGB(110, 190, 140), g_fontSmall);
        }
        
        // Draw dropdown arrow on right side (with rotation animation)
        int arrowX = platformBoxX + platformBoxW - 16;
        int arrowY = platformBoxY + platformBoxH / 2;
        float rotation = (float)(g_arrowRotationAnim.value * 180.0);  // 0-180度旋转
        
        // 使用GDI+绘制旋转箭头
        {
            Gdiplus::Graphics graphics(memDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            
            // 设置旋转变换
            Gdiplus::Matrix matrix;
            matrix.RotateAt(rotation, Gdiplus::PointF((Gdiplus::REAL)arrowX, (Gdiplus::REAL)arrowY));
            graphics.SetTransform(&matrix);
            
            // 绘制箭头
            Gdiplus::Pen arrowPen(Gdiplus::Color(GetRValue(COLOR_TEXT_DIM), GetGValue(COLOR_TEXT_DIM), GetBValue(COLOR_TEXT_DIM)), 2.0f);
            Gdiplus::GraphicsPath arrowPath;
            arrowPath.AddLine(arrowX - 5, arrowY - 3, arrowX, arrowY + 2);
            arrowPath.AddLine(arrowX, arrowY + 2, arrowX + 5, arrowY - 3);
            graphics.DrawPath(&arrowPen, &arrowPath);
        }
        
        // === Platform Selection Dropdown Menu (draw AFTER buttons but BEFORE them in z-order concept) ===
        // We'll draw it at the end for proper z-order
        int menuExtraSpace = 0;  // Extra space to push buttons down when menu is open
        double menuExpandFactor = g_menuExpandAnim.value;  // 菜单展开动画值
        // 只有菜单打开或正在收起时才计算额外空间
        bool isMenuVisible = g_platformMenuOpen || (g_menuExpandAnim.isActive() && menuExpandFactor > 0.01);
        if (isMenuVisible) {
            int menuItemH = 36;
            menuExtraSpace = (int)((g_platformCount * menuItemH + 12) * menuExpandFactor);  // 动画高度
        }
        
        // === Connect Button ===
        rowY += 80 + menuExtraSpace;  // Push down if menu is open (increased spacing)
        DrawButtonAnim(memDC, CARD_PADDING + 18, rowY, leftColW - 36, 40, 
            g_isConnected ? L"\x91CD\x65B0\x8FDE\x63A5" : L"\x8FDE\x63A5", g_btnConnectAnim, g_isConnected);
        
        // === Launch Netease Button (only when Netease not connected) ===
        if (!g_neteaseConnected) {
            rowY += 48;
            DrawButtonAnim(memDC, CARD_PADDING + 18, rowY, leftColW - 36, 36,
                L"\x542F\x52A8\x7F51\x6613\x4E91\x97F3\x4E50", g_btnLaunchAnim, false);
        }
        
        // === Platform Selection Dropdown Menu (draw LAST so it's on top) ===
        if (isMenuVisible) {
            int menuItemH = 36;
            int fullMenuH = g_platformCount * menuItemH + 8;
            int menuH = (int)(fullMenuH * menuExpandFactor);  // 动画高度
            int menuX = platformBoxX;
            int menuY = platformBoxY + platformBoxH;  // 从平台框底部开始
            int menuW = platformBoxW;
            
            // Menu background - 使用圆角矩形（只有底部圆角）
            {
                Graphics graphics(memDC);
                graphics.SetSmoothingMode(SmoothingModeHighQuality);
                
                int cornerRadius = 8;
                int d = cornerRadius * 2;
                
                // 创建背景路径：顶部直角，底部圆角
                GraphicsPath bgPath;
                bgPath.StartFigure();
                
                // 从左上角开始，顺时针绘制
                // 顶边（左到右）
                bgPath.AddLine(menuX, menuY, menuX + menuW, menuY);
                // 右边（上到下，到右下角圆角起点）
                bgPath.AddLine(menuX + menuW, menuY, menuX + menuW, menuY + menuH - cornerRadius);
                // 右下角圆角：从右边(0°)画到底边(90°)
                bgPath.AddArc(menuX + menuW - d, menuY + menuH - d, d, d, 0, 90);
                // 底边（右到左）
                bgPath.AddLine(menuX + menuW - cornerRadius, menuY + menuH, menuX + cornerRadius, menuY + menuH);
                // 左下角圆角：从底边(90°)画到左边(180°)
                bgPath.AddArc(menuX, menuY + menuH - d, d, d, 90, 90);
                // 左边（下到上，回到起点）
                bgPath.AddLine(menuX, menuY + menuH - cornerRadius, menuX, menuY);
                
                bgPath.CloseFigure();
                
                // 填充背景
                SolidBrush bgBrush(Color(255, GetRValue(COLOR_MENU_BG), GetGValue(COLOR_MENU_BG), GetBValue(COLOR_MENU_BG)));
                graphics.FillPath(&bgBrush, &bgPath);
                
                // 绘制边框（左、底、右，不画顶边）
                Pen borderPen(Color(255, GetRValue(COLOR_MENU_BORDER), GetGValue(COLOR_MENU_BORDER), GetBValue(COLOR_MENU_BORDER)));
                
                // 左边框
                graphics.DrawLine(&borderPen, menuX, menuY, menuX, menuY + menuH - cornerRadius);
                // 左下角圆角边框
                graphics.DrawArc(&borderPen, menuX, menuY + menuH - d, d, d, 90, 90);
                // 底边框
                graphics.DrawLine(&borderPen, menuX + cornerRadius, menuY + menuH, menuX + menuW - cornerRadius, menuY + menuH);
                // 右下角圆角边框
                graphics.DrawArc(&borderPen, menuX + menuW - d, menuY + menuH - d, d, d, 0, 90);
                // 右边框
                graphics.DrawLine(&borderPen, menuX + menuW, menuY + menuH - cornerRadius, menuX + menuW, menuY);
            }
            
            // Menu items (only draw when expanded enough)
            if (menuExpandFactor > 0.3) {
                for (int i = 0; i < g_platformCount; i++) {
                    int itemY = menuY + 4 + i * menuItemH;
                    // 跳过超出当前动画高度的项目
                    if (itemY + menuItemH > menuY + menuH) break;
                    
                    // 逐条淡入效果
                    double itemAlpha = g_menuItemAnims[i].value;
                    if (itemAlpha < 0.01) continue;  // 跳过完全透明的项目
                    
                    bool itemHover = (g_platformMenuHover == i);
                    bool isCurrentActive = (g_activePlatform == i);
                    bool isConnected = g_platforms[i].connected;
                    
                    // Item background on hover (with alpha)
                    if (itemHover) {
                        COLORREF hoverColor = COLOR_MENU_HOVER;
                        // 根据alpha调整颜色
                        int r = GetRValue(hoverColor);
                        int g = GetGValue(hoverColor);
                        int b = GetBValue(hoverColor);
                        DrawRoundRectAlpha(memDC, menuX + 4, itemY, menuW - 8, menuItemH - 4, 4, 
                            RGB(r, g, b), (int)(itemAlpha * 255));
                    }
                    
                    // Platform name (left side, vertically centered in item) - with alpha
                    COLORREF nameColor = isConnected ? COLOR_TEXT : COLOR_TEXT_DIM;
                    SetTextColor(memDC, nameColor);
                    SetBkMode(memDC, TRANSPARENT);
                    HFONT oldFont = (HFONT)SelectObject(memDC, g_fontNormal);
                    // 使用alpha混合绘制文字
                    if (itemAlpha >= 0.99) {
                        DrawTextVCentered(memDC, g_platforms[i].name.c_str(), menuX + 12, itemY, menuItemH - 4, nameColor, g_fontNormal);
                    } else {
                        // 半透明文字
                        int alpha = (int)(itemAlpha * 255);
                        int nr = (GetRValue(COLOR_BG) * (255 - alpha) + GetRValue(nameColor) * alpha) / 255;
                        int ng = (GetGValue(COLOR_BG) * (255 - alpha) + GetGValue(nameColor) * alpha) / 255;
                        int nb = (GetBValue(COLOR_BG) * (255 - alpha) + GetBValue(nameColor) * alpha) / 255;
                        DrawTextVCentered(memDC, g_platforms[i].name.c_str(), menuX + 12, itemY, menuItemH - 4, RGB(nr, ng, nb), g_fontNormal);
                    }
                    SelectObject(memDC, oldFont);
                    
                    // Connection status (right side, vertically centered)
                    std::wstring statusText;
                    COLORREF statusColor;
                    if (isCurrentActive) {
                        statusText = L"\x5F53\x524D";  // 当前
                        statusColor = COLOR_SUCCESS;
                    } else if (isConnected) {
                        statusText = g_platforms[i].connectMethod;
                        statusColor = RGB(110, 190, 140);
                    } else {
                        statusText = L"\x672A\x8FDE\x63A5";  // 未连接
                        statusColor = COLOR_TEXT_DIM;
                    }
                    // 状态文字也应用alpha
                    if (itemAlpha >= 0.99) {
                        DrawTextRight(memDC, statusText.c_str(), menuX + menuW - 16, itemY + 10, statusColor, g_fontSmall);
                    } else {
                        int alpha = (int)(itemAlpha * 255);
                        int sr = (GetRValue(COLOR_MENU_BG) * (255 - alpha) + GetRValue(statusColor) * alpha) / 255;
                        int sg = (GetGValue(COLOR_MENU_BG) * (255 - alpha) + GetGValue(statusColor) * alpha) / 255;
                        int sb = (GetBValue(COLOR_MENU_BG) * (255 - alpha) + GetBValue(statusColor) * alpha) / 255;
                        DrawTextRight(memDC, statusText.c_str(), menuX + menuW - 16, itemY + 10, RGB(sr, sg, sb), g_fontSmall);
                    }
                }
            }
        }
    } else if (g_currentTab == 1) {
        // === PERFORMANCE TAB: Display Mode, System Info, Buttons ===

        // === Display Mode Selector (Music / Performance) ===
        int modeBtnW = 70;
        int modeBtnH = 32;
        int modeBtnSpacing = 6;
        int totalModeBtnW = 2 * modeBtnW + modeBtnSpacing;
        int modeBtnStartX = CARD_PADDING + 18 + (leftColW - 36 - totalModeBtnW) / 2;
        int modeBtnY = rowY + 30;

        // Display mode label
        DrawTextLeft(memDC, L"\x663E\x793A\x6A21\x5F0F:", CARD_PADDING + 18, rowY, COLOR_TEXT_DIM, g_fontLabel);

        // Draw mode buttons with slide animation (like tabs)
        for (int m = 0; m < 2; m++) {
            int btnX = modeBtnStartX + m * (modeBtnW + modeBtnSpacing);
            bool isActive = (g_performanceMode == m);
            bool isHover = g_displayModeHover[m];

            if (!isActive || g_displayModeSlideAnim.isActive()) {
                COLORREF btnBg = isHover ? RGB(60, 60, 80) : RGB(40, 40, 55);
                DrawRoundRect(memDC, btnX, modeBtnY, modeBtnW, modeBtnH, 8, btnBg);
            }
        }

        // Slide animation for mode selector
        if (g_displayModeSlideAnim.isActive()) {
            double slideProgress = g_displayModeSlideAnim.value;
            int prevBtnX = modeBtnStartX + g_prevDisplayMode * (modeBtnW + modeBtnSpacing);
            int currBtnX = modeBtnStartX + g_performanceMode * (modeBtnW + modeBtnSpacing);
            int animBtnX = (int)(prevBtnX + (currBtnX - prevBtnX) * slideProgress);
            DrawRoundRect(memDC, animBtnX, modeBtnY, modeBtnW, modeBtnH, 8, COLOR_ACCENT);
        } else {
            int activeBtnX = modeBtnStartX + g_performanceMode * (modeBtnW + modeBtnSpacing);
            DrawRoundRect(memDC, activeBtnX, modeBtnY, modeBtnW, modeBtnH, 8, COLOR_ACCENT);
        }

        // Draw mode text
        const wchar_t* modeNames[] = { L"\x97F3\x4E50", L"\x6027\x80FD" };
        for (int m = 0; m < 2; m++) {
            int btnX = modeBtnStartX + m * (modeBtnW + modeBtnSpacing);
            bool isActive = (g_performanceMode == m);
            COLORREF textColor = isActive ? RGB(255, 255, 255) : COLOR_TEXT;
            DrawTextCentered(memDC, modeNames[m], btnX + modeBtnW / 2, modeBtnY + 3, textColor, g_fontNormal);
        }

        // === System Info Section (only in Performance mode, with animation) ===
        double expandAnim = g_systemInfoExpandAnim.value;
        int infoH = (int)(140 * expandAnim);
        bool showSystemInfo = (g_performanceMode == 1 && expandAnim > 0.01) || (g_systemInfoExpandAnim.isActive() && expandAnim > 0.01);

        if (showSystemInfo && infoH > 4) {
            int infoX = CARD_PADDING + 18;
            int infoY = modeBtnY + modeBtnH + 20;
            int infoW = leftColW - 36;

            // Background with alpha
            COLORREF infoBg = COLOR_BOX_BG;
            if (expandAnim < 1.0) {
                int alpha = (int)(255 * expandAnim);
                DrawRoundRectAlpha(memDC, infoX, infoY, infoW, infoH, 8, infoBg, alpha);
            } else {
                DrawRoundRect(memDC, infoX, infoY, infoW, infoH, 8, infoBg);
            }

            // Clip to animation region
            HRGN clipRgn = nullptr;
            if (expandAnim < 1.0 && infoH > 0) {
                clipRgn = CreateRectRgn(infoX, infoY, infoX + infoW, infoY + infoH);
                SelectClipRgn(memDC, clipRgn);
            }

            // Calculate alpha for fade effect
            int textAlpha = min(255, (int)(255 * expandAnim * 1.2));  // 文字淡入稍快
            
            // Use GDI+ for alpha-blended text and controls (limited scope)
            {
                Graphics graphics(memDC);
                graphics.SetSmoothingMode(SmoothingModeHighQuality);

                // CPU input
                int inputH = 36;
                int inputY = infoY + 15;
                
                // CPU label with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT_DIM), GetGValue(COLOR_TEXT_DIM), GetBValue(COLOR_TEXT_DIM)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)infoX, (REAL)(inputY + 7));
                    graphics.DrawString(L"CPU", -1, &font, pt, &textBrush);
                }
                
                bool cpuEditing = (g_editingField == EDIT_CPU_NAME);
                COLORREF cpuBorder = cpuEditing ? COLOR_ACCENT : COLOR_BOX_BORDER;
                
                // CPU input box with alpha
                {
                    int boxAlpha = (int)(255 * expandAnim);
                    SolidBrush boxBrush(Color(boxAlpha, GetRValue(COLOR_EDIT_BG), GetGValue(COLOR_EDIT_BG), GetBValue(COLOR_EDIT_BG)));
                    RectF rect((REAL)(infoX + 50), (REAL)inputY, (REAL)(infoW - 50), (REAL)inputH);
                    GraphicsPath path;
                    CreateRoundRectPath(&path, rect, 6);
                    graphics.FillPath(&boxBrush, &path);
                    
                    // Border
                    Pen borderPen(Color(boxAlpha, GetRValue(cpuBorder), GetGValue(cpuBorder), GetBValue(cpuBorder)));
                    graphics.DrawPath(&borderPen, &path);
                }

                // CPU text with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT), GetGValue(COLOR_TEXT), GetBValue(COLOR_TEXT)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)(infoX + 60), (REAL)(inputY + 7));
                    graphics.DrawString(g_cpuDisplayName.c_str(), -1, &font, pt, &textBrush);
                }

                // Draw cursor if editing and visible
                if (cpuEditing && g_cursorVisible) {
                    int cursorX = infoX + 60 + GetTextWidth(memDC, g_cpuDisplayName.c_str(), g_fontNormal, g_cursorPos);
                    HPEN pen = CreatePen(PS_SOLID, 2, COLOR_TEXT);
                    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                    MoveToEx(memDC, cursorX, inputY + 10, nullptr);
                    LineTo(memDC, cursorX, inputY + 26);
                    SelectObject(memDC, oldPen);
                    DeleteObject(pen);
                }

                // RAM input
                inputY += 45;
                
                // RAM label with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT_DIM), GetGValue(COLOR_TEXT_DIM), GetBValue(COLOR_TEXT_DIM)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)infoX, (REAL)(inputY + 7));
                    graphics.DrawString(L"RAM", -1, &font, pt, &textBrush);
                }
                
                bool ramEditing = (g_editingField == EDIT_RAM_NAME);
                COLORREF ramBorder = ramEditing ? COLOR_ACCENT : COLOR_BOX_BORDER;
                
                // RAM input box with alpha
                {
                    int boxAlpha = (int)(255 * expandAnim);
                    SolidBrush boxBrush(Color(boxAlpha, GetRValue(COLOR_EDIT_BG), GetGValue(COLOR_EDIT_BG), GetBValue(COLOR_EDIT_BG)));
                    RectF rect((REAL)(infoX + 50), (REAL)inputY, (REAL)(infoW - 50), (REAL)inputH);
                    GraphicsPath path;
                    CreateRoundRectPath(&path, rect, 6);
                    graphics.FillPath(&boxBrush, &path);
                    
                    Pen borderPen(Color(boxAlpha, GetRValue(ramBorder), GetGValue(ramBorder), GetBValue(ramBorder)));
                    graphics.DrawPath(&borderPen, &path);
                }

                // RAM text with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT), GetGValue(COLOR_TEXT), GetBValue(COLOR_TEXT)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)(infoX + 60), (REAL)(inputY + 7));
                    graphics.DrawString(g_ramDisplayName.c_str(), -1, &font, pt, &textBrush);
                }

                // Draw cursor if editing and visible
                if (ramEditing && g_cursorVisible) {
                    int cursorX = infoX + 60 + GetTextWidth(memDC, g_ramDisplayName.c_str(), g_fontNormal, g_cursorPos);
                    HPEN pen = CreatePen(PS_SOLID, 2, COLOR_TEXT);
                    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                    MoveToEx(memDC, cursorX, inputY + 10, nullptr);
                    LineTo(memDC, cursorX, inputY + 26);
                    SelectObject(memDC, oldPen);
                    DeleteObject(pen);
                }

                // GPU input
                inputY += 45;
                
                // GPU label with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT_DIM), GetGValue(COLOR_TEXT_DIM), GetBValue(COLOR_TEXT_DIM)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)infoX, (REAL)(inputY + 7));
                    graphics.DrawString(L"GPU", -1, &font, pt, &textBrush);
                }
                
                bool gpuEditing = (g_editingField == EDIT_GPU_NAME);
                COLORREF gpuBorder = gpuEditing ? COLOR_ACCENT : COLOR_BOX_BORDER;
                
                // GPU input box with alpha
                {
                    int boxAlpha = (int)(255 * expandAnim);
                    SolidBrush boxBrush(Color(boxAlpha, GetRValue(COLOR_EDIT_BG), GetGValue(COLOR_EDIT_BG), GetBValue(COLOR_EDIT_BG)));
                    RectF rect((REAL)(infoX + 50), (REAL)inputY, (REAL)(infoW - 50), (REAL)inputH);
                    GraphicsPath path;
                    CreateRoundRectPath(&path, rect, 6);
                    graphics.FillPath(&boxBrush, &path);
                    
                    Pen borderPen(Color(boxAlpha, GetRValue(gpuBorder), GetGValue(gpuBorder), GetBValue(gpuBorder)));
                    graphics.DrawPath(&borderPen, &path);
                }

                // GPU text with alpha
                {
                    SolidBrush textBrush(Color(textAlpha, GetRValue(COLOR_TEXT), GetGValue(COLOR_TEXT), GetBValue(COLOR_TEXT)));
                    Font font(memDC, g_fontNormal);
                    PointF pt((REAL)(infoX + 60), (REAL)(inputY + 7));
                    graphics.DrawString(g_gpuDisplayName.c_str(), -1, &font, pt, &textBrush);
                }

                // Draw cursor if editing and visible
                if (gpuEditing && g_cursorVisible) {
                    int cursorX = infoX + 60 + GetTextWidth(memDC, g_gpuDisplayName.c_str(), g_fontNormal, g_cursorPos);
                    HPEN pen = CreatePen(PS_SOLID, 2, COLOR_TEXT);
                    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                    MoveToEx(memDC, cursorX, inputY + 10, nullptr);
                    LineTo(memDC, cursorX, inputY + 26);
                    SelectObject(memDC, oldPen);
                    DeleteObject(pen);
                }
            }  // Graphics 对象在这里销毁

            // Cancel clipping (after Graphics object is destroyed)
            if (clipRgn) {
                SelectClipRgn(memDC, nullptr);
                DeleteObject(clipRgn);
            }
        }

        // === Auto Detect Button ===
        int btnW = leftColW - 36;
        int btnH = 36;
        // 按钮位置与系统信息区域同步：只有当区域可见时才移动
        double btnExpandAnim = showSystemInfo ? expandAnim : 0.0;
        int baseBtnY = modeBtnY + modeBtnH + 20;
        int expandedBtnY = baseBtnY + 140 + 20;
        int btnY = (int)(baseBtnY + (expandedBtnY - baseBtnY) * btnExpandAnim);
        DrawButtonAnim(memDC, CARD_PADDING + 18, btnY, btnW, btnH, L"\x81EA\x52A8\x68C0\x6D4B", g_btnAutoDetectAnim, false);

        // === Show Order Button ===
        btnY += 44;
        DrawButtonAnim(memDC, CARD_PADDING + 18, btnY, btnW, btnH, L"\x663E\x793A\x987A\x5E8F", g_btnShowOrderAnim, false);

    } else {
        // === SETTINGS TAB: IP, Port, Checkboxes ===
        DrawTextLeft(memDC, L"OSC IP:", CARD_PADDING + 18, rowY, COLOR_TEXT_DIM, g_fontSmall);
        DrawRoundRect(memDC, CARD_PADDING + 18, rowY + 24, leftColW - 36, 36, 6, COLOR_EDIT_BG);
        DrawTextLeft(memDC, g_oscIp.c_str(), CARD_PADDING + 30, rowY + 30, COLOR_TEXT, g_fontNormal);
        
        rowY += 75;
        DrawTextLeft(memDC, L"OSC \x7AEF\x53E3:", CARD_PADDING + 18, rowY, COLOR_TEXT_DIM, g_fontSmall);
        DrawRoundRect(memDC, CARD_PADDING + 18, rowY + 24, leftColW - 36, 36, 6, COLOR_EDIT_BG);
        wchar_t portStr[16];
        swprintf_s(portStr, L"%d", g_oscPort);
        DrawTextLeft(memDC, portStr, CARD_PADDING + 30, rowY + 30, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Show Performance ===
        rowY += 70;
        {
            bool checked = g_showPerfOnPause;
            bool hover = g_checkboxHover[0];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            // 悬停且未选中时绘制边框
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                // 绘制圆角矩形边框
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x6682\x505C\x7EDF\x8BA1", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Auto Update ===
        rowY += 38;
        {
            bool checked = g_autoUpdate;
            bool hover = g_checkboxHover[1];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x81EA\x52A8\x66F4\x65B0", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Show Platform ===
        rowY += 38;
        {
            bool checked = g_showPlatform;
            bool hover = g_checkboxHover[2];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x663E\x793A\x5E73\x53F0", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Minimize to Tray ===
        rowY += 38;
        {
            bool checked = g_minimizeToTray;
            bool hover = g_checkboxHover[3];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x6700\x5C0F\x5316\x5230\x6258\x76D8", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);

        // === Checkbox: Start Minimized ===
        rowY += 38;
        {
            bool checked = g_startMinimized;
            bool hover = g_checkboxHover[4];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x542F\x52A8\x6700\x5C0F\x5316", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);

        // === Checkbox: Auto Start ===
        rowY += 38;
        {
            bool checked = g_autoStart;
            bool hover = g_checkboxHover[5];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x5F00\x673A\x81EA\x542F", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);

        // === Checkbox: Run as Admin ===
        rowY += 38;
        {
            bool checked = g_runAsAdmin;
            bool hover = g_checkboxHover[6];
            COLORREF cbBg = checked ? COLOR_CHECK_ACCENT : (hover ? COLOR_BTN_HOVER : COLOR_CHECK_BG);
            DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
            if (hover && !checked) {
                HPEN hoverPen = CreatePen(PS_SOLID, 2, COLOR_ACCENT);
                HPEN oldPen = (HPEN)SelectObject(memDC, hoverPen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, checkboxX, rowY, checkboxX + checkboxSize, rowY + checkboxSize, 8, 8);
                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hoverPen);
            }
            if (checked) {
                HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
                HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
                MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
                LineTo(memDC, checkboxX + 10, rowY + 18);
                LineTo(memDC, checkboxX + 20, rowY + 8);
                SelectObject(memDC, oldTickPen);
                DeleteObject(tickPen);
            }
        }
        DrawTextVCentered(memDC, L"\x7BA1\x7406\x5458\x542F\x52A8", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Admin Status ===
        rowY += 38;
        bool isAdmin = IsRunningAsAdmin();
        if (isAdmin) {
            DrawTextLeft(memDC, L"\x2705 \x5DF2\x662F\x7BA1\x7406\x5458\x6743\x9650", checkboxX, rowY + 4, COLOR_SUCCESS, g_fontSmall);
        } else {
            COLORREF linkColor = g_btnAdminHover ? RGB(130, 200, 255) : COLOR_ACCENT;
            DrawTextLeft(memDC, L"\x26A0 \x70B9\x51FB\x4EE5\x7BA1\x7406\x5458\x8FD0\x884C", checkboxX, rowY + 4, linkColor, g_fontSmall);
            HPEN linkPen = CreatePen(PS_SOLID, 1, linkColor);
            HPEN oldPen = (HPEN)SelectObject(memDC, linkPen);
            MoveToEx(memDC, checkboxX, rowY + 24, nullptr);
            LineTo(memDC, checkboxX + 160, rowY + 24);
            SelectObject(memDC, oldPen);
            DeleteObject(linkPen);
        }
        
        // === Hotkey Setting ===
        rowY += 50;
        DrawTextLeft(memDC, L"\x6682\x505C\x5FEB\x6377\x952E", checkboxX, rowY + 4, COLOR_TEXT_DIM, g_fontSmall);
        
        // Hotkey display box
        int hotkeyBoxX = checkboxX + 90;
        int hotkeyBoxW = 120;
        int hotkeyBoxH = 32;
        
        // Draw hotkey box background
        COLORREF hotkeyBg = g_editingHotkey ? COLOR_ACCENT : (g_hotkeyBoxHover ? RGB(60, 60, 70) : RGB(45, 45, 55));
        DrawRoundRect(memDC, hotkeyBoxX, rowY, hotkeyBoxW, hotkeyBoxH, 6, hotkeyBg);
        
        // Draw hotkey text
        wchar_t hotkeyText[32] = {0};
        if (g_editingHotkey) {
            wcscpy_s(hotkeyText, L"\x8BF7\x6309\x4E0B\x65B0\x952E...");
        } else {
            // Build hotkey display string
            std::wstring hk;
            if (g_oscPauseHotkeyMods & MOD_CONTROL) hk += L"Ctrl+";
            if (g_oscPauseHotkeyMods & MOD_ALT) hk += L"Alt+";
            if (g_oscPauseHotkeyMods & MOD_SHIFT) hk += L"Shift+";
            if (g_oscPauseHotkeyMods & MOD_WIN) hk += L"Win+";
            
            // Key name
            if (g_oscPauseHotkey >= VK_F1 && g_oscPauseHotkey <= VK_F24) {
                hk += L"F" + std::to_wstring(g_oscPauseHotkey - VK_F1 + 1);
            } else if (g_oscPauseHotkey == VK_SPACE) {
                hk += L"Space";
            } else if (g_oscPauseHotkey == VK_RETURN) {
                hk += L"Enter";
            } else if (g_oscPauseHotkey == VK_ESCAPE) {
                hk += L"Esc";
            } else if (g_oscPauseHotkey == VK_TAB) {
                hk += L"Tab";
            } else if (g_oscPauseHotkey == VK_INSERT) {
                hk += L"Ins";
            } else if (g_oscPauseHotkey == VK_DELETE) {
                hk += L"Del";
            } else if (g_oscPauseHotkey == VK_HOME) {
                hk += L"Home";
            } else if (g_oscPauseHotkey == VK_END) {
                hk += L"End";
            } else if (g_oscPauseHotkey == VK_PRIOR) {
                hk += L"PgUp";
            } else if (g_oscPauseHotkey == VK_NEXT) {
                hk += L"PgDn";
            } else if (g_oscPauseHotkey >= '0' && g_oscPauseHotkey <= '9') {
                hk += (wchar_t)g_oscPauseHotkey;
            } else if (g_oscPauseHotkey >= 'A' && g_oscPauseHotkey <= 'Z') {
                hk += (wchar_t)g_oscPauseHotkey;
            } else {
                hk += L"?";
            }
            wcscpy_s(hotkeyText, hk.c_str());
        }
        
        // Center text in hotkey box
        SIZE textSize;
        GetTextExtentPoint32W(memDC, hotkeyText, (int)wcslen(hotkeyText), &textSize);
        int textX = hotkeyBoxX + (hotkeyBoxW - textSize.cx) / 2;
        int textY = rowY + (hotkeyBoxH - textSize.cy) / 2;
        SetTextColor(memDC, g_editingHotkey ? RGB(0, 0, 0) : COLOR_TEXT);
        SetBkMode(memDC, TRANSPARENT);
        SelectObject(memDC, g_fontNormal);
        TextOutW(memDC, textX, textY, hotkeyText, (int)wcslen(hotkeyText));
        
        // === Export Log Button ===
        rowY += 40;
        // 只有当按钮在可见区域内时才绘制
        if (rowY + 38 < h - CARD_PADDING) {
            DrawButtonAnim(memDC, checkboxX, rowY, leftColW - 36 - checkboxX, 38, 
                L"\x5BFC\x51FA\x65E5\x5FD7", g_btnExportLogAnim, false);
        }
    }
    
    // 恢复视口偏移和剪辑区域
    if (contentOffsetX != 0) {
        OffsetViewportOrgEx(memDC, -contentOffsetX, 0, nullptr);
    }
    if (contentClip) {
        SelectClipRgn(memDC, nullptr);  // 恢复无剪辑状态
        DeleteObject(contentClip);
    }
    
    // === RIGHT COLUMN: OSC Message Preview ===
    int previewH = h - contentY - CARD_PADDING;
    // 绘制半透明毛玻璃面板（alpha=70，约73%透明度，比窗口毛玻璃更强）
    DrawRoundRectAlpha(memDC, rightColX, contentY, rightColW, previewH, 16, COLOR_CARD, 70);
    DrawRoundRect(memDC, rightColX, contentY, 5, 35, 2, COLOR_WARNING);
    DrawTextLeft(memDC, L"OSC \x6D88\x606F\x9884\x89C8", rightColX + 18, contentY + 10, COLOR_TEXT, g_fontNormal);
    
    // 设置右侧面板的剪辑区域
    HRGN rightClip = CreateRectRgn(rightColX + 5, contentY + 40, rightColX + rightColW - 5, contentY + previewH - 5);
    HRGN oldRightClip = CreateRectRgn(0, 0, 0, 0);
    GetClipRgn(memDC, oldRightClip);
    SelectClipRgn(memDC, rightClip);
    
    // Get current data and build SongInfo for preview
    moekoe::SongInfo previewInfo;
    
    EnterCriticalSection(&g_cs);
    previewInfo.title = g_pendingTitle;
    previewInfo.artist = g_pendingArtist;
    previewInfo.duration = g_pendingDuration;
    previewInfo.currentTime = g_pendingCurrentTime;
    previewInfo.isPlaying = g_pendingIsPlaying;
    previewInfo.hasData = !g_pendingTitle.empty();
    previewInfo.lyrics = g_pendingLyrics;
    LeaveCriticalSection(&g_cs);
    
    // Format OSC message using the actual function
    std::wstring oscMessage;
    
    // Use cached preview message with rate limiting
    DWORD now = GetTickCount();
    bool needUpdate = false;
    
    EnterCriticalSection(&g_cs);
    // Update preview if: first time, interval passed, or play state changed
    if (g_cachedPreviewMsg.empty() || 
        (now - g_lastPreviewUpdate) >= PREVIEW_UPDATE_INTERVAL ||
        g_playStateChanged) {
        needUpdate = true;
    }
    LeaveCriticalSection(&g_cs);
    
    if (needUpdate) {
        moekoe::SongInfo previewInfo;
        EnterCriticalSection(&g_cs);
        previewInfo.title = g_pendingTitle;
        previewInfo.artist = g_pendingArtist;
        previewInfo.duration = g_pendingDuration;
        previewInfo.currentTime = g_pendingCurrentTime;
        previewInfo.isPlaying = g_pendingIsPlaying;
        previewInfo.hasData = !g_pendingTitle.empty();
        previewInfo.lyrics = g_pendingLyrics;
        LeaveCriticalSection(&g_cs);
        
        // 根据显示模式选择预览消息
        if (g_performanceMode == 1) {
            g_cachedPreviewMsg = BuildPerformanceOSCMessage(0);
        } else {
            g_cachedPreviewMsg = FormatOSCMessage(previewInfo);
        }
        g_lastPreviewUpdate = now;
    }
    oscMessage = g_cachedPreviewMsg;
    
    // Draw OSC message preview with word wrap
    int msgY = contentY + 55;
    int msgX = rightColX + 18;
    int lineHeight = 28;
    int maxLines = (previewH - 100) / lineHeight;  // Leave more space at bottom
    // 修正宽度计算：面板宽度减去左右边距（18+18=36），再留一些安全边距
    int maxTextWidth = rightColW - 50;  // 更保守的宽度计算
    
    int lineCount = 0;
    
    // Helper to measure text width
    auto getTextWidth = [&](const std::wstring& text) -> int {
        SIZE sz;
        GetTextExtentPoint32W(memDC, text.c_str(), (int)text.length(), &sz);
        return sz.cx;
    };
    
    // Helper lambda to draw a line with word wrap based on actual pixel width
    auto drawLine = [&](const std::wstring& line) {
        if (lineCount >= maxLines) return false;
        
        std::wstring remaining = line;
        while (!remaining.empty() && lineCount < maxLines) {
            // Find how many characters fit in maxTextWidth
            // 使用二分查找提高效率
            size_t lo = 1, hi = remaining.length(), fitLen = remaining.length();
            
            // 先检查整行是否能放下
            if (getTextWidth(remaining) <= maxTextWidth) {
                fitLen = remaining.length();
            } else {
                // 二分查找最大能放下的字符数
                while (lo <= hi) {
                    size_t mid = (lo + hi) / 2;
                    if (getTextWidth(remaining.substr(0, mid)) <= maxTextWidth) {
                        fitLen = mid;
                        lo = mid + 1;
                    } else {
                        hi = mid - 1;
                    }
                }
            }
            
            if (fitLen == 0) fitLen = 1;  // At least one character
            
            std::wstring chunk = remaining.substr(0, fitLen);
            DrawTextLeft(memDC, chunk.c_str(), msgX, msgY, COLOR_TEXT, g_fontNormal);
            msgY += lineHeight;
            lineCount++;
            
            if (fitLen < remaining.length()) {
                remaining = remaining.substr(fitLen);
            } else {
                break;
            }
        }
        return lineCount < maxLines;
    };
    
    // Split by \n and draw each line with word wrap
    size_t pos = 0;
    std::wstring remaining = oscMessage;
    while ((pos = remaining.find(L"\n")) != std::wstring::npos || !remaining.empty()) {
        std::wstring line = (pos != std::wstring::npos) ? remaining.substr(0, pos) : remaining;
        if (!drawLine(line)) break;
        
        if (pos != std::wstring::npos) {
            remaining = remaining.substr(pos + 1);
        } else {
            break;
        }
    }
    
    // Show "..." if there are more lines
    if (lineCount >= maxLines) {
        DrawTextLeft(memDC, L"...", msgX, msgY - lineHeight, COLOR_TEXT_DIM, g_fontNormal);
    }
    
    // Draw character count progress bar (battery style)
    {
        size_t charCount = oscMessage.length();
        double targetProgress = min(1.0, (double)charCount / 144.0);
        g_charProgressAnim.setTarget(targetProgress);
        g_charProgressAnim.speed = 0.15;
        g_charProgressAnim.update();  // 平滑过渡
        
        // Tooltip淡入淡出动画
        g_charTooltipAnim.setTarget(g_charProgressHover ? 1.0 : 0.0);
        g_charTooltipAnim.speed = g_charProgressHover ? 0.2 : 0.15;  // 淡入快，淡出慢
        g_charTooltipAnim.update();
        
        int labelX = rightColX + 18;
        int labelY = contentY + previewH - 30;
        
        // 绘制标签
        DrawTextLeft(memDC, L"\x5B57\x7B26\x6570", labelX, labelY, COLOR_TEXT_DIM, g_fontSmall);
        
        // 电池样式进度条 - 与标签垂直居中对齐
        int barH = 12;  // 电池高度
        int barX = labelX + 50;  // 标签后面
        int barY = labelY + 6;  // 垂直位置
        int barW = 60;  // 电池宽度
        int tipW = 3;   // 电池尖端宽度
        
        // 颜色逻辑：70%以下绿色，70%-95%黄色，95%以上红色
        COLORREF progressColor;
        double progress = g_charProgressAnim.value;
        if (progress > 0.95) {
            // 红色 (95%以上)
            progressColor = RGB(255, 80, 80);
        } else if (progress > 0.70) {
            // 黄色到橙色 (70%-95%)
            int t = (int)((progress - 0.70) * 255 / 0.25);
            progressColor = RGB(255, 200 - t/3, 50);  // 黄->橙
        } else {
            // 绿色 (70%以下)
            progressColor = RGB(80, 200, 100);
        }
        
        // 使用GDI+绘制电池样式进度条
        {
            Graphics graphics(memDC);
            graphics.SetSmoothingMode(SmoothingModeAntiAlias);
            
            // 电池外壳（圆角矩形）
            int cornerRadius = 2;
            int d = cornerRadius * 2;
            
            // 电池主体（外框）
            GraphicsPath shellPath;
            shellPath.AddArc(barX, barY, d, d, 180, 90);  // 左上角
            shellPath.AddLine(barX + cornerRadius, barY, barX + barW - cornerRadius, barY);  // 顶边
            shellPath.AddArc(barX + barW - d, barY, d, d, 270, 90);  // 右上角
            shellPath.AddLine(barX + barW, barY + cornerRadius, barX + barW, barY + barH - cornerRadius);  // 右边
            shellPath.AddArc(barX + barW - d, barY + barH - d, d, d, 0, 90);  // 右下角
            shellPath.AddLine(barX + barW - cornerRadius, barY + barH, barX + cornerRadius, barY + barH);  // 底边
            shellPath.AddArc(barX, barY + barH - d, d, d, 90, 90);  // 左下角
            shellPath.CloseFigure();
            
            // 绘制外框
            Pen borderPen(Color(255, GetRValue(COLOR_BORDER), GetGValue(COLOR_BORDER), GetBValue(COLOR_BORDER)), 1.5f);
            graphics.DrawPath(&borderPen, &shellPath);
            
            // 电池尖端（右侧小凸起）- 使用三条线
            int tipX = barX + barW;
            int tipH = barH / 2;
            int tipY = barY + (barH - tipH) / 2;
            graphics.DrawLine(&borderPen, tipX, tipY, tipX + tipW, tipY + tipH/2);
            graphics.DrawLine(&borderPen, tipX + tipW, tipY + tipH/2, tipX, tipY + tipH);
            
            // 内部填充
            double animProgress = g_charProgressAnim.value;
            if (animProgress > 0.01) {
                int innerPad = 2;
                int innerW = (int)((barW - innerPad * 2) * min(1.0, animProgress));
                if (innerW > 2) {
                    GraphicsPath fillPath;
                    int fx = barX + innerPad;
                    int fy = barY + innerPad;
                    int fh = barH - innerPad * 2;
                    int fr = 1;
                    int fd = fr * 2;
                    
                    fillPath.AddArc(fx, fy, fd, fd, 180, 90);
                    fillPath.AddLine(fx + fr, fy, fx + innerW - fr, fy);
                    fillPath.AddArc(fx + innerW - fd, fy, fd, fd, 270, 90);
                    fillPath.AddLine(fx + innerW, fy + fr, fx + innerW, fy + fh - fr);
                    fillPath.AddArc(fx + innerW - fd, fy + fh - fd, fd, fd, 0, 90);
                    fillPath.AddLine(fx + innerW - fr, fy + fh, fx + fr, fy + fh);
                    fillPath.AddArc(fx, fy + fh - fd, fd, fd, 90, 90);
                    fillPath.CloseFigure();
                    
                    SolidBrush fillBrush(Color(255, GetRValue(progressColor), GetGValue(progressColor), GetBValue(progressColor)));
                    graphics.FillPath(&fillBrush, &fillPath);
                }
            }
        }
        
        // Tooltip淡入淡出显示（无背景）
        double tooltipAlpha = g_charTooltipAnim.value;
        if (tooltipAlpha > 0.05) {
            int tipX = barX + barW + tipW + 8;
            int tipY = barY - 1;
            
            wchar_t tipBuf[64];
            int remaining = 144 - (int)charCount;
            if (remaining >= 0) {
                swprintf_s(tipBuf, L"%zd/144 (\x5269\x4F59%d)", charCount, remaining);
            } else {
                swprintf_s(tipBuf, L"%zd/144 (\x8D85\x51FA%d)", charCount, -remaining);
            }
            
            // 计算淡入淡出颜色
            COLORREF textColor = charCount > 144 ? COLOR_ERROR : COLOR_TEXT;
            int alpha = (int)(tooltipAlpha * 255);
            int tr = (GetRValue(COLOR_BG) * (255 - alpha) + GetRValue(textColor) * alpha) / 255;
            int tg = (GetGValue(COLOR_BG) * (255 - alpha) + GetGValue(textColor) * alpha) / 255;
            int tb = (GetBValue(COLOR_BG) * (255 - alpha) + GetBValue(textColor) * alpha) / 255;
            
            HFONT oldFont = (HFONT)SelectObject(memDC, g_fontSmall);
            SetTextColor(memDC, RGB(tr, tg, tb));
            SetBkMode(memDC, TRANSPARENT);
            TextOutW(memDC, tipX, tipY, tipBuf, (int)wcslen(tipBuf));
            SelectObject(memDC, oldFont);
        }
        
        // 超出限制时显示警告图标
        if (charCount > 144) {
            DrawTextLeft(memDC, L"\x26A0", barX + barW + tipW + 70, barY, COLOR_WARNING, g_fontSmall);
        }
    }
    
    // 恢复剪辑区域（使用nullptr清除剪辑区域）
    SelectClipRgn(memDC, nullptr);
    DeleteObject(rightClip);
    DeleteObject(oldRightClip);
    
    // Update tray
    if (!previewInfo.title.empty()) {
        std::wstring tip = TruncateStr(previewInfo.title + L" - " + previewInfo.artist, 60);
        UpdateTrayTip(tip.c_str());
    }
    
    // === Version display at window bottom left (outside card) ===
    wchar_t verBuf[32];
    swprintf_s(verBuf, L"v" APP_VERSION);
    DrawTextLeft(memDC, verBuf, CARD_PADDING + 18, h - 25, COLOR_TEXT_DIM, g_fontSmall);
    
    // === Author display at window bottom right ===
    DrawTextRight(memDC, L"by \x6C90\x98CE", w - CARD_PADDING - 18, h - 25, COLOR_TEXT_DIM, g_fontSmall);
    
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    
    EndPaint(hwnd, &ps);
}

void ApplySettings() {
    // IP and Port are now edited via config file
    // Just recreate OSC sender with current values
    if (g_osc) delete g_osc;
    g_osc = new moekoe::OSCSender(WstringToUtf8(g_oscIp), g_oscPort);
    SaveConfig(g_configPath);
}

void Disconnect() {
    if (g_moeKoeClient) { delete g_moeKoeClient; g_moeKoeClient = nullptr; }
    if (g_neteaseClient) { delete g_neteaseClient; g_neteaseClient = nullptr; }
    g_isConnected = false;
    g_moeKoeConnected = false;
    g_neteaseConnected = false;
    g_needsRedraw = true;
    g_activePlatform = -1;
    g_pendingTitle.clear();
    g_pendingArtist.clear();
    g_pendingLyrics.clear();
    g_pendingDuration = 0;
    g_pendingCurrentTime = 0;
    g_pendingIsPlaying = false;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void Connect() {
    // 防止重复连接
    if (g_isConnecting) {
        MainDebugLog("[Main] Connect() already in progress, skipping");
        return;
    }
    
    MainDebugLog("[Main] Connect() called");
    g_isConnecting = true;
    Disconnect();  // Clean up any existing connections
    
    // Show connecting status immediately
    g_isConnected = false;
    g_moeKoeConnected = false;
    g_neteaseConnected = false;
    g_activePlatform = -1;  // No active platform yet
    g_pendingTitle = L"\x6B63\x5728\x8FDE\x63A5...";
    g_pendingArtist = L"";
    g_needsRedraw = true;
    g_pendingLyrics.clear();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    
    // Run connection in background thread to avoid blocking UI
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        MainDebugLog("[Connect] Starting connection attempt...");
        
        // Try to connect MoeKoe
        MainDebugLog("[Connect] Trying MoeKoe...");
        moekoe::MoeKoeWS* moeKoeClient = new moekoe::MoeKoeWS("127.0.0.1", g_moekoePort);
        moeKoeClient->setCallback([](const moekoe::SongInfo& info) {
            QueueUpdate(info, 0);
        });
        if (moeKoeClient->connect()) {
            g_moeKoeClient = moeKoeClient;
            g_moeKoeConnected = true;
            g_needsRedraw = true;
            MainDebugLog("[Connect] MoeKoe connected!");
        } else {
            MainDebugLog("[Connect] MoeKoe connection failed");
            delete moeKoeClient;
        }
        
        // Try to connect Netease
        MainDebugLog("[Connect] Trying Netease (port 9222)...");
        moekoe::NeteaseWS* neteaseClient = new moekoe::NeteaseWS(9222);
        neteaseClient->setCallback([](const moekoe::SongInfo& info) {
            QueueUpdate(info, 1);
        });
        if (neteaseClient->connect()) {
            g_neteaseClient = neteaseClient;
            g_neteaseConnected = true;
            g_needsRedraw = true;
            MainDebugLog("[Connect] Netease connected!");
        } else {
            MainDebugLog("[Connect] Netease connection failed");
            delete neteaseClient;
        }
        
        // Update connection status
        g_isConnected = g_moeKoeConnected || g_neteaseConnected || g_smtcConnected;
        g_isConnecting = false;  // 连接完成
        MainDebugLog(g_isConnected ? "[Connect] Overall: CONNECTED" : "[Connect] Overall: FAILED");
        if (!g_isConnected) {
            g_pendingTitle = L"\x8FDE\x63A5\x5931\x8D25";
            g_pendingArtist = L"\x8BF7\x542F\x52A8 MoeKoeMusic \x6216\x7F51\x6613\x4E91\x97F3\x4E50";
            g_needsRedraw = true;
        } else {
            // Set active platform to the one that's connected
            if (g_moeKoeConnected) g_activePlatform = 0;
            else if (g_neteaseConnected) g_activePlatform = 1;
            g_pendingTitle.clear();
            g_pendingArtist.clear();
        }
        
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        return 0;
    }, nullptr, 0, nullptr);
}

bool IsInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

bool IsAnimationActive() {
    // Check if any animation is still updating
    // 按钮悬停动画
    if (g_btnConnectAnim.isActive()) return true;
    if (g_btnApplyAnim.isActive()) return true;
    if (g_btnCloseAnim.isActive()) return true;
    if (g_btnMinAnim.isActive()) return true;
    if (g_btnUpdateAnim.isActive()) return true;
    if (g_btnLaunchAnim.isActive()) return true;
    if (g_btnExportLogAnim.isActive()) return true;
    if (g_btnThemeAnim.isActive()) return true;
    if (g_btnAutoDetectAnim.isActive()) return true;
    if (g_btnShowOrderAnim.isActive()) return true;
    
    // 窗口启动动画
    if (!g_startupAnimComplete) return true;
    if (g_windowFadeAnim.isActive()) return true;
    if (g_windowScaleAnim.isActive()) return true;
    
    // 标签页切换动画
    if (g_tabSlideAnim.isActive()) return true;

    // 显示模式切换动画
    if (g_displayModeSlideAnim.isActive()) return true;

    // 系统信息展开动画
    if (g_systemInfoExpandAnim.isActive()) return true;

    // 下拉菜单动画（只在菜单可见时才认为活跃）
    if (g_menuExpandAnim.isActive() && (g_platformMenuOpen || g_menuExpandAnim.value > 0.01)) return true;
    
    // 歌词滚动动画
    if (g_lyricScrollAnim.isActive()) return true;
    
    // 主题切换过渡
    if (g_themeTransition.isActive()) return true;
    
    return false;
}

void UpdateAnimations() {
    // 按钮悬停动画
    g_btnConnectAnim.setTarget(g_btnConnectHover ? 1.0 : 0.0);
    g_btnApplyAnim.setTarget(g_btnApplyHover ? 1.0 : 0.0);
    g_btnCloseAnim.setTarget(g_btnCloseHover ? 1.0 : 0.0);
    g_btnMinAnim.setTarget(g_btnMinHover ? 1.0 : 0.0);
    g_btnUpdateAnim.setTarget(g_btnUpdateHover ? 1.0 : 0.0);
    g_btnLaunchAnim.setTarget(g_btnLaunchHover ? 1.0 : 0.0);
    g_btnExportLogAnim.setTarget(g_btnExportLogHover ? 1.0 : 0.0);
    g_btnThemeAnim.setTarget(g_btnThemeHover ? 1.0 : 0.0);
    g_btnAutoDetectAnim.setTarget(g_autoDetectHover ? 1.0 : 0.0);
    g_btnShowOrderAnim.setTarget(g_showOrderHover ? 1.0 : 0.0);
    g_btnConnectAnim.update();
    g_btnApplyAnim.update();
    g_btnCloseAnim.update();
    g_btnMinAnim.update();
    g_btnUpdateAnim.update();
    g_btnLaunchAnim.update();
    g_btnExportLogAnim.update();
    g_btnThemeAnim.update();
    g_btnAutoDetectAnim.update();
    g_btnShowOrderAnim.update();
    
    // 窗口启动动画（缩放）- 简化版本，不再使用淡入
    if (!g_startupAnimComplete) {
        g_windowScaleAnim.update();
        
        // 检查动画是否完成
        if (!g_windowScaleAnim.isActive()) {
            g_startupAnimComplete = true;
        }
    }
    
    // 标签页切换动画
    g_tabSlideAnim.update();

    // 显示模式切换动画
    g_displayModeSlideAnim.update();

    // 系统信息展开动画
    g_systemInfoExpandAnim.update();

    // 下拉菜单展开动画
    g_menuExpandAnim.update();
    
    // 下拉箭头旋转动画
    g_arrowRotationAnim.update();
    
    // 下拉菜单逐条淡入淡出动画（延迟递增效果）
    double menuExpandFactor = g_menuExpandAnim.value;
    for (int i = 0; i < g_platformCount && i < 10; i++) {
        if (g_platformMenuOpen) {
            // 展开：逐条淡入（从上到下）
            double delay = i * 0.08;  // 每项延迟0.08
            double itemProgress = max(0.0, menuExpandFactor - delay) / max(0.01, 1.0 - delay);
            itemProgress = min(1.0, itemProgress);
            // 使用缓动函数
            itemProgress = itemProgress * itemProgress * (3.0 - 2.0 * itemProgress);  // smoothstep
            g_menuItemAnims[i].value = itemProgress;
        } else {
            // 收起：逐条淡出（从下到上）
            int reverseIdx = g_platformCount - 1 - i;  // 反向索引
            double delay = reverseIdx * 0.06;  // 从下往上，每项延迟0.06
            double collapseProgress = 1.0 - menuExpandFactor;  // 收起进度
            double itemProgress = max(0.0, collapseProgress - delay) / max(0.01, 1.0 - delay);
            itemProgress = min(1.0, itemProgress);
            itemProgress = itemProgress * itemProgress * (3.0 - 2.0 * itemProgress);  // smoothstep
            g_menuItemAnims[i].value = 1.0 - itemProgress;  // 反转：进度越大越透明
        }
    }
    
    // 更新按钮旋转动画（检查更新时旋转）
    if (g_checkingUpdate) {
        DWORD now = GetTickCount();
        if (g_lastRotationTime == 0) g_lastRotationTime = now;
        double elapsed = (now - g_lastRotationTime) / 1000.0;
        g_updateRotation += elapsed * 360.0;  // 每秒转360度
        if (g_updateRotation >= 360.0) g_updateRotation -= 360.0;
        g_lastRotationTime = now;
    } else {
        g_updateRotation = 0.0;
        g_lastRotationTime = 0;
    }
    
    // 连接状态脉冲动画（呼吸效果）- 只在已连接时启用
    static DWORD pulseTimer = 0;
    DWORD now = GetTickCount();
    if (g_isConnected && now - pulseTimer > 50) {  // 50ms 更新一次
        pulseTimer = now;
        // 脉冲效果：在0到1之间循环
        double pulseTarget = (sin(GetTickCount() / 500.0) + 1.0) / 2.0;
        g_connectPulseAnim.setTarget(pulseTarget);
        g_connectPulseAnim.update();
    } else if (!g_isConnected) {
        // 未连接时重置脉冲动画
        g_connectPulseAnim.value = 0.0;
        g_connectPulseAnim.target = 0.0;
    }
    
    // 歌词滚动动画
    g_lyricScrollAnim.update();
    
    // 主题切换过渡动画
    g_themeTransition.update();
}

// 前向声明
void TriggerParticleBurst(int centerX, int centerY);
void TriggerParticleBurstAtProgressEnd();
void CreateOverlayWindow();
void DestroyOverlayWindow();

// Low-level keyboard hook for global hotkey (works in VRChat fullscreen)
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        
        // Only process key down events
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            // 检查是否是我们关注的热键
            if (kb->vkCode == g_oscPauseHotkey && !g_editingHotkey) {
                // 使用 GetAsyncKeyState 代替 GetKeyState（在低级钩子中更可靠）
                bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
                bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                bool win = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
                
                UINT mods = 0;
                if (ctrl) mods |= MOD_CONTROL;
                if (alt) mods |= MOD_ALT;
                if (shift) mods |= MOD_SHIFT;
                if (win) mods |= MOD_WIN;
                
                // Check if modifiers match
                if (mods == g_oscPauseHotkeyMods) {
                    // Toggle pause state
                    DWORD now = GetTickCount();
                    
                    // 判断是否真的在暂停中（时间还没到）
                    bool isReallyPaused = g_oscPaused && g_oscPauseEndTime > now;
                    
                    if (isReallyPaused || g_overlayHwnd) {
                        // Already paused or window exists - cancel pause with particle burst
                        MainDebugLog("[Hotkey] Canceling OSC pause (low-level hook)");
                        
                        // 发送恢复消息提示
                        if (g_osc && g_oscEnabled) {
                            SendSystemOSCMessage(L"OSC \x5DF2\x6062\x590D\x53D1\x9001");
                        }
                        
                        // 触发粒子爆炸（在进度条末端）
                        TriggerParticleBurstAtProgressEnd();
                        
                        g_oscPaused = false;
                        g_oscPauseEndTime = 0;
                        
                        // 如果窗口正在关闭动画，立即销毁；否则开始关闭动画
                        if (g_overlayHwnd) {
                            if (g_overlayClosing) {
                                // 正在关闭，立即销毁
                                DestroyWindow(g_overlayHwnd);
                                g_overlayHwnd = nullptr;
                                g_overlayActive = false;
                                g_overlayClosing = false;
                                g_overlayExpandAnim = 0.0f;
                                g_particles.clear();
                                g_sandParticles.clear();
                            } else {
                                // 开始关闭动画
                                g_overlayClosing = true;
                            }
                        }
                    } else {
                        // Start pause
                        g_oscPaused = true;
                        g_oscPauseEndTime = now + OSC_PAUSE_DURATION * 1000;
                        g_overlayClosing = false;  // 确保不是关闭状态
                        MainDebugLog("[Hotkey] OSC paused for 30 seconds (low-level hook)");
                        
                        // 发送暂停消息提示
                        if (g_osc && g_oscEnabled) {
                            SendSystemOSCMessage(L"\x6B63\x5728\x6682\x505C OSC \x53D1\x9001...\n\x8BF7\x7B49\x5F85 30 \x79D2");
                        }
                        
                        // Create overlay window
                        CreateOverlayWindow();
                    }
                    
                    // Redraw main window
                    if (g_hwnd) {
                        InvalidateRect(g_hwnd, nullptr, FALSE);
                    }
                    return 1; // Consume the key
                }
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

// 更新粒子系统
void UpdateParticles() {
    // 更新现有粒子
    for (auto it = g_particles.begin(); it != g_particles.end();) {
        it->x += it->vx;
        it->y += it->vy;
        it->vy += 0.3f; // 重力
        it->life -= 0.02f;
        
        if (it->life <= 0) {
            it = g_particles.erase(it);
        } else {
            ++it;
        }
    }
    
    // 更新沙漏粒子
    for (auto it = g_sandParticles.begin(); it != g_sandParticles.end();) {
        it->y += it->vy;
        it->vy += 0.15f; // 重力
        
        if (it->y > 60) { // 超出进度条范围（窗口高度80，进度条在底部）
            it = g_sandParticles.erase(it);
        } else {
            ++it;
        }
    }
}

// 触发粒子爆发效果 - 更精致的粒子
void TriggerParticleBurst(int centerX, int centerY) {
    g_particleBurst = true;
    
    // 主爆发粒子（大粒子，向外扩散）
    for (int i = 0; i < 40; i++) {
        Particle p;
        p.x = (float)centerX;
        p.y = (float)centerY;
        
        // 放射状速度
        float angle = (rand() % 360) * 3.14159f / 180.0f;
        float speed = 2.0f + (rand() % 120) / 10.0f;  // 2 to 14
        p.vx = cosf(angle) * speed;
        p.vy = sinf(angle) * speed - 3.0f;  // 向上偏移
        p.life = 1.2f + (rand() % 50) / 100.0f;  // 1.2 to 1.7
        p.maxLife = p.life;
        p.size = rand() % 6 + 4;  // 4 to 9
        
        // 蓝色系渐变颜色
        int colorType = rand() % 5;
        switch (colorType) {
            case 0: p.color = RGB(100, 180, 255); break;   // 亮蓝
            case 1: p.color = RGB(150, 200, 255); break;   // 天蓝
            case 2: p.color = RGB(80, 140, 255); break;    // 深蓝
            case 3: p.color = RGB(200, 230, 255); break;   // 浅蓝
            case 4: p.color = RGB(180, 100, 255); break;   // 紫蓝
        }
        g_particles.push_back(p);
    }
    
    // 小粒子火花（快速消失）
    for (int i = 0; i < 30; i++) {
        Particle p;
        p.x = (float)centerX + (rand() % 20 - 10);
        p.y = (float)centerY + (rand() % 20 - 10);
        p.vx = ((rand() % 200) - 100) / 10.0f;  // -10 to 10
        p.vy = ((rand() % 200) - 150) / 10.0f;  // -15 to 5 (向上)
        p.life = 0.4f + (rand() % 30) / 100.0f;  // 0.4 to 0.7
        p.maxLife = p.life;
        p.size = rand() % 3 + 1;  // 1 to 3
        
        // 白色/亮色火花
        int brightness = 200 + rand() % 55;
        p.color = RGB(brightness, brightness, 255);
        g_particles.push_back(p);
    }
}

// 在进度条末端触发粒子爆炸
void TriggerParticleBurstAtProgressEnd() {
    if (!g_overlayHwnd) return;
    
    RECT rc;
    GetClientRect(g_overlayHwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    
    // 计算进度条参数（与绘制代码一致）
    int barPadding = 20;
    int barH = 10;
    int barY = h - barH - 22;
    int barX = barPadding + 12;
    int barW = w - barPadding * 2 - 12;
    
    // 计算当前进度
    float progress = 0;
    if (g_oscPauseEndTime > 0) {
        DWORD now = GetTickCount();
        if (g_oscPauseEndTime > now) {
            progress = (float)(g_oscPauseEndTime - now) / (OSC_PAUSE_DURATION * 1000.0f);
        }
    }
    
    // 进度条末端坐标
    int endX = barX + (int)(barW * progress);
    int endY = barY + barH / 2;  // 进度条中心
    
    TriggerParticleBurst(endX, endY);
}

// 获取进度条颜色（绿色→黄色→红色渐变）
COLORREF GetProgressColor(float progress) {
    // progress: 1.0 = 开始(绿色), 0.0 = 结束(红色)
    if (progress > 0.5f) {
        // 绿色到黄色
        float t = (progress - 0.5f) * 2.0f; // 0-1
        int r = (int)(255 * (1.0f - t));
        int g = 255;
        int b = (int)(100 * t);
        return RGB(r, g, b);
    } else {
        // 黄色到红色
        float t = progress * 2.0f; // 0-1
        int r = 255;
        int g = (int)(255 * t);
        int b = 0;
        return RGB(r, g, b);
    }
}

// 覆盖层窗口过程
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            int w, h;
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                w = rc.right - rc.left;
                h = rc.bottom - rc.top;
            }
            
            // 创建双缓冲
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            
            // 先填充透明背景（窗口区域外的部分）
            {
                RECT bgRect = {0, 0, w, h};
                HBRUSH bgBrush = CreateSolidBrush(RGB(1, 1, 1));  // 透明色
                FillRect(memDC, &bgRect, bgBrush);
                DeleteObject(bgBrush);
            }
            
            SetBkMode(memDC, TRANSPARENT);
            
            // 计算展开动画裁剪区域（从中间向两边展开/收缩）
            int clipW = (int)(w * g_overlayExpandAnim);
            int clipX = (w - clipW) / 2;
            
            // 绘制背景渐变和内容（受收缩动画控制）
            if (clipW > 0) {
                // 创建裁剪区域
                HRGN clipRgn = CreateRectRgn(clipX, 0, clipX + clipW, h);
                SelectClipRgn(memDC, clipRgn);
                
                // 进度条参数
                int barPadding = 20;
                int barH = 10;
                int barY = h - barH - 22;
                int barX = barPadding;
                int barW = w - barPadding * 2;
                
                // 计算进度
                DWORD now = GetTickCount();
                float progress = 0;
                if (g_oscPaused && g_oscPauseEndTime > now) {
                    progress = (float)(g_oscPauseEndTime - now) / (OSC_PAUSE_DURATION * 1000.0f);
                }
                
                // 更新粒子
                UpdateParticles();
                
                // 只有在暂停状态有效时才绘制进度条
                if (g_oscPaused && g_oscPauseEndTime > now) {
                    // 绘制进度条背景（圆角）
                    DrawRoundRect(memDC, barX, barY, barW, barH, barH / 2, RGB(60, 60, 65));
                    
                    if (progress > 0) {
                        // 绘制进度条（从左到右减少，绿→黄→红渐变）
                        int progressW = (int)(barW * progress);
                        if (progressW > barH) {  // 确保最小宽度以显示圆角
                            COLORREF progressColor = GetProgressColor(progress);
                            DrawRoundRect(memDC, barX, barY, progressW, barH, barH / 2, progressColor);
                            
                            // 沙漏粒子效果 - 从进度条末端向上飘落
                            if ((rand() % 4) == 0 && g_overlayExpandAnim >= 1.0f) {
                                SandParticle sp;
                                sp.x = (float)(barX + progressW);
                                sp.y = -5.0f;
                                sp.vy = 0.5f + (rand() % 10) / 10.0f;
                                sp.size = 2 + rand() % 2;
                                sp.color = progressColor;
                                g_sandParticles.push_back(sp);
                            }
                        }
                    }
                }
                
                // 绘制沙漏粒子（向上下飘动，不是向下）
                for (const auto& sp : g_sandParticles) {
                    HBRUSH sandBrush = CreateSolidBrush(sp.color);
                    int py = (int)(barY - sp.y);  // 向上飘
                    RECT sandRect = {
                        (int)(sp.x - sp.size/2),
                        py - sp.size/2,
                        (int)(sp.x + sp.size/2),
                        py + sp.size/2
                    };
                    FillRect(memDC, &sandRect, sandBrush);
                    DeleteObject(sandBrush);
                }
                
                // 绘制倒计时文本（右侧）
                if (g_oscPaused && g_oscPauseEndTime > now) {
                    int remaining = (int)((g_oscPauseEndTime - now) / 1000);
                    wchar_t timeText[32];
                    swprintf_s(timeText, L"%ds", remaining);
                    
                    SetTextColor(memDC, RGB(255, 255, 255));
                    HFONT font = CreateFontW(22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
                    HFONT oldFont = (HFONT)SelectObject(memDC, font);
                    
                    SIZE textSize;
                    GetTextExtentPoint32W(memDC, timeText, (int)wcslen(timeText), &textSize);
                    int textX = w - barPadding - textSize.cx;
                    int textY = 12;
                    TextOutW(memDC, textX, textY, timeText, (int)wcslen(timeText));
                    
                    SelectObject(memDC, oldFont);
                    DeleteObject(font);
                }
                
                // 绘制爆发粒子
                for (const auto& p : g_particles) {
                    HBRUSH particleBrush = CreateSolidBrush(p.color);
                    RECT pRect = {
                        (int)(p.x - p.size/2),
                        (int)(p.y - p.size/2),
                        (int)(p.x + p.size/2),
                        (int)(p.y + p.size/2)
                    };
                    FillRect(memDC, &pRect, particleBrush);
                    DeleteObject(particleBrush);
                }
                
                SelectClipRgn(memDC, nullptr);
                DeleteObject(clipRgn);
            }
            
            // 复制到屏幕
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_TIMER: {
            if (wParam == 2) {
                DWORD now = GetTickCount();
                
                // 更新粒子系统
                UpdateParticles();
                
                // 更新展开/收缩动画
                if (!g_overlayClosing && g_overlayExpandAnim < 1.0f) {
                    // 展开：从中间向两边展开
                    g_overlayExpandAnim += 0.08f;  // 动画速度
                    if (g_overlayExpandAnim > 1.0f) g_overlayExpandAnim = 1.0f;
                } else if (g_overlayClosing && g_overlayExpandAnim > 0.0f) {
                    // 收缩：从两边向中间收缩
                    g_overlayExpandAnim -= 0.08f;
                    if (g_overlayExpandAnim < 0.0f) g_overlayExpandAnim = 0.0f;
                }
                
                // 检查暂停是否自然结束
                if (g_oscPaused && g_oscPauseEndTime > 0 && now >= g_oscPauseEndTime) {
                    g_oscPaused = false;
                    g_oscPauseEndTime = 0;
                    g_overlayClosing = true;  // 开始收缩动画
                    MainDebugLog("[Overlay] OSC pause ended naturally, closing animation");
                }
                
                // 检查是否应该销毁覆盖层
                if (g_overlayClosing && g_overlayExpandAnim <= 0.0f && g_particles.empty() && g_sandParticles.empty()) {
                    KillTimer(hwnd, 2);
                    DestroyWindow(hwnd);
                    return 0;
                }
                
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_DESTROY:
            KillTimer(hwnd, 2);
            g_overlayHwnd = nullptr;
            g_overlayActive = false;
            g_overlayExpandAnim = 0.0f;
            g_overlayClosing = false;
            g_particles.clear();
            g_sandParticles.clear();
            g_particleBurst = false;
            MainDebugLog("[Overlay] Destroyed overlay window");
            return 0;
            
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// 创建覆盖层窗口
void CreateOverlayWindow() {
    // 如果窗口已存在，先销毁（支持快速重复启动）
    if (g_overlayHwnd) {
        DestroyWindow(g_overlayHwnd);
        g_overlayHwnd = nullptr;
        g_overlayActive = false;
    }
    
    HINSTANCE hInst = GetModuleHandle(nullptr);
    
    // 获取屏幕尺寸
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    // 进度条窗口尺寸（屏幕底部居中）
    int winW = 300;
    int winH = 60;
    int winX = (screenW - winW) / 2;  // 水平居中
    int winY = screenH - winH - 20;   // 距离底部20像素
    
    // 注册窗口类（只注册一次）
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = OverlayWndProc;
        wc.hInstance = hInst;
        wc.hCursor = nullptr;
        wc.hbrBackground = CreateSolidBrush(RGB(1, 1, 1));  // 特殊颜色作为透明背景
        wc.lpszClassName = L"VRCLyricsOverlay_Class";
        RegisterClassExW(&wc);
        registered = true;
    }
    
    // 创建顶层窗口，点击穿透，不抢焦点，不显示在任务栏
    g_overlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"VRCLyricsOverlay_Class", L"",
        WS_POPUP,
        winX, winY, winW, winH,
        nullptr, nullptr, hInst, nullptr);
    
    // 使用颜色键透明：RGB(1,1,1) 将变为透明，这样收缩动画时未绘制区域会自动透明
    SetLayeredWindowAttributes(g_overlayHwnd, RGB(1, 1, 1), 0, LWA_COLORKEY);
    
    // 设置覆盖层自己的定时器（16ms，约60fps）
    SetTimer(g_overlayHwnd, 2, 16, nullptr);
    
    // 初始化展开动画：从中间向两边展开
    g_overlayExpandAnim = 0.0f;
    g_overlayClosing = false;
    
    ShowWindow(g_overlayHwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_overlayHwnd);
    g_overlayActive = true;
    
    MainDebugLog("[Overlay] Created overlay window at bottom center of screen");
}

// 销毁覆盖层窗口
void DestroyOverlayWindow() {
    if (g_overlayHwnd) {
        // 定时器会在WM_DESTROY中清理
        DestroyWindow(g_overlayHwnd);
        g_overlayHwnd = nullptr;
        g_overlayActive = false;
        g_particles.clear();
        g_sandParticles.clear();
        g_particleBurst = false;
        MainDebugLog("[Overlay] Destroyed overlay window via DestroyOverlayWindow()");
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            MainDebugLog("[Main] WM_CREATE - Application starting");
            
            // 从Git配置自动检测仓库地址
            g_autoDetectedRepo = GetRepoFromGitConfig();
            std::string logMsg = "[Main] Auto-detected repo: " + g_autoDetectedRepo;
            MainDebugLog(logMsg.c_str());
            
            // Fonts - larger for high DPI
            g_fontTitle = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontSubtitle = CreateFontW(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontNormal = CreateFontW(26, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontSmall = CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontLyric = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontLabel = CreateFontW(24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            
            g_brushBg = CreateSolidBrush(COLOR_BG);
            g_brushCard = CreateSolidBrush(COLOR_CARD);
            g_brushEditBg = CreateSolidBrush(COLOR_EDIT_BG);
            
            // 设置 DWM 圆角和毛玻璃效果
            UpdateWindowSystemTheme(hwnd);
            
            // 设置窗口圆角区域
            HRGN hrgn = CreateRoundedWindowRegion(hwnd, 12);
            if (hrgn) {
                SetWindowRgn(hwnd, hrgn, TRUE);  // TRUE = 销毁之前的区域
            }
            
            EnableBlurBehind(hwnd);
            
            // Create edit controls - positions must match OnPaint drawing
            // Note: IP and Port editing is done via config file
            // Click on IP/Port in settings tab shows a hint
            
            CreateTrayIcon(hwnd);
            
            // Initialize SMTC client for QQ Music support
            MainDebugLog("[Main] Initializing SMTC client for QQ Music");
            g_smtcClient = new smtc::SMTCClient();
            if (g_smtcClient) {
                g_smtcClient->setAppFilter(L"QQMusic");  // Only detect QQ Music
                if (g_smtcClient->start()) {
                    g_smtcClient->setCallback([hwnd](const smtc::MediaInfo& info) {
                        // Post message to main thread for UI update
                        PostMessageW(hwnd, WM_USER + 101, 0, 0);
                    });
                    MainDebugLog("[Main] SMTC client started successfully (QQ Music only)");
                } else {
                    MainDebugLog("[Main] SMTC client failed to start");
                }
            }
            
            // 注册全局热键：用户自定义热键暂停OSC发送30秒
            RegisterHotKey(hwnd, HOTKEY_OSC_PAUSE, g_oscPauseHotkeyMods, g_oscPauseHotkey);
            
            // 设置低级键盘钩子（用于VRChat全屏模式）
            g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
            if (g_keyboardHook) {
                MainDebugLog("[Main] Low-level keyboard hook installed for VRChat fullscreen mode");
            } else {
                MainDebugLog("[Main] Warning: Failed to install low-level keyboard hook");
            }
            
            // 启动OSC接收器（用于接收VRChat手势触发的暂停命令）
            g_oscReceiver = new moekoe::OSCReceiver(9001);
            g_oscReceiver->setPauseCallback([]() {
                // 在主线程中切换暂停状态
                PostMessage(g_hwnd, WM_USER + 102, 0, 0);
            });
            if (g_oscReceiver->start()) {
                MainDebugLog("[Main] OSC Receiver started on port 9001");
            } else {
                MainDebugLog("[Main] Warning: Failed to start OSC Receiver");
            }
            
            return 0;
        }
        
        case WM_HOTKEY: {
            if (wParam == HOTKEY_OSC_PAUSE) {
                // 切换暂停状态
                DWORD now = GetTickCount();
                
                // 判断是否真的在暂停中（时间还没到）
                bool isReallyPaused = g_oscPaused && g_oscPauseEndTime > now;
                
                if (isReallyPaused || g_overlayHwnd) {
                    // 已经在暂停中或窗口存在 - 取消暂停，触发粒子爆发
                    MainDebugLog("[Hotkey] Canceling OSC pause (WM_HOTKEY)");
                    
                    // 发送恢复消息提示
                    if (g_osc && g_oscEnabled) {
                        SendSystemOSCMessage(L"OSC \x5DF2\x6062\x590D\x53D1\x9001");
                    }
                    
                    // 触发粒子爆炸（在进度条末端）
                    TriggerParticleBurstAtProgressEnd();
                    
                    g_oscPaused = false;
                    g_oscPauseEndTime = 0;
                    
                    // 如果窗口正在关闭动画，立即销毁；否则开始关闭动画
                    if (g_overlayHwnd) {
                        if (g_overlayClosing) {
                            // 正在关闭，立即销毁
                            DestroyWindow(g_overlayHwnd);
                            g_overlayHwnd = nullptr;
                            g_overlayActive = false;
                            g_overlayClosing = false;  // 重置关闭状态
                            g_overlayExpandAnim = 0.0f;
                            g_particles.clear();
                            g_sandParticles.clear();
                        } else {
                            // 开始关闭动画
                            g_overlayClosing = true;
                        }
                    }
                } else {
                    // 开始新的暂停
                    g_oscPaused = true;
                    g_oscPauseEndTime = now + OSC_PAUSE_DURATION * 1000;
                    g_overlayClosing = false;  // 确保不是关闭状态
                    MainDebugLog("[OSC] Paused for 30 seconds");
                    
                    // 发送暂停消息提示
                    if (g_osc && g_oscEnabled) {
                        SendSystemOSCMessage(L"\x6B63\x5728\x6682\x505C OSC \x53D1\x9001...\n\x8BF7\x7B49\x5F85 30 \x79D2");
                    }
                    
                    // 创建覆盖层窗口
                    CreateOverlayWindow();
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, COLOR_TEXT);
            SetBkColor(hdcEdit, COLOR_EDIT_BG);
            return (LRESULT)g_brushEditBg;
        }
        
        case WM_PAINT: OnPaint(hwnd); return 0;
        
        case WM_ERASEBKGND: return 1;  // 背景由OnPaint完全处理，阻止系统默认擦除
        
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            if (g_dragging) {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(hwnd, nullptr, pt.x - g_dragStart.x, pt.y - g_dragStart.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                return 0;
            }
            
            // Layout positions
            int leftColW = 260;
            int contentY = TITLEBAR_H + 20;
            int tabH = 32;
            int tabY = contentY + 10;  // Tabs inside card
            int rowY = contentY + 55;
            int checkboxX = CARD_PADDING + 18;
            
            bool oldConnect = g_btnConnectHover, oldApply = g_btnApplyHover;
            bool oldClose = g_btnCloseHover, oldMin = g_btnMinHover, oldUpdate = g_btnUpdateHover, oldLaunch = g_btnLaunchHover;
            bool oldExportLog = g_btnExportLogHover, oldAdmin = g_btnAdminHover, oldTheme = g_btnThemeHover, oldHotkeyBox = g_hotkeyBoxHover;
            bool oldTabHover[3] = {g_tabHover[0], g_tabHover[1], g_tabHover[2]};

            // Tab hover detection
            int tabW = 70;
            for (int t = 0; t < 3; t++) {
                int tabX = CARD_PADDING + 18 + t * (tabW + 8);
                g_tabHover[t] = IsInRect(x, y, tabX, tabY, tabW, tabH);
            }
            
            // Button hover detection (Main Tab only)
            if (g_currentTab == 0) {
                int platformRowY = rowY;
                int platformBoxX = CARD_PADDING + 18;
                int platformBoxY = platformRowY + 22;
                int platformBoxW = leftColW - 36;
                int platformBoxH = 44;  // 必须与绘制代码一致
                int menuItemH = 36;
                int menuH = g_platformCount * menuItemH + 8;
                // 使用动画值计算菜单空间，与绘制代码一致
                double menuExpandFactor = g_menuExpandAnim.value;
                bool isMenuVisible = g_platformMenuOpen || (g_menuExpandAnim.isActive() && menuExpandFactor > 0.01);
                int menuExtraSpace = isMenuVisible ? (int)((g_platformCount * menuItemH + 12) * menuExpandFactor) : 0;
                int btnY = platformRowY + 80 + menuExtraSpace;  // Match drawing position
                int launchBtnY = btnY + 48;
                
                // Platform box hover
                bool oldPlatformBoxHover = g_platformBoxHover;
                g_platformBoxHover = IsInRect(x, y, platformBoxX, platformBoxY, platformBoxW, platformBoxH);
                
                // Platform menu item hover (if menu is open) - menu is BELOW the box
                int oldMenuHover = g_platformMenuHover;
                g_platformMenuHover = -1;
                if (g_platformMenuOpen) {
                    int menuY = platformBoxY + platformBoxH + 4;  // Menu below platform box
                    int menuW = platformBoxW;
                    
                    for (int i = 0; i < g_platformCount; i++) {
                        int itemY = menuY + 4 + i * menuItemH;
                        if (IsInRect(x, y, platformBoxX + 4, itemY, menuW - 8, menuItemH - 4)) {
                            g_platformMenuHover = i;
                            break;
                        }
                    }
                    
                    // When menu is open, don't detect button hovers
                    g_btnConnectHover = false;
                    g_btnLaunchHover = false;
                } else {
                    // Only detect button hovers when menu is closed
                    g_btnConnectHover = IsInRect(x, y, CARD_PADDING + 18, btnY, leftColW - 36, 40);
                    g_btnLaunchHover = !g_neteaseConnected && IsInRect(x, y, CARD_PADDING + 18, launchBtnY, leftColW - 36, 36);
                }
                g_btnExportLogHover = false;
                g_btnAdminHover = false;
                
                // Check if we need to redraw
                if (g_platformBoxHover != oldPlatformBoxHover || g_platformMenuHover != oldMenuHover) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            } else if (g_currentTab == 1) {
                // Performance tab hover detection
                int modeBtnW = 70;
                int modeBtnH = 32;
                int modeBtnSpacing = 6;
                int totalModeBtnW = 2 * modeBtnW + modeBtnSpacing;
                int modeBtnStartX = CARD_PADDING + 18 + (leftColW - 36 - totalModeBtnW) / 2;
                int modeBtnY = rowY + 30;

                // Display mode buttons
                for (int m = 0; m < 2; m++) {
                    int btnX = modeBtnStartX + m * (modeBtnW + modeBtnSpacing);
                    g_displayModeHover[m] = IsInRect(x, y, btnX, modeBtnY, modeBtnW, modeBtnH);
                }

                // Auto Detect and Show Order buttons
                int btnW = leftColW - 36;
                int btnH = 36;
                // 按钮位置与系统信息区域同步
                double expandAnim = g_systemInfoExpandAnim.value;
                int infoH = (int)(140 * expandAnim);
                bool showSystemInfo = (g_performanceMode == 1 && expandAnim > 0.01) || (g_systemInfoExpandAnim.isActive() && expandAnim > 0.01);
                double btnExpandAnim = (showSystemInfo && infoH > 4) ? expandAnim : 0.0;
                int baseBtnY = modeBtnY + modeBtnH + 20;
                int expandedBtnY = baseBtnY + 140 + 20;
                int btnY = (int)(baseBtnY + (expandedBtnY - baseBtnY) * btnExpandAnim);
                g_autoDetectHover = IsInRect(x, y, CARD_PADDING + 18, btnY, btnW, btnH);
                g_showOrderHover = IsInRect(x, y, CARD_PADDING + 18, btnY + 44, btnW, btnH);

            } else {
                // Close platform menu when switching tabs
                g_platformMenuOpen = false;
                g_menuExpandAnim.value = 0.0;
                g_menuExpandAnim.target = 0.0;
                g_platformBoxHover = false;
                g_platformMenuHover = -1;
                g_btnConnectHover = false;
                g_btnLaunchHover = false;
                // Settings tab positions (calculated same as drawing code):
                // rowY starts at contentY + 55
                int ipRowY = rowY;
                int portRowY = rowY + 75;
                int checkboxRowY = rowY + 75 + 70;
                int autoUpdateRowY = checkboxRowY + 38;
                int showPlatformRowY = autoUpdateRowY + 38;
                int trayRowY = showPlatformRowY + 38;
                int startMinimizedRowY = trayRowY + 38;
                int autoStartRowY = startMinimizedRowY + 38;
                int runAsAdminRowY = autoStartRowY + 38;
                int adminStatusY = runAsAdminRowY + 38;
                int hotkeyRowY = adminStatusY + 50;
                int exportLogRowY = hotkeyRowY + 55;
                
                int hotkeyBoxX = checkboxX + 90;
                
                g_btnAdminHover = !IsRunningAsAdmin() && IsInRect(x, y, checkboxX, adminStatusY, 150, 22);
                g_hotkeyBoxHover = IsInRect(x, y, hotkeyBoxX, hotkeyRowY, 120, 32);
                // 只有当按钮在可见区域内时才检测悬停
                g_btnExportLogHover = (exportLogRowY + 38 < g_winH - CARD_PADDING) && 
                                       IsInRect(x, y, checkboxX, exportLogRowY, leftColW - 36 - checkboxX, 38);
                
                // Checkbox hover detection (also include the label area for better UX)
                int checkboxSize = 26;
                int labelWidth = 120;  // 大致标签宽度
                g_checkboxHover[0] = IsInRect(x, y, checkboxX, checkboxRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[1] = IsInRect(x, y, checkboxX, autoUpdateRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[2] = IsInRect(x, y, checkboxX, showPlatformRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[3] = IsInRect(x, y, checkboxX, trayRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[4] = IsInRect(x, y, checkboxX, startMinimizedRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[5] = IsInRect(x, y, checkboxX, autoStartRowY, checkboxSize + labelWidth, checkboxSize);
                g_checkboxHover[6] = IsInRect(x, y, checkboxX, runAsAdminRowY, checkboxSize + labelWidth, checkboxSize);
            }
            
            // Clear checkbox hover when not in settings tab
            if (g_currentTab != 2) {
                for (int i = 0; i < 7; i++) g_checkboxHover[i] = false;
            }
            
            g_btnApplyHover = false;
            g_btnThemeHover = IsInRect(x, y, g_winW - 160, 14, 38, 38);
            g_btnThemeHover = IsInRect(x, y, g_winW - 210, 14, 38, 38);
            g_btnUpdateHover = IsInRect(x, y, g_winW - 160, 14, 38, 38);
            g_btnMinHover = IsInRect(x, y, g_winW - 110, 14, 38, 38);
            g_btnCloseHover = IsInRect(x, y, g_winW - 60, 14, 38, 38);
            
            // 字符数进度条hover检测
            bool oldCharProgressHover = g_charProgressHover;
            int rightColX = leftColW + CARD_PADDING * 2;
            int previewH = g_winH - contentY - CARD_PADDING;
            int labelY = contentY + previewH - 30;
            int barX = rightColX + 18 + 50;  // 标签后面
            int barH = 12;
            int barY = labelY + 6;  // 与绘制代码一致
            int barW = 60 + 3;  // 电池宽度+尖端
            g_charProgressHover = IsInRect(x, y, barX - 5, barY - 3, barW + 10, barH + 6);  // 扩大检测范围

            if (oldConnect != g_btnConnectHover || oldApply != g_btnApplyHover ||
                oldClose != g_btnCloseHover || oldMin != g_btnMinHover || oldUpdate != g_btnUpdateHover ||
                oldLaunch != g_btnLaunchHover || oldExportLog != g_btnExportLogHover ||
                oldTheme != g_btnThemeHover ||
                oldTabHover[0] != g_tabHover[0] || oldTabHover[1] != g_tabHover[1] || oldTabHover[2] != g_tabHover[2] ||
                g_btnAdminHover != oldAdmin || g_charProgressHover != oldCharProgressHover || g_hotkeyBoxHover != oldHotkeyBox) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            // Theme toggle button (sun/moon)
            if (IsInRect(x, y, g_winW - 210, 14, 38, 38)) {
                // 触发主题切换过渡动画
                g_themeTransition.value = 0.0;
                g_themeTransition.target = 1.0;
                g_themeTransition.speed = 0.06;
                
                // 添加轻微的缩放效果
                g_windowScaleAnim.value = 0.98;
                g_windowScaleAnim.target = 1.0;
                g_windowScaleAnim.speed = 0.12;
                
                // 深浅色切换功能已移除，固定使用深色模式
// g_darkMode = !g_darkMode;  // 已禁用
                UpdateThemeColors();
                EnableBlurBehind(hwnd);
                UpdateWindowSystemTheme(hwnd);
                if (g_brushBg) DeleteObject(g_brushBg);
                if (g_brushCard) DeleteObject(g_brushCard);
                g_brushBg = CreateSolidBrush(COLOR_BG);
                g_brushCard = CreateSolidBrush(COLOR_CARD);
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            
            // Update button click
            if (IsInRect(x, y, g_winW - 160, 14, 38, 38)) {
                if (g_downloadingUpdate) {
                    // Download in progress, ignore
                    return 0;
                }
                if (g_updateAvailable && !g_downloadUrl.empty()) {
                    // Show update dialog with changelog
                    std::wstring msg = L"\x53D1\x73B0\x65B0\x7248\x672C v" + g_latestVersion + L"\n\n";
                    if (!g_latestChangelog.empty()) {
                        msg += L"\x66F4\x65B0\x5185\x5BB9:\n";
                        // Limit changelog to first 500 chars
                        std::wstring changelog = g_latestChangelog;
                        if (changelog.length() > 500) {
                            changelog = changelog.substr(0, 500) + L"...";
                        }
                        msg += changelog + L"\n\n";
                    }
                    msg += L"\x66F4\x65B0\x540E\x7A0B\x5E8F\x5C06\x81EA\x52A8\x91CD\x542F";
                    
                    int result = ShowUpdateDialog(L"\x66F4\x65B0\x63D0\x793A", msg);
                    if (result == 1) {
                        // Update button clicked
                        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                            if (DownloadAndInstallUpdate()) {
                                ShowInfoDialog(L"\x66F4\x65B0", L"\x4E0B\x8F7D\x5B8C\x6210\xFF01\n\x7A0B\x5E8F\x5373\x5C06\x5173\x95ED\x5E76\x81EA\x52A8\x66F4\x65B0\x3002");
                                PostMessage(g_hwnd, WM_CLOSE, 0, 0);
                            } else {
                                ShowErrorDialog(L"\x9519\x8BEF", L"\x4E0B\x8F7D\x5931\x8D25\xFF0C\x8BF7\x624B\x52A8\x4E0B\x8F7D\x66F4\x65B0\x3002");
                            }
                            return 0;
                        }, nullptr, 0, nullptr);
                    } else if (result == 3) {
                        // Skip this version
                        g_skipVersion = g_latestVersion;
                        g_updateAvailable = false;
                        SaveConfig(g_configPath);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } else if (!g_checkingUpdate) {
                    // Manual check for updates
                    g_updateCheckComplete = false;
                    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                        CheckForUpdate(true);
                        if (g_hwnd) {
                            InvalidateRect(g_hwnd, nullptr, FALSE);
                            // Post message to show result
                            PostMessage(g_hwnd, WM_USER + 100, 0, 0);
                        }
                        return 0;
                    }, nullptr, 0, nullptr);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            
            if (y < TITLEBAR_H && x < g_winW - 120) {
                g_dragging = true;
                POINT pt; GetCursorPos(&pt);
                RECT rc; GetWindowRect(hwnd, &rc);
                g_dragStart.x = pt.x - rc.left;
                g_dragStart.y = pt.y - rc.top;
                SetCapture(hwnd);
                return 0;
            }
            
            // Layout positions - must match OnPaint exactly
            int leftColW = 260;
            int contentY = TITLEBAR_H + 20;
            int tabH = 32;
            int tabY = contentY + 10;  // Tabs inside card
            int rowY = contentY + 55;
            int checkboxX = CARD_PADDING + 18;
            int checkboxSize = 26;
            
            // === Tab clicks ===
            int tabW = 80;
            for (int t = 0; t < 3; t++) {  // 3个标签页：主页、性能、设置
                int tabX = CARD_PADDING + 18 + t * (tabW + 8);
                if (IsInRect(x, y, tabX, tabY, tabW, tabH)) {
                    if (g_currentTab != t) {
                        // 触发标签页滑动动画
                        g_tabSlideDirection = (t > g_currentTab) ? 1 : -1;  // 1=右滑, -1=左滑
                        g_prevTab = g_currentTab;
                        g_tabSlideAnim.value = 0.0;
                        g_tabSlideAnim.target = 1.0;
                        g_tabSlideAnim.speed = 0.12;
                        g_currentTab = t;
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            
            if (g_currentTab == 0) {
                // === MAIN TAB click handling ===
                int platformBoxX = CARD_PADDING + 18;
                int platformBoxY = rowY + 22;
                int platformBoxW = leftColW - 36;
                int platformBoxH = 36;
                int menuItemH = 36;
                int menuH = g_platformCount * menuItemH + 8;
                int btnY = rowY + 80;  // Match drawing position (increased spacing)
                int launchBtnY = btnY + 48;
                
                // Platform menu item click (if menu is open) - menu is BELOW the box
                if (g_platformMenuOpen) {
                    int menuY = platformBoxY + platformBoxH + 4;  // Menu below platform box
                    
                    for (int i = 0; i < g_platformCount; i++) {
                        int itemY = menuY + 4 + i * menuItemH;
                        if (IsInRect(x, y, platformBoxX + 4, itemY, platformBoxW - 8, menuItemH - 4)) {
                            if (g_platforms[i].connected) {
                                // Clear lyrics state when switching platform
                                if (g_activePlatform != i) {
                                    g_pendingLyrics.clear();
                                    g_qqMusicLastTitle.clear();
                                    g_qqMusicLastArtist.clear();
                                }
                                g_activePlatform = i;
                                g_autoPlatformSwitch = false;
                            }
                            g_platformMenuOpen = false;
                            g_menuExpandAnim.value = 1.0;
                            g_menuExpandAnim.target = 0.0;  // 触发收起动画
                            InvalidateRect(hwnd, nullptr, FALSE);
                            return 0;
                        }
                    }
                    
                    // Click outside menu - close it (but not on platform box)
                    if (!IsInRect(x, y, platformBoxX, platformBoxY, platformBoxW, platformBoxH)) {
                        g_platformMenuOpen = false;
                        g_menuExpandAnim.value = 1.0;
                        g_menuExpandAnim.target = 0.0;  // 触发收起动画
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                }
                
                // Platform box click - toggle menu
                if (IsInRect(x, y, platformBoxX, platformBoxY, platformBoxW, platformBoxH)) {
                    g_platformMenuOpen = !g_platformMenuOpen;
                    // 触发菜单展开/收起动画
                    g_menuExpandAnim.value = g_platformMenuOpen ? 0.0 : 1.0;
                    g_menuExpandAnim.target = g_platformMenuOpen ? 1.0 : 0.0;
                    g_menuExpandAnim.speed = 0.15;
                    // 触发箭头旋转动画
                    g_arrowRotationAnim.value = g_platformMenuOpen ? 0.0 : 1.0;
                    g_arrowRotationAnim.target = g_platformMenuOpen ? 1.0 : 0.0;
                    g_arrowRotationAnim.speed = 0.15;
                    // 启动每个菜单项的逐条淡入动画（延迟递增）
                    if (g_platformMenuOpen) {
                        for (int i = 0; i < g_platformCount && i < 10; i++) {
                            g_menuItemAnims[i].value = 0.0;
                            g_menuItemAnims[i].target = 1.0;
                            g_menuItemAnims[i].speed = 0.15;  // 统一速度，通过延迟实现效果
                        }
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                
                // Connect button
                if (IsInRect(x, y, CARD_PADDING + 18, btnY, leftColW - 36, 40)) {
                    g_autoPlatformSwitch = true;  // Re-enable auto switch when reconnecting
                    // 只在菜单打开时才触发收起动画
                    if (g_platformMenuOpen) {
                        g_platformMenuOpen = false;
                        g_menuExpandAnim.value = 1.0;
                        g_menuExpandAnim.target = 0.0;  // 触发收起动画
                    }
                    ApplySettings();
                    Connect();
                }
                // Launch Netease button (only if Netease not connected)
                else if (!g_neteaseConnected && IsInRect(x, y, CARD_PADDING + 18, launchBtnY, leftColW - 36, 32)) {
                // Find Netease Cloud Music installation path
                wchar_t ncmPath[MAX_PATH] = {0};
                
                // Helper function to check if file exists
                auto fileExists = [](const wchar_t* path) -> bool {
                    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
                };
                
                // 1. Search all drives for common installation paths
                const wchar_t* drives[] = { L"C:", L"D:", L"E:", L"F:", L"G:", L"H:" };
                const wchar_t* subPaths[] = {
                    L"\\Program Files\\Netease\\CloudMusic\\cloudmusic.exe",
                    L"\\Program Files (x86)\\Netease\\CloudMusic\\cloudmusic.exe",
                    L"\\Netease\\CloudMusic\\cloudmusic.exe",
                    L"\\Program Files\\CloudMusic\\cloudmusic.exe",
                    L"\\Program Files (x86)\\CloudMusic\\cloudmusic.exe",
                    L"\\CloudMusic\\cloudmusic.exe",
                    L"\\Apps\\Netease\\CloudMusic\\cloudmusic.exe",
                    L"\\Software\\Netease\\CloudMusic\\cloudmusic.exe",
                    L"\\Software\\CloudMusic\\cloudmusic.exe",
                    L"\\Games\\Netease\\CloudMusic\\cloudmusic.exe",
                };
                
                for (int d = 0; d < 6 && ncmPath[0] == 0; d++) {
                    for (int s = 0; s < 10; s++) {
                        wchar_t fullPath[MAX_PATH];
                        swprintf_s(fullPath, L"%s%s", drives[d], subPaths[s]);
                        if (fileExists(fullPath)) {
                            wcscpy_s(ncmPath, fullPath);
                            break;
                        }
                    }
                }
                
                // 2. Check registry - multiple locations
                if (ncmPath[0] == 0) {
                    HKEY hKey;
                    const wchar_t* regPaths[] = {
                        L"Software\\Netease\\CloudMusic",
                        L"Software\\WOW6432Node\\Netease\\CloudMusic",
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NeteaseCloudMusic",
                    };
                    for (int r = 0; r < 3 && ncmPath[0] == 0; r++) {
                        if (RegOpenKeyExW(HKEY_CURRENT_USER, regPaths[r], 0, KEY_READ, &hKey) == ERROR_SUCCESS ||
                            RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPaths[r], 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                            DWORD size = MAX_PATH * sizeof(wchar_t);
                            if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr, (LPBYTE)ncmPath, &size) == ERROR_SUCCESS ||
                                RegQueryValueExW(hKey, L"InstallLocation", nullptr, nullptr, (LPBYTE)ncmPath, &size) == ERROR_SUCCESS ||
                                RegQueryValueExW(hKey, L"Path", nullptr, nullptr, (LPBYTE)ncmPath, &size) == ERROR_SUCCESS) {
                                wcscat_s(ncmPath, L"\\cloudmusic.exe");
                                if (!fileExists(ncmPath)) ncmPath[0] = 0;
                            }
                            RegCloseKey(hKey);
                        }
                    }
                }
                
                // 3. Check start menu shortcut
                if (ncmPath[0] == 0) {
                    wchar_t startMenu[MAX_PATH];
                    if (SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenu) == S_OK) {
                        wchar_t shortcutPath[MAX_PATH];
                        swprintf_s(shortcutPath, L"%s\\%s.lnk", startMenu, L"\x7F51\x6613\x4E91\x97F3\x4E50");
                        if (fileExists(shortcutPath)) {
                            // Resolve shortcut using IShellLink
                            HRESULT hr = CoInitialize(nullptr);
                            if (SUCCEEDED(hr)) {
                                IShellLinkW* pShellLink = nullptr;
                                if (CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink) == S_OK) {
                                    IPersistFile* pPersistFile = nullptr;
                                    if (pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile) == S_OK) {
                                        if (pPersistFile->Load(shortcutPath, STGM_READ) == S_OK) {
                                            if (pShellLink->GetPath(ncmPath, MAX_PATH, nullptr, 0) == S_OK) {
                                                if (!fileExists(ncmPath)) ncmPath[0] = 0;
                                            }
                                        }
                                        pPersistFile->Release();
                                    }
                                    pShellLink->Release();
                                }
                                CoUninitialize();
                            }
                        }
                    }
                }
                
                // 4. Search PATH environment variable
                if (ncmPath[0] == 0) {
                    wchar_t pathEnv[4096];
                    if (GetEnvironmentVariableW(L"PATH", pathEnv, 4096) > 0) {
                        wchar_t* token = wcstok_s(pathEnv, L";", nullptr);
                        while (token && ncmPath[0] == 0) {
                            wchar_t fullPath[MAX_PATH];
                            swprintf_s(fullPath, L"%s\\cloudmusic.exe", token);
                            if (fileExists(fullPath)) {
                                wcscpy_s(ncmPath, fullPath);
                            }
                            token = wcstok_s(nullptr, L";", nullptr);
                        }
                    }
                }
                
                // 5. Check AppData local
                if (ncmPath[0] == 0) {
                    wchar_t appData[MAX_PATH];
                    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appData) == S_OK) {
                        wchar_t fullPath[MAX_PATH];
                        swprintf_s(fullPath, L"%s\\Netease\\CloudMusic\\cloudmusic.exe", appData);
                        if (fileExists(fullPath)) {
                            wcscpy_s(ncmPath, fullPath);
                        }
                    }
                }
                
                // 6. Check if already running - need to restart with debug port
                bool alreadyRunning = false;
                HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                if (hSnapshot != INVALID_HANDLE_VALUE) {
                    PROCESSENTRY32W pe = {sizeof(pe)};
                    if (Process32FirstW(hSnapshot, &pe)) {
                        do {
                            if (_wcsicmp(pe.szExeFile, L"cloudmusic.exe") == 0) {
                                alreadyRunning = true;
                                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                                if (hProcess) {
                                    DWORD size = MAX_PATH;
                                    if (QueryFullProcessImageNameW(hProcess, 0, ncmPath, &size)) {
                                        // Got path from running instance
                                    }
                                    CloseHandle(hProcess);
                                }
                                break;
                            }
                        } while (Process32NextW(hSnapshot, &pe));
                    }
                    CloseHandle(hSnapshot);
                }
                
                if (alreadyRunning) {
                    // Netease is already running - need to restart with debug port
                    bool result = ShowConfirmDialog(L"\x63D0\x793A", 
                        L"\x7F51\x6613\x4E91\x97F3\x4E50\x5DF2\x5728\x8FD0\x884C\x4E2D\x3002\n\n"
                        L"\x9700\x8981\x91CD\x65B0\x542F\x52A8\x624D\x80FD\x542F\x7528\x8C03\x8BD5\x7AEF\x53E3\x3002\n\n"
                        L"\x662F\x5426\x5173\x95ED\x5E76\x91CD\x65B0\x542F\x52A8\xFF1F",
                        L"\x91CD\x542F", L"\x53D6\x6D88");
                    if (result) {
                        // Kill all cloudmusic.exe processes
                        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                        if (hSnap != INVALID_HANDLE_VALUE) {
                            PROCESSENTRY32W pe = {sizeof(pe)};
                            if (Process32FirstW(hSnap, &pe)) {
                                do {
                                    if (_wcsicmp(pe.szExeFile, L"cloudmusic.exe") == 0) {
                                        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                                        if (hProc) {
                                            TerminateProcess(hProc, 0);
                                            CloseHandle(hProc);
                                        }
                                    }
                                } while (Process32NextW(hSnap, &pe));
                            }
                            CloseHandle(hSnap);
                        }
                        Sleep(1000);  // Wait for processes to terminate
                    } else {
                        return 0;  // User cancelled
                    }
                }
                
                if (ncmPath[0] != 0) {
                    // Launch with remote debugging port
                    wchar_t cmdLine[512];
                    wsprintfW(cmdLine, L"\"%s\" --remote-debugging-port=9222", ncmPath);
                    STARTUPINFOW si = {sizeof(si)};
                    PROCESS_INFORMATION pi;
                    if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        
                        // Wait for Netease to start, then auto-connect
                                    g_pendingTitle = L"\x7B49\x5F85\x7F51\x6613\x4E91\x542F\x52A8...";
                                    g_pendingArtist = L"";
                                    g_needsRedraw = true;                        InvalidateRect(hwnd, nullptr, FALSE);
                        
                        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                            Sleep(5000);  // Wait 5 seconds for Netease to initialize
                            if (g_currentPlatform == 1 && !g_isConnected) {
                                Connect();
                            }
                            return 0;
                        }, nullptr, 0, nullptr);
                    }
                } else {
                    ShowErrorDialog(L"\x9519\x8BEF", L"\x672A\x627E\x5230\x7F51\x6613\x4E91\x97F3\x4E50\xFF0C\x8BF7\x624B\x52A8\x542F\x52A8\x5E76\x6DFB\x52A0\x53C2\x6570:\n--remote-debugging-port=9222");
                }
            }
        } else if (g_currentTab == 1) {
            // === PERFORMANCE TAB click handling ===
            int leftColW = 260;
            int rowY = TITLEBAR_H + 50 + 30;

            // Display mode buttons (must match drawing code)
            int modeBtnW = 70;
            int modeBtnH = 32;
            int modeBtnSpacing = 6;
            int totalModeBtnW = 2 * modeBtnW + modeBtnSpacing;
            int modeBtnStartX = CARD_PADDING + 18 + (leftColW - 36 - totalModeBtnW) / 2;
            int modeBtnY = rowY + 30;

            for (int m = 0; m < 2; m++) {
                int btnX = modeBtnStartX + m * (modeBtnW + modeBtnSpacing);
                if (IsInRect(x, y, btnX, modeBtnY, modeBtnW, modeBtnH)) {
                    if (g_performanceMode != m) {
                        g_prevDisplayMode = g_performanceMode;
                        g_performanceMode = m;
                        g_displayModeSlideAnim.setTarget(1.0);
                        g_displayModeSlideAnim.value = 0.0;
                        
                        // 清除OSC消息缓存，避免切换模式后立即发送
                        g_lastOscMessage.clear();

                        // 触发系统信息展开/收缩动画
                        // 使用当前value作为起点，避免突然跳变
                        if (m == 1) {
                            // 切换到性能模式，展开（从当前位置开始动画）
                            // 如果当前值很小，设置一个最小值让动画更平滑
                            if (g_systemInfoExpandAnim.value < 0.02) {
                                g_systemInfoExpandAnim.value = 0.02;
                            }
                            g_systemInfoExpandAnim.target = 1.0;
                            g_systemInfoExpandAnim.speed = 0.08;
                        } else {
                            // 切换到音乐模式，收缩（从当前位置开始动画）
                            g_systemInfoExpandAnim.target = 0.0;
                            g_systemInfoExpandAnim.speed = 0.08;
                        }

                        SaveConfig(g_configPath);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                    return 0;
                }
            }

            // System info input boxes
            double expandAnim = g_systemInfoExpandAnim.value;
            bool showSystemInfo = (g_performanceMode == 1 && expandAnim > 0.01) || (g_systemInfoExpandAnim.isActive() && expandAnim > 0.01);

            if (showSystemInfo) {
                int infoX = CARD_PADDING + 18;
                int infoY = modeBtnY + modeBtnH + 20;
                int infoW = leftColW - 36;
                int inputH = 36;
                int inputY = infoY + 15;

                // CPU input click
                if (IsInRect(x, y, infoX + 50, inputY, infoW - 50, inputH)) {
                    g_editingField = EDIT_CPU_NAME;
                    g_editingText = g_cpuDisplayName;
                    g_cursorPos = (int)g_editingText.length();
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                // RAM input click
                inputY += 45;
                if (IsInRect(x, y, infoX + 50, inputY, infoW - 50, inputH)) {
                    g_editingField = EDIT_RAM_NAME;
                    g_editingText = g_ramDisplayName;
                    g_cursorPos = (int)g_editingText.length();
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }

                // GPU input click
                inputY += 45;
                if (IsInRect(x, y, infoX + 50, inputY, infoW - 50, inputH)) {
                    g_editingField = EDIT_GPU_NAME;
                    g_editingText = g_gpuDisplayName;
                    g_cursorPos = (int)g_editingText.length();
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }

            // Auto Detect and Show Order buttons
            int btnW = leftColW - 36;
            int btnH = 36;
            int btnY = showSystemInfo ? (modeBtnY + modeBtnH + 20 + 140 + 20) : (modeBtnY + modeBtnH + 20);

            if (IsInRect(x, y, CARD_PADDING + 18, btnY, btnW, btnH)) {
                // Auto Detect button clicked
                ShowInfoDialog(L"\x63D0\x793A", L"\x81EA\x52A8\x68C0\x6D4B\x529F\x80FD\x5F85\x5B9E\x73B0");
                return 0;
            }

            btnY += 44;
            if (IsInRect(x, y, CARD_PADDING + 18, btnY, btnW, btnH)) {
                // Show Order button clicked
                ShowInfoDialog(L"\x63D0\x793A", L"\x663E\x793A\x987A\x5E8F\x529F\x80FD\x5F85\x5B9E\x73B0");
                return 0;
            }

        } else {
            // === SETTINGS TAB click handling ===
            // IP and Port input fields - show hint to edit config file
            int ipRowY = rowY;
            int portRowY = rowY + 75;
            int checkboxRowY = rowY + 75 + 70;
            int autoUpdateRowY = checkboxRowY + 38;
            int showPlatformRowY = autoUpdateRowY + 38;
            int trayRowY = showPlatformRowY + 38;
            int startMinimizedRowY = trayRowY + 38;
            int autoStartRowY = startMinimizedRowY + 38;
            int runAsAdminRowY = autoStartRowY + 38;
            int adminStatusY = runAsAdminRowY + 38;
            int hotkeyRowY = adminStatusY + 50;
            int exportLogRowY = hotkeyRowY + 55;
            
            // Click on IP or Port input field - show hint
            if (IsInRect(x, y, CARD_PADDING + 18, ipRowY + 24, leftColW - 36, 36) ||
                IsInRect(x, y, CARD_PADDING + 18, portRowY + 24, leftColW - 36, 36)) {
                ShowInfoDialog(L"\x63D0\x793A", L"\x8BF7\x7F16\x8F91 config_gui.json \x4FEE\x6539 IP \x548C\x7AEF\x53E3\n\x4FEE\x6539\x540E\x91CD\x65B0\x542F\x52A8\x7A0B\x5E8F\x751F\x6548");
                return 0;
            }
            // Checkbox for show performance
            if (IsInRect(x, y, checkboxX, checkboxRowY, checkboxSize, checkboxSize)) {
                g_showPerfOnPause = !g_showPerfOnPause;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for auto update
            if (IsInRect(x, y, checkboxX, autoUpdateRowY, checkboxSize, checkboxSize)) {
                g_autoUpdate = !g_autoUpdate;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for show platform
            if (IsInRect(x, y, checkboxX, showPlatformRowY, checkboxSize, checkboxSize)) {
                g_showPlatform = !g_showPlatform;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for minimize to tray
            if (IsInRect(x, y, checkboxX, trayRowY, checkboxSize, checkboxSize)) {
                g_minimizeToTray = !g_minimizeToTray;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for start minimized
            if (IsInRect(x, y, checkboxX, startMinimizedRowY, checkboxSize, checkboxSize)) {
                g_startMinimized = !g_startMinimized;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for auto start
            if (IsInRect(x, y, checkboxX, autoStartRowY, checkboxSize, checkboxSize)) {
                g_autoStart = !g_autoStart;
                SetAutoStart(g_autoStart);
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for run as admin
            if (IsInRect(x, y, checkboxX, runAsAdminRowY, checkboxSize, checkboxSize)) {
                g_runAsAdmin = !g_runAsAdmin;
                SaveConfig(g_configPath);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Admin link (only if not already admin)
            if (!IsRunningAsAdmin() && IsInRect(x, y, checkboxX, adminStatusY, 150, 22)) {
                if (ShowConfirmDialog(L"\x7BA1\x7406\x5458\x6743\x9650", 
                    L"\x4EE5\x7BA1\x7406\x5458\x8EAB\x4EFD\x91CD\x542F\x53EF\x4EE5\x4FDD\x8BC1\x6240\x6709\x529F\x80FD\x6B63\x5E38\x8FD0\x884C\x3002\n\n\x662F\x5426\x73B0\x5728\x91CD\x542F\xFF1F",
                    L"\x91CD\x542F", L"\x53D6\x6D88")) {
                    RestartAsAdmin(hwnd);
                }
                return 0;
            }
            // Hotkey input field click - enter editing mode
            int hotkeyBoxX = checkboxX + 90;
            if (IsInRect(x, y, hotkeyBoxX, hotkeyRowY, 120, 32)) {
                g_editingHotkey = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Export log button - only if visible
            if (exportLogRowY + 38 < g_winH - CARD_PADDING && 
                IsInRect(x, y, checkboxX, exportLogRowY, leftColW - 36 - checkboxX, 32)) {
                ExportLogs(hwnd);
                return 0;
            }
        }

        // Close button (works on both tabs)
        if (IsInRect(x, y, g_winW - 60, 14, 38, 38)) {
            if (g_minimizeToTray) ShowWindow(hwnd, SW_HIDE);
            else PostMessage(hwnd, WM_CLOSE, 0, 0);
        }
        // Minimize button
        else if (IsInRect(x, y, g_winW - 110, 14, 38, 38)) {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
        return 0;
    }
    
    case WM_LBUTTONUP: if (g_dragging) { g_dragging = false; ReleaseCapture(); } return 0;

        case WM_CHAR: {
            // Handle character input for editing fields
            if (g_editingField != EDIT_NONE) {
                wchar_t ch = (wchar_t)wParam;

                // Allow printable characters (no control characters except space)
                if (ch >= 32 && ch != 127) {
                    // Delete selected text if any
                    if (g_selectStart != g_selectEnd) {
                        int start = min(g_selectStart, g_selectEnd);
                        int end = max(g_selectStart, g_selectEnd);
                        g_editingText.erase(start, end - start);
                        g_cursorPos = start;
                        g_selectStart = g_selectEnd = g_cursorPos;
                    }

                    // Insert character at cursor position
                    if (g_cursorPos < (int)g_editingText.length()) {
                        g_editingText.insert(g_cursorPos, 1, ch);
                    } else {
                        g_editingText += ch;
                    }
                    g_cursorPos++;
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();

                    // Update corresponding display name
                    switch (g_editingField) {
                        case EDIT_CPU_NAME: g_cpuDisplayName = g_editingText; break;
                        case EDIT_RAM_NAME: g_ramDisplayName = g_editingText; break;
                        case EDIT_GPU_NAME: g_gpuDisplayName = g_editingText; break;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            // Handle hotkey editing
            if (g_editingHotkey) {
                // Escape cancels editing
                if (wParam == VK_ESCAPE) {
                    g_editingHotkey = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                
                // Capture the new hotkey
                UINT newKey = (UINT)wParam;
                UINT newMods = 0;
                
                // Check modifier keys
                if (GetKeyState(VK_CONTROL) & 0x8000) newMods |= MOD_CONTROL;
                if (GetKeyState(VK_MENU) & 0x8000) newMods |= MOD_ALT;  // Alt key
                if (GetKeyState(VK_SHIFT) & 0x8000) newMods |= MOD_SHIFT;
                if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) newMods |= MOD_WIN;
                
                // Don't allow modifier-only hotkeys
                if (newKey != VK_CONTROL && newKey != VK_SHIFT && newKey != VK_MENU && newKey != VK_LWIN && newKey != VK_RWIN) {
                    // Unregister old hotkey
                    UnregisterHotKey(hwnd, HOTKEY_OSC_PAUSE);
                    
                    // Update hotkey
                    g_oscPauseHotkey = newKey;
                    g_oscPauseHotkeyMods = newMods;
                    
                    // Register new hotkey
                    RegisterHotKey(hwnd, HOTKEY_OSC_PAUSE, g_oscPauseHotkeyMods, g_oscPauseHotkey);
                    
                    // Save config
                    SaveConfig(g_configPath);
                    
                    char debugBuf[128];
                    sprintf_s(debugBuf, "[Hotkey] Changed to mods=%d, key=%d", g_oscPauseHotkeyMods, g_oscPauseHotkey);
                    MainDebugLog(debugBuf);
                }
                
                g_editingHotkey = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            
            // Handle keyboard input for editing fields
            if (g_editingField != EDIT_NONE) {
                if (wParam == VK_BACK) {
                    // Backspace: delete character before cursor
                    if (g_selectStart != g_selectEnd) {
                        // Delete selected text
                        int start = min(g_selectStart, g_selectEnd);
                        int end = max(g_selectStart, g_selectEnd);
                        g_editingText.erase(start, end - start);
                        g_cursorPos = start;
                        g_selectStart = g_selectEnd = g_cursorPos;
                    } else if (g_cursorPos > 0) {
                        // Delete character before cursor
                        g_editingText.erase(g_cursorPos - 1, 1);
                        g_cursorPos--;
                        g_selectStart = g_selectEnd = g_cursorPos;
                    }
                    g_lastCursorBlink = GetTickCount();

                    // Update corresponding display name
                    switch (g_editingField) {
                        case EDIT_CPU_NAME: g_cpuDisplayName = g_editingText; break;
                        case EDIT_RAM_NAME: g_ramDisplayName = g_editingText; break;
                        case EDIT_GPU_NAME: g_gpuDisplayName = g_editingText; break;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_DELETE) {
                    // Delete: delete character at cursor
                    if (g_selectStart != g_selectEnd) {
                        // Delete selected text
                        int start = min(g_selectStart, g_selectEnd);
                        int end = max(g_selectStart, g_selectEnd);
                        g_editingText.erase(start, end - start);
                        g_cursorPos = start;
                        g_selectStart = g_selectEnd = g_cursorPos;
                    } else if (g_cursorPos < (int)g_editingText.length()) {
                        // Delete character at cursor
                        g_editingText.erase(g_cursorPos, 1);
                        g_selectStart = g_selectEnd = g_cursorPos;
                    }
                    g_lastCursorBlink = GetTickCount();

                    // Update corresponding display name
                    switch (g_editingField) {
                        case EDIT_CPU_NAME: g_cpuDisplayName = g_editingText; break;
                        case EDIT_RAM_NAME: g_ramDisplayName = g_editingText; break;
                        case EDIT_GPU_NAME: g_gpuDisplayName = g_editingText; break;
                    }

                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_LEFT) {
                    // Left arrow: move cursor left
                    if (g_cursorPos > 0) {
                        g_cursorPos--;
                        g_selectStart = g_selectEnd = g_cursorPos;
                        g_lastCursorBlink = GetTickCount();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } else if (wParam == VK_RIGHT) {
                    // Right arrow: move cursor right
                    if (g_cursorPos < (int)g_editingText.length()) {
                        g_cursorPos++;
                        g_selectStart = g_selectEnd = g_cursorPos;
                        g_lastCursorBlink = GetTickCount();
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } else if (wParam == VK_HOME) {
                    // Home: move cursor to start
                    g_cursorPos = 0;
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_END) {
                    // End: move cursor to end
                    g_cursorPos = (int)g_editingText.length();
                    g_selectStart = g_selectEnd = g_cursorPos;
                    g_lastCursorBlink = GetTickCount();
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_RETURN) {
                    // Enter: finish editing
                    g_editingField = EDIT_NONE;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (wParam == VK_ESCAPE) {
                    // Escape: cancel editing
                    switch (g_editingField) {
                        case EDIT_CPU_NAME: g_cpuDisplayName = g_editingText; break;
                        case EDIT_RAM_NAME: g_ramDisplayName = g_editingText; break;
                        case EDIT_GPU_NAME: g_gpuDisplayName = g_editingText; break;
                    }
                    g_editingField = EDIT_NONE;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }

        case WM_TIMER: {
            // 主定时器逻辑（timer ID = 1）
            if (wParam == 1) {
                UpdateAnimations();
                UpdatePerfStats();

                // 检查OSC暂停状态是否过期（自然结束）
                DWORD now = GetTickCount();
                // 暂停状态检查已移至覆盖层的WM_TIMER，避免冲突
                
                // 更新光标闪烁状态
                if (g_editingField != EDIT_NONE && (now - g_lastCursorBlink) >= 500) {
                    g_cursorVisible = !g_cursorVisible;
                    g_lastCursorBlink = now;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }

                // 暂停状态下每秒更新主窗口倒计时显示
                static DWORD lastPauseUpdate = 0;
                if (g_oscPaused && (now - lastPauseUpdate) >= 1000) {
                    lastPauseUpdate = now;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                
                // 覆盖层窗口有自己的定时器更新，这里不需要更新

                // Smart redraw: only redraw when needed
                
                // Detect system lag recovery (timer interval > 500ms instead of normal 16ms)
                if (g_lastTimerTick > 0 && (now - g_lastTimerTick) > 500) {
                    // System just recovered from lag - reset OSC timer to prevent burst sends
                    g_lastOscSendTime = now;
                    g_systemResumeTime = now;  // Mark recovery time
                    MainDebugLog("[Timer] System lag detected, resetting OSC timers");
                }
                g_lastTimerTick = now;
                
                // After system recovery, wait at least 2 seconds before allowing OSC sends
                // This prevents burst sends from accumulated callbacks
                
                bool animActive = IsAnimationActive();
                
                if (animActive) {
                    // Animation running - need smooth updates
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (g_needsRedraw) {
                    // Content changed - redraw once
                    InvalidateRect(hwnd, nullptr, FALSE);
                    g_needsRedraw = false;
                } else if (now - g_lastContentChange >= IDLE_REDRAW_INTERVAL) {
                    // Periodic refresh for time display etc.
                    InvalidateRect(hwnd, nullptr, FALSE);
                    g_lastContentChange = now;
                }
            }
            return 0;
        }
        
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            else if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                ShowTrayMenu(pt.x, pt.y);
            }
            return 0;
        
        case WM_SIZE: {
            // 忽略最小化状态，此时宽高为0
            if (wParam != SIZE_MINIMIZED) {
                g_winW = LOWORD(lParam);
                g_winH = HIWORD(lParam);
                // 更新窗口圆角区域
                HRGN hrgn = CreateRoundedWindowRegion(hwnd, 12);
                if (hrgn) {
                    SetWindowRgn(hwnd, hrgn, TRUE);
                }
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT) {
                POINT pt = {LOWORD(lParam), HIWORD(lParam)};
                ScreenToClient(hwnd, &pt);
                int x = pt.x, y = pt.y;
                int edgeSize = 8;
                // Check corners first
                if (x < edgeSize && y < edgeSize) return HTTOPLEFT;
                if (x >= g_winW - edgeSize && y < edgeSize) return HTTOPRIGHT;
                if (x < edgeSize && y >= g_winH - edgeSize) return HTBOTTOMLEFT;
                if (x >= g_winW - edgeSize && y >= g_winH - edgeSize) return HTBOTTOMRIGHT;
                // Check edges
                if (x < edgeSize) return HTLEFT;
                if (x >= g_winW - edgeSize) return HTRIGHT;
                if (y < edgeSize) return HTTOP;
                if (y >= g_winH - edgeSize) return HTBOTTOM;
            }
            return hit;
        }
        
        case WM_GETMINMAXINFO: {
            MINMAXINFO* pMMI = (MINMAXINFO*)lParam;
            pMMI->ptMinTrackSize.x = WIN_W_MIN;
            pMMI->ptMinTrackSize.y = WIN_H_MIN;
            return 0;
        }
        
        case WM_USER + 100: {
            // Manual update check completed
            if (g_manualCheckUpdate && g_updateCheckComplete) {
                if (!g_updateAvailable) {
                    // Already latest version
                    std::wstring curVer = Utf8ToWstring(APP_VERSION);
                    std::wstring msg = L"\x5F53\x524D\x7248\x672C v" + curVer + L"\n";
                    msg += L"\x6700\x65B0\x7248\x672C v" + g_latestVersion + L"\n\n";
                    msg += L"\x5DF2\x662F\x6700\x65B0\x7248\x672C\xFF01";
                    ShowInfoDialog(L"\x68C0\x67E5\x66F4\x65B0", msg);
                } else {
                    // New version available - show dialog with changelog
                    std::wstring msg = L"\x53D1\x73B0\x65B0\x7248\x672C v" + g_latestVersion + L"\n\n";
                    if (!g_latestChangelog.empty()) {
                        msg += L"\x66F4\x65B0\x5185\x5BB9:\n";
                        std::wstring changelog = g_latestChangelog;
                        if (changelog.length() > 500) {
                            changelog = changelog.substr(0, 500) + L"...";
                        }
                        msg += changelog + L"\n\n";
                    }
                    msg += L"\x66F4\x65B0\x540E\x7A0B\x5E8F\x5C06\x81EA\x52A8\x91CD\x542F";
                    
                    int result = ShowUpdateDialog(L"\x53D1\x73B0\x65B0\x7248\x672C", msg);
                    if (result == 1) {
                        // Update button clicked
                        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                            if (DownloadAndInstallUpdate()) {
                                ShowInfoDialog(L"\x66F4\x65B0", L"\x4E0B\x8F7D\x5B8C\x6210\xFF01\n\x7A0B\x5E8F\x5373\x5C06\x5173\x95ED\x5E76\x81EA\x52A8\x66F4\x65B0\x3002");
                                PostMessage(g_hwnd, WM_CLOSE, 0, 0);
                            } else {
                                ShowErrorDialog(L"\x9519\x8BEF", L"\x4E0B\x8F7D\x5931\x8D25\xFF0C\x8BF7\x624B\x52A8\x4E0B\x8F7D\x66F4\x65B0\x3002");
                            }
                            return 0;
                        }, nullptr, 0, nullptr);
                    } else if (result == 3) {
                        // Skip this version
                        g_skipVersion = g_latestVersion;
                        g_updateAvailable = false;
                        SaveConfig(g_configPath);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        
        case WM_USER + 101: {
            // SMTC (QQ Music) media update
            if (g_smtcClient) {
                smtc::MediaInfo smtcInfo = g_smtcClient->getCurrentMedia();
                g_smtcConnected = smtcInfo.hasData;
                
                // Update overall connection status
                if (smtcInfo.hasData) {
                    g_isConnected = true;
                }
                
                if (smtcInfo.hasData && smtcInfo.isPlaying) {
                    g_smtcLastPlayTime = GetTickCount();
                    
                    // Auto switch to QQ Music if enabled
                    if (g_autoPlatformSwitch && g_activePlatform != 2) {
                        g_activePlatform = 2;
                        MainDebugLog("[Main] Auto-switched to QQ Music platform");
                    }
                    
                    // Update pending song info for OSC
                    if (g_activePlatform == 2) {
                        g_pendingTitle = smtcInfo.title;
                        g_pendingArtist = smtcInfo.artist;
                        g_pendingDuration = smtcInfo.duration;
                        g_pendingCurrentTime = smtcInfo.position;
                        g_pendingIsPlaying = smtcInfo.isPlaying;
                        g_pendingTime = FormatTime(smtcInfo.position) + L" / " + FormatTime(smtcInfo.duration);
                        g_pendingProgress = smtcInfo.duration > 0 ? smtcInfo.position / smtcInfo.duration : 0;
                        
                        // Check if song changed - search lyrics
                        if (smtcInfo.title != g_qqMusicLastTitle || smtcInfo.artist != g_qqMusicLastArtist) {
                            g_qqMusicLastTitle = smtcInfo.title;
                            g_qqMusicLastArtist = smtcInfo.artist;
                            g_pendingLyrics.clear();  // Clear old lyrics
                            
                            // Search lyrics in background thread
                            if (g_lyricsSearchThread.joinable()) {
                                g_lyricsSearchRunning = false;
                                g_lyricsSearchThread.join();
                            }
                            g_lyricsSearchRunning = true;
                            g_lyricsSearchThread = std::thread([title = smtcInfo.title, artist = smtcInfo.artist, hwnd]() {
                                auto lyrics = SearchLyricsForQQMusic(title, artist);
                                if (g_lyricsSearchRunning && !lyrics.empty()) {
                                    g_pendingLyrics = lyrics;
                                    InvalidateRect(hwnd, nullptr, FALSE);
                                }
                            });
                        }
                        
                        // Send OSC message for QQ Music
                        if (g_osc && g_oscEnabled) {
                            // Build SongInfo from pending data
                            moekoe::SongInfo info;
                            info.title = g_pendingTitle;
                            info.artist = g_pendingArtist;
                            info.duration = g_pendingDuration;
                            info.currentTime = g_pendingCurrentTime;
                            info.isPlaying = g_pendingIsPlaying;
                            info.hasData = true;
                            info.lyrics = g_pendingLyrics;
                            
                            DWORD now = GetTickCount();
                            if (now - g_lastOscSendTime >= OSC_MIN_INTERVAL) {
                                std::wstring oscMsg;
                                
                                // 根据显示模式选择消息类型
                                if (g_performanceMode == 1) {
                                    // 性能模式：一次性发送所有硬件信息
                                    oscMsg = BuildPerformanceOSCMessage(0);
                                } else {
                                    // 音乐模式：发送歌曲信息
                                    oscMsg = FormatOSCMessage(info);
                                }
                                
                                if (oscMsg != g_lastOscMessage) {
                                    g_osc->sendChatbox(oscMsg);
                                    g_lastOscMessage = oscMsg;
                                    g_lastOscSendTime = now;
                                }
                            }
                        }
                    }
                }
                
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_CLOSE:
            // 退出前发送关闭OSC消息
            if (g_osc && g_oscEnabled) {
                SendSystemOSCMessage(L"VRCLyricsDisplay\n\x6B22\x8FCE\x4E0B\x6B21\x4F7F\x7528\x54E6~");
            }
            // Save window position and size before closing
            {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                g_winX = rc.left;
                g_winY = rc.top;
                g_winW = rc.right - rc.left;
                g_winH = rc.bottom - rc.top;
                SaveConfig(g_configPath);
            }
            RemoveTrayIcon();
            DestroyWindow(hwnd);
            return 0;
        
        case WM_USER + 102: {
            // OSC接收器回调 - 切换暂停状态
            DWORD now = GetTickCount();
            
            // 判断是否真的在暂停中（时间还没到）
            bool isReallyPaused = g_oscPaused && g_oscPauseEndTime > now;
            
            if (isReallyPaused || g_overlayHwnd) {
                // 已经在暂停中或窗口存在 - 取消暂停，触发粒子爆发
                MainDebugLog("[OSC Receiver] Canceling OSC pause");
                
                // 发送恢复消息提示
                if (g_osc && g_oscEnabled) {
                    SendSystemOSCMessage(L"OSC \x5DF2\x6062\x590D\x53D1\x9001");
                }
                
                // 触发粒子爆炸（在进度条末端）
                TriggerParticleBurstAtProgressEnd();
                
                g_oscPaused = false;
                g_oscPauseEndTime = 0;
                
                // 如果窗口正在关闭动画，立即销毁；否则开始关闭动画
                if (g_overlayHwnd) {
                    if (g_overlayClosing) {
                        // 正在关闭，立即销毁
                        DestroyWindow(g_overlayHwnd);
                        g_overlayHwnd = nullptr;
                        g_overlayActive = false;
                        g_overlayClosing = false;
                        g_overlayExpandAnim = 0.0f;
                        g_particles.clear();
                        g_sandParticles.clear();
                    } else {
                        // 开始关闭动画
                        g_overlayClosing = true;
                    }
                }
            } else {
                // 开始新的暂停
                g_oscPaused = true;
                g_oscPauseEndTime = now + OSC_PAUSE_DURATION * 1000;
                g_overlayClosing = false;  // 确保不是关闭状态
                MainDebugLog("[OSC Receiver] OSC paused for 30 seconds");
                
                // 发送暂停消息提示
                if (g_osc && g_oscEnabled) {
                    SendSystemOSCMessage(L"\x6B63\x5728\x6682\x505C OSC \x53D1\x9001...\n\x8BF7\x7B49\x5F85 30 \x79D2");
                }
                
                // 创建覆盖层窗口
                CreateOverlayWindow();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        
        case WM_DESTROY:
            // Cleanup performance monitoring threads
            ShutdownPerfMonitoring();

            // Cleanup lyrics search thread
            g_lyricsSearchRunning = false;
            if (g_lyricsSearchThread.joinable()) {
                g_lyricsSearchThread.join();
            }
            
            // 销毁覆盖层窗口
            DestroyOverlayWindow();
            
            // 取消注册热键
            UnregisterHotKey(hwnd, HOTKEY_OSC_PAUSE);
            
            // 移除低级键盘钩子
            if (g_keyboardHook) {
                UnhookWindowsHookEx(g_keyboardHook);
                g_keyboardHook = nullptr;
            }
            
            // 停止OSC接收器
            if (g_oscReceiver) {
                g_oscReceiver->stop();
                delete g_oscReceiver;
                g_oscReceiver = nullptr;
            }
            
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSubtitle) DeleteObject(g_fontSubtitle);
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            if (g_fontLyric) DeleteObject(g_fontLyric);
            if (g_fontLabel) DeleteObject(g_fontLabel);
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushCard) DeleteObject(g_brushCard);
            if (g_brushEditBg) DeleteObject(g_brushEditBg);
            // Cleanup SMTC client
            if (g_smtcClient) {
                g_smtcClient->stop();
                delete g_smtcClient;
                g_smtcClient = nullptr;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// === 异步性能检测线程实现 ===

// 统一性能监控线程（合并原4个线程，优化性能和资源使用）
DWORD WINAPI WorkerThread_PerfMonitor(LPVOID param) {
    LOG_INFO("[PerfMonitor] Unified performance monitoring thread started");
    
    // === 初始化所有监控资源（只初始化一次）===
    
    // 1. DXGI for GPU显存（Windows 10+原生API）
    IDXGIFactory1* pDXGIFactory = nullptr;
    IDXGIAdapter3* pDXGIAdapter = nullptr;  // 使用Adapter3支持QueryVideoMemoryInfo
    bool dxgiInitialized = false;
    
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pDXGIFactory))) {
        // 获取第一个GPU适配器
        IDXGIAdapter1* pAdapter1 = nullptr;
        for (UINT i = 0; pDXGIFactory->EnumAdapters1(i, &pAdapter1) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(pAdapter1->GetDesc1(&desc))) {
                // 跳过软件渲染器（Microsoft Basic Render）
                if (desc.VendorId != 0x1414) {
                    // 尝试转换为Adapter3（Windows 10+）
                    if (SUCCEEDED(pAdapter1->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&pDXGIAdapter))) {
                        dxgiInitialized = true;
                        char msg[128];
                        sprintf_s(msg, "[PerfMonitor] DXGI Adapter3 initialized (Vendor: 0x%04X)", desc.VendorId);
                        LOG_INFO(msg);
                    }
                    pAdapter1->Release();
                    break;  // 使用第一个有效GPU
                }
            }
            pAdapter1->Release();
        }
    }
    
    // 2. WMI for 温度（ROOT\CIMV2 和 ROOT\LibreHardwareMonitor）
    IWbemLocator* pWmiLocator = nullptr;
    IWbemServices* pWmiServices = nullptr;          // ROOT\CIMV2
    IWbemServices* pLhmServices = nullptr;          // ROOT\LibreHardwareMonitor
    bool wmiInitialized = false;
    bool lhmInitialized = false;
    
    if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pWmiLocator))) {
        // ROOT\CIMV2（标准WMI）
        if (SUCCEEDED(pWmiLocator->ConnectServer(BSTR(L"ROOT\\CIMV2"), nullptr, nullptr, 0, 0, 0, 0, &pWmiServices))) {
            CoSetProxyBlanket(pWmiServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, 
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            wmiInitialized = true;
            LOG_INFO("[PerfMonitor] WMI ROOT\\CIMV2 initialized");
        }
        
        // ROOT\LibreHardwareMonitor（需要安装LHM）
        if (SUCCEEDED(pWmiLocator->ConnectServer(BSTR(L"ROOT\\LibreHardwareMonitor"), nullptr, nullptr, 0, 0, 0, 0, &pLhmServices))) {
            CoSetProxyBlanket(pLhmServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, 
                             RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            lhmInitialized = true;
            LOG_INFO("[PerfMonitor] LibreHardwareMonitor WMI initialized");
        }
    }
    
    {
        char initMsg[256];
        sprintf_s(initMsg, "[PerfMonitor] Initialization complete. DXGI: %s, WMI: %s, LHM: %s",
                 dxgiInitialized ? "OK" : "N/A",
                 wmiInitialized ? "OK" : "N/A",
                 lhmInitialized ? "OK" : "N/A");
        LOG_INFO(initMsg);
    }
    
    // === 主监控循环（统一频率：每1秒）===
    while (g_threadRunning[0]) {  // 使用g_threadRunning[0]作为统一退出标志
        // ===== 1. CPU/RAM监控（原生API，最高优先级）=====
        {
            // CPU使用率
            FILETIME idleTime, kernelTime, userTime;
            if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
                ULONGLONG idle = ((ULONGLONG)idleTime.dwHighDateTime << 32) | idleTime.dwLowDateTime;
                ULONGLONG kernel = ((ULONGLONG)kernelTime.dwHighDateTime << 32) | kernelTime.dwLowDateTime;
                ULONGLONG user = ((ULONGLONG)userTime.dwHighDateTime << 32) | userTime.dwLowDateTime;

                ULONGLONG lastIdle = ((ULONGLONG)g_lastIdleTime.dwHighDateTime << 32) | g_lastIdleTime.dwLowDateTime;
                ULONGLONG lastKernel = ((ULONGLONG)g_lastKernelTime.dwHighDateTime << 32) | g_lastKernelTime.dwLowDateTime;
                ULONGLONG lastUser = ((ULONGLONG)g_lastUserTime.dwHighDateTime << 32) | g_lastUserTime.dwLowDateTime;

                if (lastIdle > 0 || lastKernel > 0) {
                    ULONGLONG idleDelta = idle - lastIdle;
                    ULONGLONG totalDelta = (kernel - lastKernel) + (user - lastUser);
                    if (totalDelta > 0) {
                        double cpuUsage = (double)(totalDelta - idleDelta) / totalDelta * 100.0;
                        if (cpuUsage < 0) cpuUsage = 0;
                        if (cpuUsage > 100) cpuUsage = 100;

                        std::lock_guard<std::mutex> lock(g_perfDataMutex);
                        g_latestPerfData.cpuUsage = (int)cpuUsage;
                    }
                }
                g_lastIdleTime = idleTime;
                g_lastKernelTime = kernelTime;
                g_lastUserTime = userTime;
            }
        }
        
        // RAM使用率
        {
            MEMORYSTATUSEX memStatus = {sizeof(memStatus)};
            if (GlobalMemoryStatusEx(&memStatus)) {
                std::lock_guard<std::mutex> lock(g_perfDataMutex);
                g_latestPerfData.ramUsed = memStatus.ullTotalPhys - memStatus.ullAvailPhys;
                g_latestPerfData.ramTotal = memStatus.ullTotalPhys;
                if (memStatus.ullTotalPhys > 0) {
                    g_latestPerfData.ramUsage = (int)((memStatus.ullTotalPhys - memStatus.ullAvailPhys) * 100.0 / memStatus.ullTotalPhys);
                }
            }
        }
        
        // ===== 2. GPU监控（按优先级：NVML > ADL > DXGI > LHM）=====
        {
            int gpuUsage = 0;
            DWORD64 gpuVramUsed = 0;
            DWORD64 gpuVramTotal = 0;
            
            // 优先级1: NVIDIA NVML（同时获取占用率和显存）
            if (g_gpuVendor == GPU_NVIDIA && g_nvmlAvailable && g_nvmlDevice && nvmlDeviceGetUtilizationRates) {
                // 获取GPU占用率
                nvmlUtilization_t utilization = {0, 0};
                int utilResult = nvmlDeviceGetUtilizationRates(g_nvmlDevice, &utilization);
                if (utilResult == NVML_SUCCESS) {
                    gpuUsage = (int)utilization.gpu;
                } else {
                    char errMsg[64];
                    sprintf_s(errMsg, "[PerfMonitor] NVML GetUtilizationRates failed: %d", utilResult);
                    LOG_DEBUG(errMsg);
                }
                
                // 获取显存信息（如果函数可用）
                if (nvmlDeviceGetMemoryInfo) {
                    nvmlMemory_t memory = {0, 0, 0};
                    int memResult = nvmlDeviceGetMemoryInfo(g_nvmlDevice, &memory);
                    if (memResult == NVML_SUCCESS) {
                        gpuVramTotal = memory.total;
                        gpuVramUsed = memory.used;
                        char msg[128];
                        sprintf_s(msg, "[PerfMonitor] NVML: GPU=%d%%, VRAM=%llu/%llu MB", 
                                 gpuUsage, memory.used/1024/1024, memory.total/1024/1024);
                        LOG_DEBUG(msg);
                    } else {
                        char errMsg[64];
                        sprintf_s(errMsg, "[PerfMonitor] NVML GetMemoryInfo failed: %d", memResult);
                        LOG_DEBUG(errMsg);
                    }
                }
            }
            
            // 优先级2: AMD ADL（待实现）
            // TODO: AMD ADL同时获取占用率和显存
            
            // 优先级3: DXGI QueryVideoMemoryInfo（Windows 10+原生API，获取显存）
            if (gpuVramTotal == 0 && dxgiInitialized && pDXGIAdapter) {
                DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
                if (SUCCEEDED(pDXGIAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                    // 获取显存使用情况
                    if (info.Budget > 0) {
                        gpuVramTotal = info.Budget;  // 预算值作为总量
                        gpuVramUsed = info.CurrentUsage;
                        char dxgiMsg[128];
                        sprintf_s(dxgiMsg, "[PerfMonitor] DXGI QueryVideoMemoryInfo: VRAM=%llu/%llu MB", 
                                 gpuVramUsed/1024/1024, gpuVramTotal/1024/1024);
                        LOG_DEBUG(dxgiMsg);
                    }
                }
                
                // 如果没有获取到总量，使用GetDesc1
                if (gpuVramTotal == 0) {
                    DXGI_ADAPTER_DESC1 desc;
                    if (SUCCEEDED(pDXGIAdapter->GetDesc1(&desc))) {
                        gpuVramTotal = desc.DedicatedVideoMemory;
                    }
                }
            }
            
            // 优先级4: nvidia-smi命令行（备用方案）
            if (gpuUsage == 0 && g_gpuVendor == GPU_NVIDIA) {
                STARTUPINFOW si = { sizeof(si)};
                PROCESS_INFORMATION pi = {0};
                SECURITY_ATTRIBUTES sa = { sizeof(sa)};
                HANDLE hReadPipe, hWritePipe;
                
                si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
                SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
                si.hStdOutput = hWritePipe;
                si.hStdError = hWritePipe;
                
                wchar_t cmd[] = L"nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total --format=csv,noheader,nounits";
                
                if (CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                    CloseHandle(hWritePipe);
                    
                    char buffer[512];
                    DWORD bytesRead;
                    std::string output;
                    
                    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                        buffer[bytesRead] = '\0';
                        output += buffer;
                    }
                    
                    CloseHandle(hReadPipe);
                    WaitForSingleObject(pi.hProcess, 2000);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    
                    // 解析输出：格式 "45, 2048, 8192"
                    if (!output.empty()) {
                        int memUsed = 0, memTotal = 0;
                        if (sscanf_s(output.c_str(), "%d, %d, %d", &gpuUsage, &memUsed, &memTotal) >= 1) {
                            if (memTotal > 0) {
                                gpuVramUsed = (DWORD64)memUsed * 1024 * 1024;
                                gpuVramTotal = (DWORD64)memTotal * 1024 * 1024;
                            }
                            char smiMsg[128];
                            sprintf_s(smiMsg, "[PerfMonitor] nvidia-smi: GPU=%d%%, VRAM=%d/%d MB", gpuUsage, memUsed, memTotal);
                            LOG_DEBUG(smiMsg);
                        }
                    }
                }
            }
            
            // 优先级5: LibreHardwareMonitor WMI（最后保底）
            if ((gpuUsage == 0 || gpuVramUsed == 0) && lhmInitialized && pLhmServices) {
                // GPU占用率
                if (gpuUsage == 0) {
                    IEnumWbemClassObject* pEnum = nullptr;
                    HRESULT hr = pLhmServices->ExecQuery(BSTR(L"WQL"), 
                        BSTR(L"SELECT Value FROM Sensor WHERE SensorType='Load' AND Name='GPU Core'"), 
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
                    
                    if (SUCCEEDED(hr) && pEnum) {
                        IWbemClassObject* pObj = nullptr;
                        ULONG uReturn = 0;
                        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
                            VARIANT vt;
                            VariantInit(&vt);
                            if (SUCCEEDED(pObj->Get(L"Value", 0, &vt, 0, 0)) && vt.vt == VT_R4) {
                                gpuUsage = (int)vt.fltVal;
                            }
                            VariantClear(&vt);
                            pObj->Release();
                        }
                        pEnum->Release();
                    }
                }
                
                // 显存使用
                if (gpuVramUsed == 0) {
                    IEnumWbemClassObject* pEnum = nullptr;
                    HRESULT hr = pLhmServices->ExecQuery(BSTR(L"WQL"), 
                        BSTR(L"SELECT Value FROM Sensor WHERE SensorType='Data' AND Name='GPU Memory Used'"), 
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
                    
                    if (SUCCEEDED(hr) && pEnum) {
                        IWbemClassObject* pObj = nullptr;
                        ULONG uReturn = 0;
                        if (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
                            VARIANT vt;
                            VariantInit(&vt);
                            if (SUCCEEDED(pObj->Get(L"Value", 0, &vt, 0, 0)) && vt.vt == VT_R4) {
                                gpuVramUsed = (DWORD64)(vt.fltVal * 1024 * 1024);  // MB -> Bytes
                            }
                            VariantClear(&vt);
                            pObj->Release();
                        }
                        pEnum->Release();
                    }
                }
            }
            
            // 更新共享数据
            {
                std::lock_guard<std::mutex> lock(g_perfDataMutex);
                g_latestPerfData.gpuUsage = gpuUsage;
                g_latestPerfData.gpuUsageValid = (gpuUsage > 0);
                if (gpuVramTotal > 0) g_latestPerfData.gpuVramTotal = gpuVramTotal;
                if (gpuVramUsed > 0) g_latestPerfData.gpuVramUsed = gpuVramUsed;
            }
        }
        
        // ===== 3. 温度监控（WMI + LibreHardwareMonitor）=====
        {
            int cpuTemp = 0;
            
            // 方法1: MSAcpi_ThermalZoneTemperature（部分主板支持）
            if (wmiInitialized && pWmiServices && cpuTemp == 0) {
                IEnumWbemClassObject* pEnum = nullptr;
                HRESULT hr = pWmiServices->ExecQuery(BSTR(L"WQL"), 
                    BSTR(L"SELECT * FROM MSAcpi_ThermalZoneTemperature WHERE Active=True"), 
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
                
                if (SUCCEEDED(hr) && pEnum) {
                    IWbemClassObject* pObj = nullptr;
                    ULONG uReturn = 0;
                    while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
                        VARIANT vt;
                        VariantInit(&vt);
                        if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vt, 0, 0)) && vt.vt == VT_I4) {
                            int tempK = vt.lVal / 10;
                            int tempC = tempK - 273;
                            if (tempC > 0 && tempC < 150) {
                                cpuTemp = tempC;
                                char tempMsg[64];
                                sprintf_s(tempMsg, "[PerfMonitor] ACPI Temperature: %d°C", cpuTemp);
                                LOG_DEBUG(tempMsg);
                                break;
                            }
                        }
                        VariantClear(&vt);
                        pObj->Release();
                    }
                    pEnum->Release();
                }
            }
            
            // 方法2: LibreHardwareMonitor WMI（最可靠）
            if (cpuTemp == 0 && lhmInitialized && pLhmServices) {
                IEnumWbemClassObject* pEnum = nullptr;
                HRESULT hr = pLhmServices->ExecQuery(BSTR(L"WQL"), 
                    BSTR(L"SELECT Value FROM Sensor WHERE SensorType='Temperature' AND (Name='CPU Core' OR Name='CPU Package' OR Name='CPU')"), 
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
                
                if (SUCCEEDED(hr) && pEnum) {
                    IWbemClassObject* pObj = nullptr;
                    ULONG uReturn = 0;
                    while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
                        VARIANT vt;
                        VariantInit(&vt);
                        if (SUCCEEDED(pObj->Get(L"Value", 0, &vt, 0, 0)) && vt.vt == VT_R4) {
                            cpuTemp = (int)vt.fltVal;
                            if (cpuTemp > 0 && cpuTemp < 150) {
                                char tempMsg[64];
                                sprintf_s(tempMsg, "[PerfMonitor] LHM CPU Temperature: %d°C", cpuTemp);
                                LOG_DEBUG(tempMsg);
                                break;
                            }
                        }
                        VariantClear(&vt);
                        pObj->Release();
                    }
                    pEnum->Release();
                }
            }
            
            // 如果没有获取到温度，尝试其他方法
            if (cpuTemp == 0 && lhmInitialized && pLhmServices) {
                // 尝试获取任何CPU相关温度传感器
                IEnumWbemClassObject* pEnum = nullptr;
                HRESULT hr = pLhmServices->ExecQuery(BSTR(L"WQL"), 
                    BSTR(L"SELECT Name, Value FROM Sensor WHERE SensorType='Temperature'"), 
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &pEnum);
                
                if (SUCCEEDED(hr) && pEnum) {
                    IWbemClassObject* pObj = nullptr;
                    ULONG uReturn = 0;
                    while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &uReturn) == S_OK) {
                        VARIANT vtName, vtValue;
                        VariantInit(&vtName);
                        VariantInit(&vtValue);
                        bool found = false;
                        if (SUCCEEDED(pObj->Get(L"Name", 0, &vtName, 0, 0)) && vtName.vt == VT_BSTR &&
                            SUCCEEDED(pObj->Get(L"Value", 0, &vtValue, 0, 0)) && vtValue.vt == VT_R4) {
                            std::wstring name = vtName.bstrVal;
                            // 检查是否是CPU相关传感器
                            if (name.find(L"CPU") != std::wstring::npos || 
                                name.find(L"Core") != std::wstring::npos ||
                                name.find(L"Package") != std::wstring::npos) {
                                int temp = (int)vtValue.fltVal;
                                if (temp > 0 && temp < 150) {
                                    cpuTemp = temp;
                                    char tempMsg[128];
                                    sprintf_s(tempMsg, "[PerfMonitor] LHM Temperature Sensor '%ls': %d°C", name.c_str(), cpuTemp);
                                    LOG_DEBUG(tempMsg);
                                    found = true;
                                }
                            }
                        }
                        VariantClear(&vtName);
                        VariantClear(&vtValue);
                        pObj->Release();
                        if (found) break;
                    }
                    pEnum->Release();
                }
            }
            
            if (cpuTemp == 0) {
                LOG_DEBUG("[PerfMonitor] CPU temperature not available (requires LibreHardwareMonitor)");
            }
            
            // 更新共享数据
            {
                std::lock_guard<std::mutex> lock(g_perfDataMutex);
                g_latestPerfData.cpuTemp = cpuTemp;
                g_latestPerfData.cpuTempValid = (cpuTemp > 0);
            }
        }
        
        Sleep(1000);  // 统一频率：每1秒更新
    }
    
    // === 清理资源 ===
    if (pDXGIAdapter) pDXGIAdapter->Release();
    if (pDXGIFactory) pDXGIFactory->Release();
    if (pLhmServices) pLhmServices->Release();
    if (pWmiServices) pWmiServices->Release();
    if (pWmiLocator) pWmiLocator->Release();
    
    LOG_INFO("[PerfMonitor] Unified performance monitoring thread stopped");
    return 0;
}

// 初始化性能监控
void InitializePerfMonitoring() {
    // 检测GPU供应商
    IDXGIFactory1* pFactory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) {
        UINT adapterIndex = 0;
        IDXGIAdapter1* pAdapter = nullptr;
        while (pFactory->EnumAdapters1(adapterIndex, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                std::wstring vendorName = desc.Description;
                if (vendorName.find(L"NVIDIA") != std::wstring::npos) {
                    g_gpuVendor = GPU_NVIDIA;
                } else if (vendorName.find(L"AMD") != std::wstring::npos || vendorName.find(L"Radeon") != std::wstring::npos) {
                    g_gpuVendor = GPU_AMD;
                } else if (vendorName.find(L"Intel") != std::wstring::npos) {
                    g_gpuVendor = GPU_INTEL;
                }
                pAdapter->Release();
                break;
            }
            pAdapter->Release();
            adapterIndex++;
        }
        pFactory->Release();
    }

    // 加载NVML库（NVIDIA）
    if (g_gpuVendor == GPU_NVIDIA) {
        g_nvmlDll = LoadLibraryW(L"nvml.dll");
        if (g_nvmlDll) {
            LOG_INFO("[NVML] nvml.dll loaded successfully");
            
            // 尝试加载 _v2 版本的函数，失败则回退到旧版本
            nvmlInit = (nvmlInit_t)GetProcAddress(g_nvmlDll, "nvmlInit_v2");
            if (!nvmlInit) {
                nvmlInit = (nvmlInit_t)GetProcAddress(g_nvmlDll, "nvmlInit");
                LOG_INFO("[NVML] Using legacy nvmlInit");
            } else {
                LOG_INFO("[NVML] Using nvmlInit_v2");
            }
            
            nvmlShutdown = (nvmlShutdown_t)GetProcAddress(g_nvmlDll, "nvmlShutdown");
            
            nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(g_nvmlDll, "nvmlDeviceGetHandleByIndex_v2");
            if (!nvmlDeviceGetHandleByIndex) {
                nvmlDeviceGetHandleByIndex = (nvmlDeviceGetHandleByIndex_t)GetProcAddress(g_nvmlDll, "nvmlDeviceGetHandleByIndex");
                LOG_INFO("[NVML] Using legacy nvmlDeviceGetHandleByIndex");
            } else {
                LOG_INFO("[NVML] Using nvmlDeviceGetHandleByIndex_v2");
            }
            
            nvmlDeviceGetUtilizationRates = (nvmlDeviceGetUtilizationRates_t)GetProcAddress(g_nvmlDll, "nvmlDeviceGetUtilizationRates");
            nvmlDeviceGetMemoryInfo = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(g_nvmlDll, "nvmlDeviceGetMemoryInfo");
            nvmlDeviceGetName = (nvmlDeviceGetName_t)GetProcAddress(g_nvmlDll, "nvmlDeviceGetName");

            if (nvmlInit && nvmlDeviceGetHandleByIndex && nvmlDeviceGetUtilizationRates) {
                int initResult = nvmlInit();
                char initMsg[64];
                sprintf_s(initMsg, "[NVML] nvmlInit returned: %d", initResult);
                LOG_INFO(initMsg);
                
                if (initResult == NVML_SUCCESS) {
                    int handleResult = nvmlDeviceGetHandleByIndex(0, &g_nvmlDevice);
                    char handleMsg[64];
                    sprintf_s(handleMsg, "[NVML] GetHandleByIndex(0) returned: %d", handleResult);
                    LOG_INFO(handleMsg);
                    
                    if (handleResult == NVML_SUCCESS) {
                        g_nvmlAvailable = true;
                        LOG_INFO("[NVML] NVML initialized successfully");
                        
                        // 测试获取GPU名称
                        if (nvmlDeviceGetName) {
                            char gpuName[256] = {0};
                            if (nvmlDeviceGetName(g_nvmlDevice, gpuName, sizeof(gpuName)) == NVML_SUCCESS) {
                                char nameMsg[300];
                                sprintf_s(nameMsg, "[NVML] GPU Name: %s", gpuName);
                                LOG_INFO(nameMsg);
                            }
                        }
                    } else {
                        g_nvmlDevice = nullptr;
                        nvmlShutdown();
                        LOG_ERROR("[NVML] Failed to get device handle");
                    }
                } else {
                    LOG_ERROR("[NVML] nvmlInit failed");
                }
            } else {
                LOG_ERROR("[NVML] Required functions not found in nvml.dll");
            }
            
            if (!g_nvmlAvailable) {
                FreeLibrary(g_nvmlDll);
                g_nvmlDll = nullptr;
                LOG_ERROR("[NVML] NVML initialization failed, falling back to nvidia-smi");
            }
        } else {
            LOG_ERROR("[NVML] Failed to load nvml.dll");
        }
    }

    // 加载ADL库（AMD）
    if (g_gpuVendor == GPU_AMD) {
        g_adlDll = LoadLibraryW(L"atiadlxx.dll");
        if (g_adlDll) {
            ADL_Main_Control_Create = (ADL_Main_Control_Create_t)GetProcAddress(g_adlDll, "ADL_Main_Control_Create");
            ADL_Main_Control_Destroy = (ADL_Main_Control_Destroy_t)GetProcAddress(g_adlDll, "ADL_Main_Control_Destroy");
            ADL_Adapter_NumberOfAdapters_Get = (ADL_Adapter_NumberOfAdapters_Get_t)GetProcAddress(g_adlDll, "ADL_Adapter_NumberOfAdapters_Get");
            ADL_Adapter_AdapterInfo_Get = (ADL_Adapter_AdapterInfo_Get_t)GetProcAddress(g_adlDll, "ADL_Adapter_AdapterInfo_Get");
            ADL_Overdrive5_CurrentActivity_Get = (ADL_Overdrive5_CurrentActivity_Get_t)GetProcAddress(g_adlDll, "ADL_Overdrive5_CurrentActivity_Get");

            if (ADL_Main_Control_Create && ADL_Adapter_NumberOfAdapters_Get) {
                if (ADL_Main_Control_Create(nullptr, 1) == 0) {
                    g_adlAvailable = true;
                }
            }
        }
    }

    // 初始化COM（用于WMI）
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // 启动工作线程
    g_threadRunning[0] = true;
    g_workerThreads[0] = CreateThread(nullptr, 0, WorkerThread_PerfMonitor, nullptr, 0, nullptr);
}

// 关闭性能监控
void ShutdownPerfMonitoring() {
    // 停止线程
    g_threadRunning[0] = false;

    // 等待线程结束
    if (g_workerThreads[0]) {
        WaitForSingleObject(g_workerThreads[0], 1000);
        CloseHandle(g_workerThreads[0]);
        g_workerThreads[0] = nullptr;
    }

    // 清理NVML
    if (g_nvmlAvailable && nvmlShutdown) {
        nvmlShutdown();
    }
    if (g_nvmlDll) {
        FreeLibrary(g_nvmlDll);
        g_nvmlDll = nullptr;
    }

    // 清理ADL
    if (g_adlAvailable && ADL_Main_Control_Destroy) {
        ADL_Main_Control_Destroy();
    }
    if (g_adlDll) {
        FreeLibrary(g_adlDll);
        g_adlDll = nullptr;
    }

    // 清理COM
    CoUninitialize();
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Initialize GDI+ for anti-aliased rendering
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    
    // Enable DPI awareness for high-resolution displays
    typedef BOOL(WINAPI* SetProcessDPIAware_t)();
    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        SetProcessDPIAware_t SetProcessDPIAware = (SetProcessDPIAware_t)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (SetProcessDPIAware) SetProcessDPIAware();
        FreeLibrary(hUser32);
    }
    
    MainDebugLog("[WinMain] Application starting");
    InitCommonControls();
    
    g_mutex = CreateMutexW(nullptr, TRUE, L"VRCLyricsDisplay_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ShowInfoDialog(L"VRChat Lyrics Display", L"\x7A0B\x5E8F\x5DF2\x5728\x8FD0\x884C!");
        return 0;
    }
    
    InitializeCriticalSection(&g_cs);
    g_startTime = GetTickCount();

    // 获取程序所在目录，设置配置文件绝对路径
    GetModuleFileNameW(nullptr, g_configPath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(g_configPath, L'\\');
    if (lastSlash) {
        wcscpy(lastSlash + 1, L"config_gui.json");
        wcscpy(g_noLyricConfigPath, g_configPath);
        wcscpy(wcsrchr(g_noLyricConfigPath, L'\\') + 1, L"config.json");
    }

    // 初始化异步性能监控
    InitializePerfMonitoring();

    // 先初始化默认主题颜色，确保弹窗显示正确
    UpdateThemeColors();
    
    // Check if this is first run (config file doesn't exist)
    bool isFirstRun = (_waccess(g_configPath, 0) == -1);
    if (isFirstRun) {
        g_minimizeToTray = ShowFirstRunDialog();
    }
    
    LoadConfig(g_configPath);
    UpdateThemeColors();  // 根据配置重新加载主题颜色
    
    // 检查是否需要以管理员身份重启
    if (g_runAsAdmin && !IsRunningAsAdmin()) {
        MainDebugLog("[WinMain] Configured to run as admin, restarting...");
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"runas";
        sei.lpFile = exePath;
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            ReleaseMutex(g_mutex);
            CloseHandle(g_mutex);
            GdiplusShutdown(gdiplusToken);
            return 0;
        }
        // 如果用户取消了UAC提示，继续以普通权限运行
        MainDebugLog("[WinMain] Admin restart cancelled by user, continuing with normal privileges");
    }
    
    g_noLyricMsgs = LoadNoLyricMessages(g_noLyricConfigPath);
    if (g_noLyricMsgs.empty()) {
        g_noLyricMsgs = {
            L"~ \x6CA1\x6709\x6B4C\x8BCD ~",
            L"~ \x7EAF\x97F3\x4E50 ~",
            L"~ \x8DDF\x7740\x54FC\x5531\x5427 ~",
            L"~ \x611F\x53D7\x8282\x594F ~",
            L"~ \x4EAB\x53D7\x97F3\x4E50 ~",
            L"~ \x97F3\x4E50\x662F\x6700\x597D\x7684\x4F34\x4FA3 ~",
            L"~ \x7528\x5FC3\x542C\xFF0C\x4E0D\x7528\x8033\x6735 ~",
            L"~ \x6BCF\x4E2A\x97F3\x7B26\x90FD\x5728\x8BB2\x6545\x4E8B ~",
            L"~ \x97F3\x4E50\xFF0C\x5FC3\x7075\x7684\x6CBB\x6108 ~",
            L"~ \x8282\x594F\x662F\x5FC3\x8DF3\x7684\x56DE\x58F0 ~",
            L"~ \x6B4C\x58F0\x505C\x4E86\xFF0C\x5FC3\x4ECD\x5728\x98DE\x7FD4 ~",
            L"~ \x97F3\x4E50\x65E0\x5904\x4E0D\x5728 ~",
            L"~ \x6CA1\x6709\x6B4C\x8BCD\xFF0C\x6709\x611F\x89C9\x5C31\x597D ~",
            L"~ \x6C89\x6D78\x5728\x65CB\x5F8B\x4E2D ~",
            L"~ \x8BA9\x97F3\x4E50\x5E26\x4F60\x8D70\x8FDC ~",
            L"~ \x6BCF\x4E2A\x97F3\x7B26\x90FD\x662F\x4E00\x4E2A\x68A6 ~",
            L"~ \x7528\x97F3\x4E50\x70B9\x4EAE\x5FC3\x7075 ~",
            L"~ \x6B4C\x66F2\x662F\x65F6\x5149\x7684\x8BB0\x5FC6 ~",
            L"~ \x97F3\x4E50\x662F\x60C5\x611F\x7684\x6CE2\x52A8 ~",
            L"~ \x6BCF\x4E00\x62D4\x90FD\x662F\x7075\x9B42\x7684\x8212\x5C55 ~",
            L"~ \x97F3\x4E50\xFF0C\x8BA9\x65F6\x5149\x505C\x7559 ~",
            L"~ \x8DDF\x7740\x65CB\x5F8B\xFF0C\x81EA\x7531\x7FFB\x7FFD ~",
            L"~ \x97F3\x4E50\x662F\x7075\x9B42\x7684\x7A97\x53E3 ~",
            L"~ \x5728\x97F3\x4E50\x4E2D\x627E\x5230\x81EA\x5DF1 ~",
            L"~ \x6B4C\x58F0\x662F\x5FC3\x7075\x7684\x547C\x5524 ~",
            L"~ \x97F3\x4E50\xFF0C\x751F\x6D3B\x7684\x8C03\x5473 ~",
            L"~ \x7528\x8033\x6735\x62E5\x62B1\x4E16\x754C ~",
            L"~ \x6BCF\x4E2A\x97F3\x7B26\x90FD\x5728\x5FAE\x7B11 ~",
            L"~ \x97F3\x4E50\x662F\x65E0\x58F0\x7684\x8BED\x8A00 ~",
            L"~ \x8BA9\x8282\x594F\x6D41\x6DF4\x5FC3\x7530 ~",
            L"~ \x6B4C\x66F2\x662F\x5FC3\x7075\x7684\x7B14\x8BED ~",
            L"~ \x97F3\x4E50\xFF0C\x6E38\x7984\x4E16\x754C\x7684\x7968 ~",
            L"~ \x7528\x65CB\x5F8B\x7ED8\x5236\x4EBA\x751F ~",
            L"~ \x6BCF\x6B21\x65CB\x5F8B\x53D8\x5316\xFF0C\x90FD\x662F\x6545\x4E8B ~",
            L"~ \x97F3\x4E50\x662F\x7075\x9B42\x7684\x8212\x7F13 ~",
            L"~ \x8BA9\x6B4C\x58F0\x6E29\x6696\x5FC3\x623F ~",
            L"~ \x97F3\x4E50\xFF0C\x70B9\x71C3\x5FC3\x7075\x7684\x706B\x7130 ~",
            L"~ \x7528\x97F3\x7B26\x7F16\x7EC7\x68A6\x60F3 ~",
            L"~ \x6BCF\x6BB5\x65CB\x5F8B\xFF0C\x90FD\x662F\x56DE\x5FC6 ~",
            L"~ \x97F3\x4E50\x662F\x5E78\x798F\x7684\x6E90\x6CC9 ~",
            L"~ \x8DDF\x7740\x97F3\x4E50\xFF0C\x53BB\x8FDC\x65B9 ~",
            L"~ \x6B4C\x66F2\x662F\x7075\x9B42\x7684\x8F66\x7968 ~",
            L"~ \x97F3\x4E50\xFF0C\x8BA9\x5FC3\x7075\x81EA\x7531\x98DE\x7FD4 ~",
            L"~ \x7528\x8282\x594F\x8BB2\x8FF0\x4EBA\x751F ~",
            L"~ \x6BCF\x4E2A\x6B4C\x8BCD\x90FD\x662F\x4E00\x79CD\x5FC3\x60C5 ~",
            L"~ \x97F3\x4E50\x662F\x65F6\x7A7A\x7684\x7A7F\x68AD ~",
            L"~ \x8BA9\x65CB\x5F8B\x8F7B\x62C2\x5FC3\x5934 ~",
            L"~ \x6B4C\x58F0\x6D88\x5931\xFF0C\x611F\x52A8\x7559\x5B58 ~",
            L"~ \x97F3\x4E50\xFF0C\x7075\x9B42\x7684\x547C\x5438 ~",
        };
    }
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = NULL;  // 背景由OnPaint完全处理
    wc.lpszClassName = L"VRCLyricsDisplay_Class";
    RegisterClassExW(&wc);
    
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_COMPOSITED, L"VRCLyricsDisplay_Class", L"",
        WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, g_winW, g_winH, nullptr, nullptr, hInst, nullptr);
    
    // Set window position: saved position or center on first run
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    
    if (g_winX >= 0 && g_winY >= 0) {
        // Restore saved position if still on screen
        if (g_winX < screenW - 100 && g_winY < screenH - 100 && g_winX > -g_winW + 100 && g_winY > -100) {
            SetWindowPos(g_hwnd, nullptr, g_winX, g_winY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
    } else {
        // First run: center window on screen
        int centerX = (screenW - g_winW) / 2;
        int centerY = (screenH - g_winH) / 2;
        SetWindowPos(g_hwnd, nullptr, centerX, centerY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    
    // Set window theme
    UpdateWindowSystemTheme(g_hwnd);
    
    // 初始化启动动画
    g_startupAnimStart = GetTickCount();
    g_windowFadeAnim.value = 0.0;
    g_windowFadeAnim.target = 1.0;
    g_windowFadeAnim.speed = 0.2;  // 加快淡入速度
    g_windowScaleAnim.value = 0.96;
    g_windowScaleAnim.target = 1.0;
    g_windowScaleAnim.speed = 0.25;  // 加快缩放速度
    g_themeTransition.setImmediate(1.0);
    
    // Support start minimized option
    if (g_startMinimized) {
        ShowWindow(g_hwnd, SW_HIDE);
    } else {
        ShowWindow(g_hwnd, nCmdShow);
    }
    UpdateWindow(g_hwnd);
    
    // 确保毛玻璃效果生效
    EnableBlurBehind(g_hwnd);
    
    g_osc = new moekoe::OSCSender(WstringToUtf8(g_oscIp), g_oscPort);
    Connect();
    SetTimer(g_hwnd, 1, 16, nullptr);
    
    // 启动时发送测试消息，确保OSC连接正常
    if (g_osc && g_oscEnabled) {
        SendSystemOSCMessage(L"VRCLyricsDisplay\n\x5DF2\x542F\x52A8\x5E76\x8FDE\x63A5\x6210\x529F");
        MainDebugLog("[Main] Startup test message sent");
    }
    
    // Auto-check for updates on startup if enabled (in background thread)
    if (g_autoUpdate) {
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            Sleep(2000);  // Wait 2 seconds before checking
            CheckForUpdate();
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
            return 0;
        }, nullptr, 0, nullptr);
    }
    
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    
    if (g_moeKoeClient) delete g_moeKoeClient;
    if (g_neteaseClient) delete g_neteaseClient;
    if (g_osc) delete g_osc;
    DeleteCriticalSection(&g_cs);
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
    }
    
    // Shutdown GDI+
    GdiplusShutdown(gdiplusToken);
    
    return (int)msg.wParam;
}























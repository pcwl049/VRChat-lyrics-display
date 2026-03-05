// MoeKoeMusic VRChat OSC - Modern Settings GUI (Borderless + Animations)
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_IE 0x0600

// Version info
#define APP_VERSION "1.0.0"
#define APP_VERSION_NUM 10000
#define GITHUB_REPO "pcwl049/MoeKoeOSC"
#define GITHUB_API_URL "https://api.github.com/repos/pcwl049/MoeKoeOSC/releases/latest"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <random>
#include <ctime>
#include <cstdio>
#include <cmath>
#include "moekoe_ws.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

// Colors
#define COLOR_BG RGB(18, 18, 24)
#define COLOR_CARD RGB(30, 30, 42)
#define COLOR_ACCENT RGB(88, 166, 255)
#define COLOR_TEXT RGB(245, 245, 250)
#define COLOR_TEXT_DIM RGB(160, 160, 180)
#define COLOR_BORDER RGB(55, 55, 75)
#define COLOR_TITLEBAR RGB(22, 22, 30)
#define COLOR_EDIT_BG RGB(42, 42, 58)

// Tray icon
#define TRAY_ICON_ID 1
#define WM_TRAYICON (WM_USER + 200)

// OSC rate limits
const DWORD OSC_MIN_INTERVAL = 4000;
const DWORD OSC_PAUSE_INTERVAL = 3000;

// Window size
const int WIN_W = 520;
const int WIN_H = 600;
const int TITLEBAR_H = 50;
const int CARD_PADDING = 20;

// Animation helper
struct Animation {
    double value = 0.0, target = 0.0, speed = 0.15;
    void update() { value += (target - value) * speed; if (fabs(target - value) < 0.001) value = target; }
    void setTarget(double t) { target = t; }
};

// Global state
CRITICAL_SECTION g_cs;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
moekoe::OSCSender* g_osc = nullptr;
moekoe::MoeKoeWS* g_client = nullptr;

// Edit controls
HWND g_hEditIp = nullptr;
HWND g_hEditPort = nullptr;
HWND g_hEditMoekoePort = nullptr;

// Fonts
HFONT g_fontTitle = nullptr;
HFONT g_fontSubtitle = nullptr;
HFONT g_fontNormal = nullptr;
HFONT g_fontSmall = nullptr;
HFONT g_fontLyric = nullptr;
HFONT g_fontLabel = nullptr;

// Brushes
HBRUSH g_brushBg = nullptr;
HBRUSH g_brushCard = nullptr;
HBRUSH g_brushEditBg = nullptr;

// Dragging
bool g_dragging = false;
POINT g_dragStart = {0, 0};

// Tab control
int g_currentTab = 0;  // 0=MoeKoe, 1=Netease, 2=QQ, 3=Settings
HWND g_hTabCtrl = nullptr;
const wchar_t* g_tabNames[] = { L"MoeKoe", L"\x7F51\x6613\x4E91", L"QQ", L"\x8BBE\x7F6E" };
const int g_tabCount = 4;
const int TAB_H = 36;

// Update checking
std::wstring g_latestVersion = L"";
std::wstring g_downloadUrl = L"";
bool g_updateAvailable = false;
bool g_checkingUpdate = false;
bool g_downloadingUpdate = false;
DWORD g_lastUpdateCheck = 0;
const DWORD UPDATE_CHECK_INTERVAL = 3600000;  // 1 hour

// Parse version string like "1.2.3" to number
int ParseVersion(const std::wstring& ver) {
    int major = 0, minor = 0, patch = 0;
    swscanf_s(ver.c_str(), L"%d.%d.%d", &major, &minor, &patch);
    return major * 10000 + minor * 100 + patch;
}

// Check for updates from GitHub
bool CheckForUpdate() {
    if (g_checkingUpdate) return false;
    g_checkingUpdate = true;
    
    HINTERNET hSession = WinHttpOpen(L"MoeKoeOSC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { g_checkingUpdate = false; return false; }
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); g_checkingUpdate = false; return false; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/repos/pcwl049/MoeKoeOSC/releases/latest", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); g_checkingUpdate = false; return false; }
    
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
                g_updateAvailable = (latestVer > APP_VERSION_NUM);
            }
        }
        
        // Find download URL for .exe
        size_t assetsPos = response.find("\"assets\"");
        if (assetsPos != std::string::npos) {
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
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    g_checkingUpdate = false;
    g_lastUpdateCheck = GetTickCount();
    return true;
}

// Button animations
Animation g_btnConnectAnim, g_btnApplyAnim, g_btnCloseAnim, g_btnMinAnim;
bool g_btnConnectHover = false, g_btnApplyHover = false;
bool g_btnCloseHover = false, g_btnMinHover = false;

// Song data
std::wstring g_pendingTitle, g_pendingArtist, g_pendingTime;
double g_pendingProgress = 0;
double g_pendingCurrentTime = 0;
std::vector<moekoe::LyricLine> g_pendingLyrics;
DWORD g_lastOscSendTime = 0;
std::wstring g_lastOscMessage;
bool g_lastIsPlaying = false;
bool g_playStateChanged = false;

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

// Music quotes - max 6 Chinese chars (18 bytes) to fit 144 limit
const wchar_t* g_musicQuotes[] = {
    L"\x97F3\x4E50\x662F\x7075\x9B42\x7684\x8BED\x8A00",  // “Ù¿÷ «¡ÈªÍ”Ô—‘
    L"\x6CA1\x97F3\x4E50\xFF0C\x65E0\x8DA3",              // √ª“Ù¿÷£¨Œﬁ»§
    L"\x97F3\x4E50\x70B9\x4EAE\x5FC3\x7075",              // “Ù¿÷µ„¡¡–ƒ¡È
    L"\x7EAF\x97F3\x4E50\xFF0C\x54FC\x5531",              // ¥ø“Ù¿÷£¨∫ﬂ≥™
    L"\x611F\x53D7\x8282\x594F\x7684\x7F8E",              // ∏– ‹Ω⁄◊‡µƒ√¿
};
const int g_numQuotes = 5;

// Performance tracking
FILETIME g_lastIdleTime = {0}, g_lastKernelTime = {0}, g_lastUserTime = {0};

// Config
std::wstring g_oscIp = L"127.0.0.1";
int g_oscPort = 9000;
int g_moekoePort = 6520;
bool g_oscEnabled = true;
bool g_minimizeToTray = true;
bool g_showPerfOnPause = true;  // Show performance stats when paused
bool g_isConnected = false;
std::vector<std::wstring> g_noLyricMsgs;
int g_lastNoLyricIdx = -1;

// Last displayed values (for reducing redraws)
std::wstring g_lastDisplayTitle, g_lastDisplayArtist, g_lastDisplayLyric;
double g_lastDisplayProgress = -1;

std::wstring TruncateStr(const std::wstring& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
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
    // === System CPU Usage ===
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
                g_sysCpuUsage = (double)(totalDelta - idleDelta) / totalDelta * 100.0;
                if (g_sysCpuUsage < 0) g_sysCpuUsage = 0;
                if (g_sysCpuUsage > 100) g_sysCpuUsage = 100;
            }
        }
        g_lastIdleTime = idleTime;
        g_lastKernelTime = kernelTime;
        g_lastUserTime = userTime;
    }
    
    // === System Memory ===
    MEMORYSTATUSEX memStatus = {sizeof(memStatus)};
    if (GlobalMemoryStatusEx(&memStatus)) {
        g_sysMemTotal = (SIZE_T)(memStatus.ullTotalPhys / 1024);
        g_sysMemUsed = (SIZE_T)((memStatus.ullTotalPhys - memStatus.ullAvailPhys) / 1024);
    }
    
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

std::string WstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
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
    g_showPerfOnPause = getBool("show_perf_on_pause", true);
}

void SaveConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fprintf(f, "{\n  \"osc\": {\n    \"ip\": \"%ls\",\n    \"port\": %d\n  },\n", g_oscIp.c_str(), g_oscPort);
    fprintf(f, "  \"moekoe_port\": %d,\n  \"osc_enabled\": %s,\n  \"minimize_to_tray\": %s,\n  \"show_perf_on_pause\": %s\n}\n", 
            g_moekoePort, g_oscEnabled ? "true" : "false", g_minimizeToTray ? "true" : "false", 
            g_showPerfOnPause ? "true" : "false");
    fclose(f);
}

std::wstring FormatOSCMessage(const moekoe::SongInfo& info) {
    std::wstring msg;
    if (!info.hasData || info.title.empty()) return L"\x7B49\x5F85\x97F3\x4E50...";
    
    const size_t MAX_MSG_LEN = 144;
    
    int currentLyricIdx = -1;
    if (!info.lyrics.empty()) {
        for (int i = (int)info.lyrics.size() - 1; i >= 0; i--) {
            if (info.currentTime >= info.lyrics[i].startTime / 1000.0) { currentLyricIdx = i; break; }
        }
    }
    
    if (info.isPlaying) {
        // Line 1: Song title - artist (compact)
        msg = L"\x266B ";
        std::wstring songInfo = TruncateStr(info.title, 15);
        if (!info.artist.empty()) { songInfo += L"-"; songInfo += TruncateStr(info.artist, 10); }
        msg += TruncateStr(songInfo, 26);
        
        // Line 2: Progress bar (compact)
        if (info.duration > 0) {
            msg += L"\n";
            msg += BuildProgressBar(info.currentTime / info.duration, 8);
            msg += L" " + FormatTime(info.currentTime) + L"/" + FormatTime(info.duration);
        }
        
        // Line 3: Lyrics - dynamically truncate to fit remaining space
        if (currentLyricIdx >= 0 && currentLyricIdx < (int)info.lyrics.size()) {
            std::wstring lyric = info.lyrics[currentLyricIdx].text;
            // Calculate remaining space (reserve 3 for "..." if needed)
            size_t remaining = MAX_MSG_LEN - msg.length() - 3; // -3 for "\n‚ñ?"
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
        // Paused state - 7 lines of rich info
        // Line 1: Song title - artist
        // Ultra-compact pause format for 144 byte limit
        // Line 1: Title only (no artist to save space)
        msg = L"\x23F8 " + TruncateStr(info.title, 10);
        
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
            if (g_batteryPercent >= 0) {
                swprintf_s(buf3, L"\nC:%.0f%% R:%.0fG B:%d%%", g_sysCpuUsage, g_sysMemUsed/1024.0/1024.0, g_batteryPercent);
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
            // Simple mode - just progress and count
            msg += L"\n" + BuildProgressBar(info.duration > 0 ? info.currentTime / info.duration : 0, 8);
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

void QueueUpdate(const moekoe::SongInfo& info) {
    EnterCriticalSection(&g_cs);
    UpdatePerfStats();
    
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
    
    if (g_osc && info.hasData && g_oscEnabled) {
        DWORD now = GetTickCount();
        DWORD minInterval = g_playStateChanged ? 500 : (!info.isPlaying ? OSC_PAUSE_INTERVAL : OSC_MIN_INTERVAL);
        if (now - g_lastOscSendTime >= minInterval) {
            std::wstring oscMsg = FormatOSCMessage(info);
            if (oscMsg != g_lastOscMessage || g_playStateChanged) {
                g_osc->sendChatbox(oscMsg);
                g_lastOscMessage = oscMsg;
                g_lastOscSendTime = now;
                g_playStateChanged = false;
            }
        }
    }
    
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    LeaveCriticalSection(&g_cs);
}

void CreateTrayIcon(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"MoeKoe OSC");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() { Shell_NotifyIconW(NIM_DELETE, &g_nid); }
void UpdateTrayTip(const wchar_t* text) { wcscpy_s(g_nid.szTip, text); Shell_NotifyIconW(NIM_MODIFY, &g_nid); }

void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH brush = CreateSolidBrush(color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    RoundRect(hdc, x, y, x + w, y + h, radius * 2, radius * 2);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
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

void DrawButtonAnim(HDC hdc, int x, int y, int w, int h, const wchar_t* text, Animation& anim, bool accent = false) {
    double hover = anim.value;
    COLORREF baseColor = accent ? COLOR_ACCENT : RGB(40, 40, 55);
    COLORREF hoverColor = accent ? RGB(110, 190, 255) : RGB(55, 55, 75);
    
    int r = (int)(GetRValue(baseColor) + (GetRValue(hoverColor) - GetRValue(baseColor)) * hover);
    int g = (int)(GetGValue(baseColor) + (GetGValue(hoverColor) - GetGValue(baseColor)) * hover);
    int b = (int)(GetBValue(baseColor) + (GetBValue(hoverColor) - GetBValue(baseColor)) * hover);
    
    if (hover > 0.1) {
        HPEN glowPen = CreatePen(PS_SOLID, 2, RGB(min(255, 88+50), min(255, 166+50), 255));
        HPEN oldPen = (HPEN)SelectObject(hdc, glowPen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, x - 2, y - 2, x + w + 2, y + h + 2, 20, 20);
        SelectObject(hdc, oldPen);
        DeleteObject(glowPen);
    }
    
    DrawRoundRect(hdc, x, y, w, h, 8, RGB(r, g, b));
    HPEN borderPen = CreatePen(PS_SOLID, 1, accent ? COLOR_ACCENT : COLOR_BORDER);
    HPEN oldPen = (HPEN)SelectObject(hdc, borderPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, x, y, x + w, y + h, 16, 16);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
    
    DrawTextCentered(hdc, text, x + w/2, y + (h - 18)/2, accent ? RGB(0,0,0) : COLOR_TEXT, g_fontNormal);
}

void DrawTitleBarButton(HDC hdc, int x, int y, int size, const wchar_t* symbol, Animation& anim) {
    double hover = anim.value;
    int bgColor = (int)(35 + 25 * hover);
    DrawRoundRect(hdc, x, y, size, size, 6, RGB(bgColor, bgColor, bgColor + 10));
    DrawTextCentered(hdc, symbol, x + size/2, y + (size - 18)/2, hover > 0.5 ? COLOR_TEXT : COLOR_TEXT_DIM, g_fontNormal);
}

void DrawProgressBar(HDC hdc, int x, int y, int w, int h, double progress) {
    DrawRoundRect(hdc, x, y, w, h, h/2, RGB(40, 40, 55));
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

void DrawTabs(HDC hdc, int w) {
    int tabW = 80;
    int startX = 15;
    int y = TITLEBAR_H + 5;
    
    for (int i = 0; i < g_tabCount; i++) {
        int x = startX + i * (tabW + 5);
        bool isActive = (i == g_currentTab);
        
        // Tab background
        COLORREF bgColor = isActive ? COLOR_CARD : RGB(25, 25, 35);
        DrawRoundRect(hdc, x, y, tabW, TAB_H, 8, bgColor);
        
        // Tab text
        COLORREF textColor = isActive ? COLOR_TEXT : COLOR_TEXT_DIM;
        DrawTextCentered(hdc, g_tabNames[i], x + tabW/2, y + 8, textColor, g_fontNormal);
        
        // Active indicator
        if (isActive) {
            DrawRoundRect(hdc, x, y, tabW, 3, 0, COLOR_ACCENT);
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
    
    // Background gradient
    for (int y = 0; y < h; y++) {
        int gray = (int)(18 + 6 * (double)y / h);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(gray, gray, gray + 6));
        MoveToEx(memDC, 0, y, nullptr); LineTo(memDC, w, y);
        DeleteObject(pen);
    }
    
    // Title Bar
    DrawRoundRect(memDC, 0, 0, w, TITLEBAR_H + 5, 0, COLOR_TITLEBAR);
    DrawTextLeft(memDC, L"\x266B \x97F3\x4E50\x663E\x793A", 20, 14, COLOR_TEXT, g_fontSubtitle);
    
    // Version display
    wchar_t verBuf[32];
    swprintf_s(verBuf, L"v" APP_VERSION);
    DrawTextLeft(memDC, verBuf, 180, 18, COLOR_TEXT_DIM, g_fontSmall);
    
    // Update indicator (show if update available)
    if (g_updateAvailable && !g_latestVersion.empty()) {
        wchar_t updateBuf[48];
        swprintf_s(updateBuf, L"\x65B0\x7248 v%ls \x53EF\x7528", g_latestVersion.c_str());
        DrawTextLeft(memDC, updateBuf, 230, 18, RGB(80, 200, 120), g_fontSmall);
    } else if (g_checkingUpdate) {
        DrawTextLeft(memDC, L"\x68C0\x67E5\x4E2D...", 230, 18, COLOR_TEXT_DIM, g_fontSmall);
    }
    
    DrawTitleBarButton(memDC, w - 95, 10, 32, L"\x2500", g_btnMinAnim);
    DrawTitleBarButton(memDC, w - 52, 10, 32, L"\x2715", g_btnCloseAnim);
    
    // Draw tabs
    DrawTabs(memDC, w);
    
    // === Settings Card ===
    int cardY = TITLEBAR_H + TAB_H + 20;
    int cardH = 240;
    DrawRoundRect(memDC, CARD_PADDING, cardY, w - CARD_PADDING * 2, cardH, 16, COLOR_CARD);
    DrawRoundRect(memDC, CARD_PADDING, cardY, 5, 30, 2, COLOR_ACCENT);
    DrawTextLeft(memDC, L"\x8BBE\x7F6E", CARD_PADDING + 15, cardY + 8, COLOR_TEXT, g_fontNormal);
    
    // === Row 1: OSC IP ===
    int labelY = cardY + 50;
    int inputY = labelY + 24;
    
    // OSC IP label and input
    DrawTextLeft(memDC, L"OSC IP:", CARD_PADDING + 15, labelY, COLOR_TEXT, g_fontLabel);
    DrawRoundRect(memDC, CARD_PADDING + 15, inputY, 200, 32, 6, COLOR_EDIT_BG);
    
    // === Row 1: Port ===
    DrawTextLeft(memDC, L"\x7AEF\x53E3:", CARD_PADDING + 240, labelY, COLOR_TEXT, g_fontLabel);
    DrawRoundRect(memDC, CARD_PADDING + 240, inputY, 90, 32, 6, COLOR_EDIT_BG);
    
    // === Row 2: MoeKoe Port ===
    int row2LabelY = inputY + 48;
    int row2InputY = row2LabelY + 24;
    
    DrawTextLeft(memDC, L"MoeKoe \x7AEF\x53E3:", CARD_PADDING + 15, row2LabelY, COLOR_TEXT, g_fontLabel);
    DrawRoundRect(memDC, CARD_PADDING + 15, row2InputY, 150, 32, 6, COLOR_EDIT_BG);
    
    // Buttons
    DrawButtonAnim(memDC, CARD_PADDING + 190, row2InputY, 100, 32, L"\x8FDE\x63A5", g_btnConnectAnim, g_isConnected);
    DrawButtonAnim(memDC, w - CARD_PADDING - 110, row2InputY, 90, 32, L"\x5E94\x7528", g_btnApplyAnim, true);
    
    // === Row 3: Show Performance Checkbox ===
    int row3Y = row2InputY + 45;
    int checkboxX = CARD_PADDING + 15;
    int checkboxSize = 20;
    
    // Checkbox background
    COLORREF cbBg = g_showPerfOnPause ? RGB(88, 166, 255) : RGB(50, 50, 65);
    DrawRoundRect(memDC, checkboxX, row3Y, checkboxSize, checkboxSize, 4, cbBg);
    
    // Checkbox border
    HPEN cbPen = CreatePen(PS_SOLID, 1, g_showPerfOnPause ? RGB(88, 166, 255) : RGB(80, 80, 100));
    HPEN oldCbPen = (HPEN)SelectObject(memDC, cbPen);
    SelectObject(memDC, GetStockObject(NULL_BRUSH));
    RoundRect(memDC, checkboxX, row3Y, checkboxX + checkboxSize, row3Y + checkboxSize, 8, 8);
    SelectObject(memDC, oldCbPen);
    DeleteObject(cbPen);
    
    // Checkbox tick mark
    if (g_showPerfOnPause) {
        HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
        HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
        MoveToEx(memDC, checkboxX + 5, row3Y + 10, nullptr);
        LineTo(memDC, checkboxX + 8, row3Y + 15);
        LineTo(memDC, checkboxX + 15, row3Y + 5);
        SelectObject(memDC, oldTickPen);
        DeleteObject(tickPen);
    }
    
    // Checkbox label
    DrawTextLeft(memDC, L"\x6682\x505C\x65F6\x663E\x793A\x6027\x80FD\x7EDF\x8BA1", checkboxX + 28, row3Y + 2, COLOR_TEXT, g_fontNormal);
    
    // Status indicator
    COLORREF statusColor = g_isConnected ? RGB(80, 200, 120) : RGB(220, 80, 80);
    HPEN statusPen = CreatePen(PS_SOLID, 1, statusColor);
    HBRUSH statusBrush = CreateSolidBrush(statusColor);
    HPEN oldStatusPen = (HPEN)SelectObject(memDC, statusPen);
    HBRUSH oldStatusBrush = (HBRUSH)SelectObject(memDC, statusBrush);
    Ellipse(memDC, CARD_PADDING + 310, row2InputY + 8, CARD_PADDING + 325, row2InputY + 23);
    SelectObject(memDC, oldStatusPen);
    SelectObject(memDC, oldStatusBrush);
    DeleteObject(statusPen);
    DeleteObject(statusBrush);
    
    // === Preview Card ===
    cardY = TITLEBAR_H + 15 + cardH + 10;
    cardH = h - cardY - CARD_PADDING;
    DrawRoundRect(memDC, CARD_PADDING, cardY, w - CARD_PADDING * 2, cardH, 16, COLOR_CARD);
    DrawRoundRect(memDC, CARD_PADDING, cardY, 5, 30, 2, COLOR_ACCENT);
    DrawTextLeft(memDC, L"\x9884\x89C8", CARD_PADDING + 15, cardY + 8, COLOR_TEXT, g_fontNormal);
    
    // Get current data
    std::wstring title, artist, time, lyric;
    double progress = 0;
    
    EnterCriticalSection(&g_cs);
    title = g_pendingTitle.empty() ? L"\x65E0\x6B4C\x66F2" : g_pendingTitle;
    artist = g_pendingArtist;
    time = g_pendingTime;
    progress = g_pendingProgress;
    
    int curIdx = -1;
    for (int i = (int)g_pendingLyrics.size() - 1; i >= 0; i--) {
        if (g_pendingCurrentTime >= g_pendingLyrics[i].startTime / 1000.0) { curIdx = i; break; }
    }
    
    if (curIdx >= 0 && curIdx < (int)g_pendingLyrics.size()) {
        lyric = L"\x25B6 " + TruncateStr(g_pendingLyrics[curIdx].text, 35);
    } else if (g_pendingTitle.empty()) {
        lyric = L"\x7B49\x5F85\x97F3\x4E50...";
    } else {
        lyric = L"(\x65E0\x6B4C\x8BCD)";
    }
    LeaveCriticalSection(&g_cs);
    
    // Song title (truncated)
    std::wstring displayTitle = TruncateStr(title, 28);
    DrawTextCentered(memDC, displayTitle.c_str(), w/2, cardY + 55, COLOR_TEXT, g_fontTitle);
    
    // Artist
    if (!artist.empty()) {
        std::wstring displayArtist = TruncateStr(artist, 35);
        DrawTextCentered(memDC, displayArtist.c_str(), w/2, cardY + 95, COLOR_TEXT_DIM, g_fontSubtitle);
    }
    
    // Progress bar
    DrawProgressBar(memDC, CARD_PADDING + 25, cardY + 135, w - CARD_PADDING * 2 - 50, 18, progress);
    DrawTextCentered(memDC, time.c_str(), w/2, cardY + 162, COLOR_TEXT_DIM, g_fontSmall);
    
    // Lyrics (truncated)
    DrawTextCentered(memDC, lyric.c_str(), w/2, cardY + 200, COLOR_TEXT, g_fontLyric);
    
    // Update tray
    if (!title.empty() && title != L"\x65E0\x6B4C\x66F2") {
        std::wstring tip = TruncateStr(title + L" - " + artist, 60);
        UpdateTrayTip(tip.c_str());
    }
    
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    
    EndPaint(hwnd, &ps);
}

void ApplySettings() {
    wchar_t buf[256];
    if (g_hEditIp) { GetWindowTextW(g_hEditIp, buf, 256); g_oscIp = buf; }
    if (g_hEditPort) { GetWindowTextW(g_hEditPort, buf, 256); g_oscPort = _wtoi(buf); }
    if (g_hEditMoekoePort) { GetWindowTextW(g_hEditMoekoePort, buf, 256); g_moekoePort = _wtoi(buf); }
    
    g_osc = new moekoe::OSCSender(WstringToUtf8(g_oscIp), g_oscPort);
    SaveConfig(L"config_gui.json");
}

void Connect() {
    if (g_client) { delete g_client; g_client = nullptr; }
    g_client = new moekoe::MoeKoeWS("127.0.0.1", g_moekoePort);
    g_client->setCallback(QueueUpdate);
    g_isConnected = g_client->connect();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

bool PtInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void UpdateAnimations() {
    g_btnConnectAnim.setTarget(g_btnConnectHover ? 1.0 : 0.0);
    g_btnApplyAnim.setTarget(g_btnApplyHover ? 1.0 : 0.0);
    g_btnCloseAnim.setTarget(g_btnCloseHover ? 1.0 : 0.0);
    g_btnMinAnim.setTarget(g_btnMinHover ? 1.0 : 0.0);
    g_btnConnectAnim.update();
    g_btnApplyAnim.update();
    g_btnCloseAnim.update();
    g_btnMinAnim.update();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Fonts
            g_fontTitle = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontSubtitle = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontNormal = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontSmall = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontLyric = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            g_fontLabel = CreateFontW(16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            
            g_brushBg = CreateSolidBrush(COLOR_BG);
            g_brushCard = CreateSolidBrush(COLOR_CARD);
            g_brushEditBg = CreateSolidBrush(COLOR_EDIT_BG);
            
            // Create edit controls - positions must match OnPaint drawing (with tabs)
            int cardY = TITLEBAR_H + TAB_H + 20;
            int labelY = cardY + 50;
            int inputY = labelY + 24;
            
            // OSC IP edit - aligned with label and background
            g_hEditIp = CreateWindowExW(0, L"EDIT", g_oscIp.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_BORDER,
                CARD_PADDING + 17, inputY + 2, 196, 28, hwnd, nullptr, nullptr, nullptr);
            SendMessage(g_hEditIp, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
            
            // OSC Port edit
            wchar_t portBuf[16]; swprintf_s(portBuf, L"%d", g_oscPort);
            g_hEditPort = CreateWindowExW(0, L"EDIT", portBuf,
                WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
                CARD_PADDING + 242, inputY + 2, 86, 28, hwnd, nullptr, nullptr, nullptr);
            SendMessage(g_hEditPort, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
            
            // MoeKoe Port edit - row 2
            int row2LabelY = inputY + 48;
            int row2InputY = row2LabelY + 24;
            swprintf_s(portBuf, L"%d", g_moekoePort);
            g_hEditMoekoePort = CreateWindowExW(0, L"EDIT", portBuf,
                WS_CHILD | WS_VISIBLE | ES_NUMBER | WS_BORDER,
                CARD_PADDING + 17, row2InputY + 2, 146, 28, hwnd, nullptr, nullptr, nullptr);
            SendMessage(g_hEditMoekoePort, WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
            
            CreateTrayIcon(hwnd);
            return 0;
        }
        
        case WM_CTLCOLOREDIT: {
            HDC hdcEdit = (HDC)wParam;
            SetTextColor(hdcEdit, COLOR_TEXT);
            SetBkColor(hdcEdit, COLOR_EDIT_BG);
            return (LRESULT)g_brushEditBg;
        }
        
        case WM_PAINT: OnPaint(hwnd); return 0;
        
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            if (g_dragging) {
                POINT pt; GetCursorPos(&pt);
                SetWindowPos(hwnd, nullptr, pt.x - g_dragStart.x, pt.y - g_dragStart.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                return 0;
            }
            
            int cardY = TITLEBAR_H + TAB_H + 20;
            int labelY = cardY + 50;
            int inputY = labelY + 24;
            int row2LabelY = inputY + 48;
            int row2InputY = row2LabelY + 24;
            
            bool oldConnect = g_btnConnectHover, oldApply = g_btnApplyHover;
            bool oldClose = g_btnCloseHover, oldMin = g_btnMinHover;
            
            g_btnConnectHover = PtInRect(x, y, CARD_PADDING + 190, row2InputY, 100, 32);
            g_btnApplyHover = PtInRect(x, y, WIN_W - CARD_PADDING - 110, row2InputY, 90, 32);
            g_btnCloseHover = PtInRect(x, y, WIN_W - 52, 10, 32, 32);
            g_btnMinHover = PtInRect(x, y, WIN_W - 95, 10, 32, 32);
            
            if (oldConnect != g_btnConnectHover || oldApply != g_btnApplyHover || 
                oldClose != g_btnCloseHover || oldMin != g_btnMinHover) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            // Tab click handling
            int tabW = 80;
            int startX = 15;
            int tabY = TITLEBAR_H + 5;
            for (int i = 0; i < g_tabCount; i++) {
                int tabX = startX + i * (tabW + 5);
                if (PtInRect(x, y, tabX, tabY, tabW, TAB_H)) {
                    g_currentTab = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            
            // Click on version/update area in title bar
            if (y < TITLEBAR_H && x >= 180 && x < WIN_W - 100) {
                if (g_updateAvailable && !g_downloadUrl.empty()) {
                    // Open download page in browser
                    ShellExecuteW(nullptr, L"open", g_downloadUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                } else if (!g_checkingUpdate) {
                    // Manual check for updates
                    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                        CheckForUpdate();
                        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
                        return 0;
                    }, nullptr, 0, nullptr);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            
            if (y < TITLEBAR_H && x < WIN_W - 100) {
                g_dragging = true;
                POINT pt; GetCursorPos(&pt);
                RECT rc; GetWindowRect(hwnd, &rc);
                g_dragStart.x = pt.x - rc.left;
                g_dragStart.y = pt.y - rc.top;
                SetCapture(hwnd);
                return 0;
            }
            
            int cardY = TITLEBAR_H + TAB_H + 20;
            int labelY = cardY + 50;
            int inputY = labelY + 24;
            int row2LabelY = inputY + 48;
            int row2InputY = row2LabelY + 24;
            
            if (PtInRect(x, y, CARD_PADDING + 190, row2InputY, 100, 32)) Connect();
            else if (PtInRect(x, y, WIN_W - CARD_PADDING - 110, row2InputY, 90, 34)) ApplySettings();
            else if (PtInRect(x, y, WIN_W - 52, 10, 32, 32)) {
                if (g_minimizeToTray) ShowWindow(hwnd, SW_HIDE);
                else PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            else if (PtInRect(x, y, WIN_W - 95, 10, 32, 32)) ShowWindow(hwnd, SW_MINIMIZE);
            else {
                // Row 3: Checkbox for show performance
                int row3Y = row2InputY + 45;
                int checkboxX = CARD_PADDING + 15;
                int checkboxSize = 20;
                if (PtInRect(x, y, checkboxX, row3Y, checkboxSize, checkboxSize)) {
                    g_showPerfOnPause = !g_showPerfOnPause;
                    SaveConfig(L"config_gui.json");
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        
        case WM_LBUTTONUP: if (g_dragging) { g_dragging = false; ReleaseCapture(); } return 0;
        
        case WM_TIMER: 
            UpdateAnimations(); 
            // Only invalidate if animation is running
            if (g_btnConnectAnim.value != g_btnConnectAnim.target ||
                g_btnApplyAnim.value != g_btnApplyAnim.target ||
                g_btnCloseAnim.value != g_btnCloseAnim.target ||
                g_btnMinAnim.value != g_btnMinAnim.target) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            else if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, 1, L"\x663E\x793A");
                AppendMenuW(menu, MF_STRING, 2, L"\x9000\x51FA");
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                if (cmd == 1) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
                else if (cmd == 2) PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        
        case WM_CLOSE: RemoveTrayIcon(); DestroyWindow(hwnd); return 0;
        
        case WM_DESTROY:
            if (g_fontTitle) DeleteObject(g_fontTitle);
            if (g_fontSubtitle) DeleteObject(g_fontSubtitle);
            if (g_fontNormal) DeleteObject(g_fontNormal);
            if (g_fontSmall) DeleteObject(g_fontSmall);
            if (g_fontLyric) DeleteObject(g_fontLyric);
            if (g_fontLabel) DeleteObject(g_fontLabel);
            if (g_brushBg) DeleteObject(g_brushBg);
            if (g_brushCard) DeleteObject(g_brushCard);
            if (g_brushEditBg) DeleteObject(g_brushEditBg);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    InitCommonControls();
    
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MoeKoeOSC_GUI_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"\x7A0B\x5E8F\x5DF2\x5728\x8FD0\x884C!", L"MoeKoe OSC", MB_OK | MB_ICONWARNING);
        return 0;
    }
    
    InitializeCriticalSection(&g_cs);
    g_startTime = GetTickCount();
    LoadConfig(L"config_gui.json");
    
    g_noLyricMsgs = LoadNoLyricMessages(L"config.json");
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
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MoeKoeOSC_Class";
    RegisterClassExW(&wc);
    
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, L"MoeKoeOSC_Class", L"",
        WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H, nullptr, nullptr, hInst, nullptr);
    
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
        DwmSetWindowAttribute_t fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            int pref = 2; fn(g_hwnd, 33, &pref, sizeof(pref));
            BOOL dark = TRUE; fn(g_hwnd, 20, &dark, sizeof(dark));
        }
        FreeLibrary(hDwm);
    }
    
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);
    
    g_osc = new moekoe::OSCSender(WstringToUtf8(g_oscIp), g_oscPort);
    Connect();
    SetTimer(g_hwnd, 1, 16, nullptr);
    
    // Auto-check for updates on startup (in background thread)
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        Sleep(2000);  // Wait 2 seconds before checking
        CheckForUpdate();
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
        return 0;
    }, nullptr, 0, nullptr);
    
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    
    if (g_client) delete g_client;
    if (g_osc) delete g_osc;
    DeleteCriticalSection(&g_cs);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return (int)msg.wParam;
}
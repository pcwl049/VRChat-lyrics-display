// VRChat Lyrics Display - Modern Settings GUI (Borderless + Animations)
#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_IE 0x0600

// Version info
#define APP_VERSION "0.2"
#define APP_VERSION_NUM 200  // 0.2 -> 0*10000 + 2*100 + 0 = 200
#define GITHUB_REPO "pcwl049/VRChat-lyrics-display"
#define GITHUB_API_URL "https://api.github.com/repos/pcwl049/VRChat-lyrics-display/releases/latest"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <dwmapi.h>
#include <psapi.h>
#include <cstdio>

// Debug log using Windows API - write to user temp directory
static void MainDebugLog(const char* msg) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\vrclayrics_debug.log");
    
    HANDLE hFile = CreateFileA(tempPath, 
        FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
        WriteFile(hFile, "\r\n", 2, &written, NULL);
        CloseHandle(hFile);
    }
    OutputDebugStringA(msg);
}
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

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winhttp.lib")

// Colors - DeepBlue Theme (毛玻璃深空蓝主题)
#define COLOR_BG_START RGB(15, 20, 35)
#define COLOR_BG_END RGB(25, 35, 55)
#define COLOR_BG RGB(18, 18, 24)
#define COLOR_CARD RGB(30, 40, 60)
#define COLOR_CARD_BORDER RGB(50, 70, 100)
#define COLOR_ACCENT RGB(80, 180, 255)
#define COLOR_ACCENT_GLOW RGB(60, 140, 220)
#define COLOR_TEXT RGB(240, 245, 255)
#define COLOR_TEXT_DIM RGB(140, 150, 170)
#define COLOR_BORDER RGB(50, 70, 100)
#define COLOR_TITLEBAR RGB(20, 28, 45)
#define COLOR_EDIT_BG RGB(35, 45, 65)
#define COLOR_GLASS_TINT RGB(20, 30, 50)
#define GLASS_ALPHA 220

// Tray icon
#define TRAY_ICON_ID 1
#define WM_TRAYICON (WM_USER + 200)

// OSC rate limits
const DWORD OSC_MIN_INTERVAL = 4000;
const DWORD OSC_PAUSE_INTERVAL = 3000;

// Window size (scaled for high DPI)
const int WIN_W_DEFAULT = 650;
const int WIN_H_DEFAULT = 720;
const int WIN_W_MIN = 600;
const int WIN_H_MIN = 620;
int g_winW = WIN_W_DEFAULT;
int g_winH = WIN_H_DEFAULT;
int g_winX = -1;  // Window position (saved on close)
int g_winY = -1;
const int TITLEBAR_H = 60;
const int CARD_PADDING = 25;

// Forward declarations
std::string WstringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWstring(const std::string& str);

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
moekoe::MoeKoeWS* g_moeKoeClient = nullptr;
moekoe::NeteaseWS* g_neteaseClient = nullptr;
const wchar_t* g_platformNames[] = { L"MoeKoeMusic", L"\x7F51\x6613\x4E91\x97F3\x4E50" };
const wchar_t* g_oscPlatformNames[] = { L"\x9177\x72D7", L"\x7F51\x6613\x4E91\x97F3\x4E50" };
const int g_platformCount = 2;
int g_currentPlatform = 0;  // 0=MoeKoe, 1=Netease (user selected)
int g_activePlatform = -1;   // Currently active platform (playing music), -1 = none
bool g_autoPlatformSwitch = true; // Auto switch platforms when playing
bool g_moeKoeConnected = false;
bool g_neteaseConnected = false;
DWORD g_moeKoeLastPlayTime = 0;  // Last time MoeKoe was playing
DWORD g_neteaseLastPlayTime = 0; // Last time Netease was playing
const DWORD PLATFORM_SWITCH_DELAY = 2000; // Wait 2s before switching platforms
int g_currentTab = 0;        // 0=Main, 1=Settings
bool g_tabHover[2] = {false, false};
bool g_moeKoeBoxHover = false;
bool g_neteaseBoxHover = false;

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
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/repos/pcwl049/VRChat-lyrics-display/releases/latest", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
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
Animation g_btnConnectAnim, g_btnApplyAnim, g_btnCloseAnim, g_btnMinAnim, g_btnUpdateAnim, g_btnLaunchAnim, g_btnExportLogAnim;
bool g_btnConnectHover = false, g_btnApplyHover = false;
bool g_btnCloseHover = false, g_btnMinHover = false, g_btnUpdateHover = false, g_btnLaunchHover = false, g_btnExportLogHover = false, g_btnAdminHover = false;

// Song data
std::wstring g_pendingTitle, g_pendingArtist, g_pendingTime;
double g_pendingProgress = 0;
double g_pendingCurrentTime = 0;
double g_pendingDuration = 0;
bool g_pendingIsPlaying = true;
std::vector<moekoe::LyricLine> g_pendingLyrics;
DWORD g_lastOscSendTime = 0;
std::wstring g_lastOscMessage;
bool g_lastIsPlaying = false;
bool g_playStateChanged = false;

// Preview cache (to avoid rapid flickering)
std::wstring g_cachedPreviewMsg;
DWORD g_lastPreviewUpdate = 0;
const DWORD PREVIEW_UPDATE_INTERVAL = 500;  // Update preview every 500ms

// Redraw optimization
bool g_needsRedraw = true;         // Dirty flag for redraw
DWORD g_lastContentChange = 0;     // Last time content actually changed
const DWORD IDLE_REDRAW_INTERVAL = 1000;  // Redraw every 1s when idle

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
bool g_showPerfOnPause = true;  // Show performance stats when paused
bool g_autoUpdate = true;       // Auto check for updates on startup
bool g_showPlatform = true;     // Show platform name in OSC message
bool g_autoStart = false;       // Auto start with Windows
bool g_runAsAdmin = false;      // Run as administrator on startup
bool g_isConnected = false;
HANDLE g_mutex = nullptr;       // Single instance mutex
std::vector<std::wstring> g_noLyricMsgs;
int g_lastNoLyricIdx = -1;

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

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
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
    g_autoUpdate = getBool("auto_update", true);
    g_showPlatform = getBool("show_platform", true);
    
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
}

void SaveConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fprintf(f, "{\n  \"osc\": {\n    \"ip\": \"%ls\",\n    \"port\": %d\n  },\n", g_oscIp.c_str(), g_oscPort);
    fprintf(f, "  \"moekoe_port\": %d,\n  \"osc_enabled\": %s,\n  \"minimize_to_tray\": %s,\n  \"show_perf_on_pause\": %s,\n  \"auto_update\": %s,\n  \"show_platform\": %s,\n  \"auto_start\": %s,\n", 
            g_moekoePort, g_oscEnabled ? "true" : "false", g_minimizeToTray ? "true" : "false", 
            g_showPerfOnPause ? "true" : "false", g_autoUpdate ? "true" : "false", 
            g_showPlatform ? "true" : "false", g_autoStart ? "true" : "false");
    fprintf(f, "  \"win_width\": %d,\n  \"win_height\": %d,\n  \"win_x\": %d,\n  \"win_y\": %d,\n",
            g_winW, g_winH, g_winX, g_winY);
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

// Export logs to a file for bug reporting
void ExportLogs(HWND hwnd) {
    // Generate default filename with timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t defaultName[MAX_PATH];
    swprintf_s(defaultName, L"VRCLyricsDisplay_Logs_%04d%02d%02d_%02d%02d%02d.txt", 
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    
    // Setup OPENFILENAME struct
    wchar_t filePath[MAX_PATH] = {0};
    wcscpy_s(filePath, defaultName);
    
    OPENFILENAMEW ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"\x65E5\x5FD7\x6587\x4EF6 (*.txt)\0*.txt\0\x6240\x6709\x6587\x4EF6 (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"txt";
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
    allLogs += std::string("OS: ") + osStr + "\n\n";
    
    // Read vrclayrics_debug.log
    allLogs += "=== vrclayrics_debug.log ===\n";
    char vrclayricsLogPath[MAX_PATH];
    sprintf_s(vrclayricsLogPath, "%svrclayrics_debug.log", tempPath);
    FILE* mf = fopen(vrclayricsLogPath, "r");
    if (mf) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), mf)) {
            allLogs += buf;
        }
        fclose(mf);
    } else {
        allLogs += "(log file not found)\n";
    }
    allLogs += "\n";
    
    // Read netease_debug.log
    allLogs += "=== netease_debug.log ===\n";
    char neteaseLogPath[MAX_PATH];
    sprintf_s(neteaseLogPath, "%snetease_debug.log", tempPath);
    FILE* nf = fopen(neteaseLogPath, "r");
    if (nf) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), nf)) {
            allLogs += buf;
        }
        fclose(nf);
    } else {
        allLogs += "(log file not found)\n";
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
    
    // Write to destination file
    FILE* out = _wfopen(filePath, L"w");
    if (out) {
        fwrite(allLogs.c_str(), 1, allLogs.size(), out);
        fclose(out);
        
        std::wstring successMsg = L"\x65E5\x5FD7\x5DF2\x5BFC\x51FA\x5230:\n" + std::wstring(filePath);
        ShowInfoDialog(L"\x5BFC\x51FA\x6210\x529F", successMsg);
    } else {
        ShowErrorDialog(L"\x9519\x8BEF", L"\x5BFC\x51FA\x5931\x8D25\xFF0C\x8BF7\x68C0\x67E5\x6587\x4EF6\x8DEF\x5F84");
    }
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
        // Line 1: Song title - artist
        msg = L"\x266B ";
        std::wstring songInfo = info.title;
        if (!info.artist.empty()) {
            songInfo += L" - " + info.artist;
        }
        // Truncate to fit (Chinese ~3 bytes each, leave room for other lines)
        msg += TruncateStr(songInfo, 35);
        
        // Add platform indicator if enabled
        if (g_showPlatform) {
            msg += L" [" + (std::wstring)g_oscPlatformNames[g_activePlatform >= 0 ? g_activePlatform : g_currentPlatform] + L"]";
        }
        
        // Line 2: Progress bar
        if (info.duration > 0) {
            msg += L"\n";
            msg += BuildProgressBar(info.currentTime / info.duration, 8);
            msg += L" " + FormatTime(info.currentTime) + L"/" + FormatTime(info.duration);
        }
        
        // Line 3: Lyrics
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

void DrawRoundRectWithBorder(HDC hdc, int x, int y, int w, int h, int radius, COLORREF fillColor, COLORREF borderColor) {
    HPEN pen = CreatePen(PS_SOLID, 2, borderColor);
    HBRUSH brush = CreateSolidBrush(fillColor);
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

// Draw text vertically centered in a given rect area (for checkboxes)
void DrawTextVCentered(HDC hdc, const wchar_t* text, int x, int y, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color); SetBkMode(hdc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE sz; GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, x, y + (h - sz.cy) / 2, text, (int)wcslen(text));
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
            return 0;
        }
        case WM_TIMER: {
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
            
            // Background
            HBRUSH bgBrush = CreateSolidBrush(COLOR_BG);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);
            
            // Card with rounded corners
            DrawRoundRect(memDC, 0, 0, w, h, 16, COLOR_CARD);
            
            // Accent bar - color based on type
            COLORREF accentColor = COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) accentColor = RGB(255, 100, 100);
            else if (g_dialogConfig.type == DIALOG_CONFIRM) accentColor = RGB(255, 180, 50);
            DrawRoundRect(memDC, 0, 0, 5, h, 2, accentColor);
            
            // Title (use main window's subtitle font, or default if not initialized)
            SetTextColor(memDC, COLOR_TEXT);
            SetBkMode(memDC, TRANSPARENT);
            HFONT titleFont = g_fontSubtitle ? g_fontSubtitle : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(memDC, titleFont);
            if (!g_dialogConfig.title.empty()) {
                TextOutW(memDC, 30, 25, g_dialogConfig.title.c_str(), (int)g_dialogConfig.title.length());
            }
            SelectObject(memDC, oldFont);
            
            // Content (multi-line support, use main window's normal font)
            HFONT contentFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(memDC, contentFont);
            int lineY = 70;
            int lineH = 32;
            
            // Split content by \n
            std::wstring content = g_dialogConfig.content;
            size_t pos = 0;
            while (pos < content.length()) {
                size_t nextPos = content.find(L'\n', pos);
                if (nextPos == std::wstring::npos) nextPos = content.length();
                std::wstring line = content.substr(pos, nextPos - pos);
                TextOutW(memDC, 25, lineY, line.c_str(), (int)line.length());
                lineY += lineH;
                pos = nextPos + 1;
            }
            SelectObject(memDC, oldFont);
            
            // Buttons layout
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                // Three buttons: Primary | Secondary | Skip
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                // Two buttons
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
            } else {
                // Single button (centered)
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            // Button 1 (Primary - accent)
            COLORREF btn1Bg = (g_dialogBtnHover == 0) ? RGB(110, 190, 255) : COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) btn1Bg = (g_dialogBtnHover == 0) ? RGB(255, 120, 120) : RGB(220, 80, 80);
            DrawRoundRect(memDC, btn1X, btnY, btnW, btnH, 8, btn1Bg);
            HFONT btnFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(memDC, btnFont);
            SetTextColor(memDC, RGB(0, 0, 0));
            RECT btn1Rc = {btn1X, btnY, btn1X + btnW, btnY + btnH};
            if (!g_dialogConfig.btn1Text.empty()) {
                DrawTextW(memDC, g_dialogConfig.btn1Text.c_str(), (int)g_dialogConfig.btn1Text.length(), &btn1Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            // Button 2 (Secondary - border style)
            if (g_dialogConfig.hasBtn2) {
                COLORREF btn2Bg = (g_dialogBtnHover == 1) ? RGB(70, 70, 90) : RGB(50, 50, 65);
                DrawRoundRect(memDC, btn2X, btnY, btnW, btnH, 8, btn2Bg);
                HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
                HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
                SelectObject(memDC, GetStockObject(NULL_BRUSH));
                RoundRect(memDC, btn2X, btnY, btn2X + btnW, btnY + btnH, 16, 16);
                SelectObject(memDC, oldPen);
                DeleteObject(borderPen);
                SetTextColor(memDC, COLOR_TEXT);
                RECT btn2Rc = {btn2X, btnY, btn2X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn2Text.empty()) {
                    DrawTextW(memDC, g_dialogConfig.btn2Text.c_str(), (int)g_dialogConfig.btn2Text.length(), &btn2Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            // Button 3 (Tertiary - for Skip Version)
            if (g_dialogConfig.hasBtn3) {
                COLORREF btn3Bg = (g_dialogBtnHover == 2) ? RGB(50, 50, 60) : RGB(35, 35, 45);
                DrawRoundRect(memDC, btn3X, btnY, btnW, btnH, 8, btn3Bg);
                SetTextColor(memDC, COLOR_TEXT_DIM);
                RECT btn3Rc = {btn3X, btnY, btn3X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn3Text.empty()) {
                    DrawTextW(memDC, g_dialogConfig.btn3Text.c_str(), (int)g_dialogConfig.btn3Text.length(), &btn3Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            SelectObject(memDC, oldFont);
            
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

bool ShowCustomDialog(const DialogConfig& config) {
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
    
    SetLayeredWindowAttributes(g_dialogHwnd, 0, 255, LWA_ALPHA);
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
    return ShowCustomDialog(config);
}

bool ShowErrorDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_ERROR;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x786E\x5B9A";  // 确定
    config.hasBtn2 = false;
    return ShowCustomDialog(config);
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
    return ShowCustomDialog(config);
}
// === End Custom Dialog ===

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
    
    // Center text both horizontally and vertically
    DrawTextCenteredBoth(hdc, text, x, y, w, h, accent ? RGB(0,0,0) : COLOR_TEXT, g_fontNormal);
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

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    
    // Background gradient (DeepBlue theme) with rounded corners
    int cornerRadius = 16;  // Slightly larger to fully cover DWM corners
    for (int y = 0; y < h; y++) {
        double t = (double)y / h;
        int r = (int)(GetRValue(COLOR_BG_START) + (GetRValue(COLOR_BG_END) - GetRValue(COLOR_BG_START)) * t);
        int g = (int)(GetGValue(COLOR_BG_START) + (GetGValue(COLOR_BG_END) - GetGValue(COLOR_BG_START)) * t);
        int b = (int)(GetBValue(COLOR_BG_START) + (GetBValue(COLOR_BG_END) - GetBValue(COLOR_BG_START)) * t);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        
        // Calculate start X for rounded corners at top and bottom
        int startX = 0, endX = w;
        if (y < cornerRadius) {
            // Top corners - use circle equation to determine start position
            int dy = cornerRadius - y;
            int offset = cornerRadius - (int)sqrt((double)(cornerRadius * cornerRadius - dy * dy));
            if (offset < 0) offset = 0;
            startX = offset;
            // Right corner
            int rightStart = w - offset;
            // Draw left part (skip corner)
            MoveToEx(memDC, startX, y, nullptr); LineTo(memDC, rightStart, y);
        } else if (y >= h - cornerRadius) {
            // Bottom corners
            int dy = y - (h - cornerRadius);
            int offset = cornerRadius - (int)sqrt((double)(cornerRadius * cornerRadius - dy * dy));
            if (offset < 0) offset = 0;
            startX = offset;
            int rightStart = w - offset;
            MoveToEx(memDC, startX, y, nullptr); LineTo(memDC, rightStart, y);
        } else {
            // Middle area - full width
            MoveToEx(memDC, 0, y, nullptr); LineTo(memDC, w, y);
        }
        DeleteObject(pen);
    }
    
    // Title Bar with gradient (DeepBlue theme) - respecting rounded corners
    for (int y = 0; y < TITLEBAR_H + 5; y++) {
        double t = (double)y / (TITLEBAR_H + 5);
        int r = (int)(GetRValue(COLOR_TITLEBAR) + (GetRValue(COLOR_BG_START) - GetRValue(COLOR_TITLEBAR)) * t);
        int g = (int)(GetGValue(COLOR_TITLEBAR) + (GetGValue(COLOR_BG_START) - GetGValue(COLOR_TITLEBAR)) * t);
        int b = (int)(GetBValue(COLOR_TITLEBAR) + (GetBValue(COLOR_BG_START) - GetBValue(COLOR_TITLEBAR)) * t);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        
        // Respect rounded corners at top
        if (y < cornerRadius) {
            int dy = cornerRadius - y;
            int offset = cornerRadius - (int)sqrt((double)(cornerRadius * cornerRadius - dy * dy));
            if (offset < 0) offset = 0;
            MoveToEx(memDC, offset, y, nullptr); LineTo(memDC, w - offset, y);
        } else {
            MoveToEx(memDC, 0, y, nullptr); LineTo(memDC, w, y);
        }
        DeleteObject(pen);
    }
    // Title bar glow line (skip corners)
    HPEN glowPen = CreatePen(PS_SOLID, 1, COLOR_ACCENT_GLOW);
    HPEN oldGlowPen = (HPEN)SelectObject(memDC, glowPen);
    MoveToEx(memDC, cornerRadius, TITLEBAR_H + 4, nullptr); LineTo(memDC, w - cornerRadius, TITLEBAR_H + 4);
    SelectObject(memDC, oldGlowPen);
    DeleteObject(glowPen);
    DrawTextLeft(memDC, L"\x266B \x97F3\x4E50\x663E\x793A", 25, 18, COLOR_TEXT, g_fontSubtitle);
    
    // Update button (between version and minimize)
    int updateBtnX = w - 200;
    if (g_downloadingUpdate) {
        // Show download progress
        wchar_t progressText[32];
        swprintf_s(progressText, L"\x4E0B\x8F7D %d%%", g_downloadProgress);
        DrawTextLeft(memDC, progressText, updateBtnX + 5, 22, COLOR_ACCENT, g_fontSmall);
    } else if (g_updateAvailable && !g_latestVersion.empty()) {
        // Show update available
        DrawButtonAnim(memDC, updateBtnX, 14, 80, 32, L"\x66F4\x65B0", g_btnUpdateAnim, false);
    } else if (g_checkingUpdate) {
        DrawTextLeft(memDC, L"\x68C0\x67E5\x4E2D...", updateBtnX + 5, 22, COLOR_TEXT_DIM, g_fontSmall);
    } else {
        // Show check update button
        DrawButtonAnim(memDC, updateBtnX, 14, 80, 32, L"\x68C0\x67E5", g_btnUpdateAnim, false);
    }
    
    DrawTitleBarButton(memDC, w - 110, 14, 38, L"\x2500", g_btnMinAnim);
    DrawTitleBarButton(memDC, w - 60, 14, 38, L"\x2715", g_btnCloseAnim);
    
    // === Two Column Layout ===
    int leftColW = 260;  // Left column width for settings
    int rightColX = leftColW + CARD_PADDING * 2;  // Right column start X
    int rightColW = w - rightColX - CARD_PADDING;
    int contentY = TITLEBAR_H + 20;
    
    // === LEFT COLUMN (包含标签页) ===
    int panelH = h - contentY - CARD_PADDING;
    DrawRoundRect(memDC, CARD_PADDING, contentY, leftColW, panelH, 16, COLOR_CARD);
    DrawRoundRect(memDC, CARD_PADDING, contentY, 5, 35, 2, COLOR_ACCENT);
    
    // === Tabs (在卡片内部) ===
    const wchar_t* tabNames[] = { L"\x4E3B\x9875", L"\x8BBE\x7F6E" };  // 主页, 设置
    int tabW = 80;
    int tabH = 32;
    int tabY = contentY + 10;
    for (int t = 0; t < 2; t++) {
        int tabX = CARD_PADDING + 18 + t * (tabW + 8);
        bool isActive = (t == g_currentTab);
        bool isHover = g_tabHover[t];
        COLORREF tabBg = isActive ? COLOR_ACCENT : (isHover ? RGB(60, 60, 80) : RGB(40, 40, 55));
        COLORREF tabText = isActive ? RGB(0, 0, 0) : COLOR_TEXT;
        DrawRoundRect(memDC, tabX, tabY, tabW, tabH, 8, tabBg);
        DrawTextLeft(memDC, tabNames[t], tabX + tabW/2 - 16, tabY + 7, tabText, g_fontNormal);
    }
    
    int rowY = contentY + 55;
    
    int checkboxX = CARD_PADDING + 18;
    int checkboxSize = 26;
    
    if (g_currentTab == 0) {
        // === MAIN TAB: Platform Status, Connect, Launch ===
        DrawTextLeft(memDC, L"\x97F3\x4E50\x5E73\x53F0:", CARD_PADDING + 18, rowY, COLOR_TEXT_DIM, g_fontSmall);
        
        // Platform status boxes (side by side, clickable to switch)
        int boxW = (leftColW - 36 - 10) / 2;
        int box1X = CARD_PADDING + 18;
        int box2X = box1X + boxW + 10;
        
        // MoeKoe status box - color indicates status directly
        bool mkActive = (g_activePlatform == 0);
        bool mkHover = g_moeKoeBoxHover && g_moeKoeConnected;
        COLORREF mkBg, mkBorder;
        if (g_moeKoeConnected) {
            if (mkActive) {
                mkBg = RGB(40, 100, 60);      // Active: filled green
                mkBorder = RGB(80, 200, 120);  // Bright green border
            } else {
                mkBg = RGB(35, 45, 35);        // Connected but not active
                mkBorder = RGB(60, 120, 80);   // Dim green border
            }
        } else {
            mkBg = RGB(45, 45, 50);            // Not connected
            mkBorder = RGB(70, 70, 75);        // Gray border
        }
        if (mkHover) { mkBg = RGB(50, 70, 55); mkBorder = RGB(100, 180, 140); }
        DrawRoundRectWithBorder(memDC, box1X, rowY + 22, boxW, 32, 6, mkBg, mkBorder);
        DrawTextLeft(memDC, L"MoeKoe", box1X + 8, rowY + 28, g_moeKoeConnected ? COLOR_TEXT : COLOR_TEXT_DIM, g_fontSmall);
        
        // Netease status box - color indicates status directly
        bool ncActive = (g_activePlatform == 1);
        bool ncHover = g_neteaseBoxHover && g_neteaseConnected;
        COLORREF ncBg, ncBorder;
        if (g_neteaseConnected) {
            if (ncActive) {
                ncBg = RGB(40, 100, 60);      // Active: filled green
                ncBorder = RGB(80, 200, 120);  // Bright green border
            } else {
                ncBg = RGB(35, 45, 35);        // Connected but not active
                ncBorder = RGB(60, 120, 80);   // Dim green border
            }
        } else {
            ncBg = RGB(45, 45, 50);            // Not connected
            ncBorder = RGB(70, 70, 75);        // Gray border
        }
        if (ncHover) { ncBg = RGB(50, 70, 55); ncBorder = RGB(100, 180, 140); }
        DrawRoundRectWithBorder(memDC, box2X, rowY + 22, boxW, 32, 6, ncBg, ncBorder);
        DrawTextLeft(memDC, L"\x7F51\x6613\x4E91", box2X + 8, rowY + 28, g_neteaseConnected ? COLOR_TEXT : COLOR_TEXT_DIM, g_fontSmall);
        
        // === Connect Button ===
        rowY += 60;
        DrawButtonAnim(memDC, CARD_PADDING + 18, rowY, leftColW - 36, 42, 
            g_isConnected ? L"\x91CD\x65B0\x8FDE\x63A5" : L"\x8FDE\x63A5", g_btnConnectAnim, g_isConnected);
        
        // === Launch Netease Button (only when platform is Netease) ===
        if (!g_neteaseConnected) {
            rowY += 55;
            DrawButtonAnim(memDC, CARD_PADDING + 18, rowY, leftColW - 36, 36,
                L"\x542F\x52A8\x7F51\x6613\x4E91\x97F3\x4E50", g_btnLaunchAnim, false);
        }
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
        COLORREF cbBg = g_showPerfOnPause ? RGB(88, 166, 255) : RGB(50, 50, 65);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg);
        
        if (g_showPerfOnPause) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x6682\x505C\x7EDF\x8BA1", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Auto Update ===
        rowY += 38;
        COLORREF cbBg2 = g_autoUpdate ? RGB(88, 166, 255) : RGB(50, 50, 65);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg2);
        
        if (g_autoUpdate) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x81EA\x52A8\x66F4\x65B0", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Show Platform ===
        rowY += 38;
        COLORREF cbBg3 = g_showPlatform ? RGB(88, 166, 255) : RGB(50, 50, 65);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg3);
        
        if (g_showPlatform) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x663E\x793A\x5E73\x53F0", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Minimize to Tray ===
        rowY += 38;
        COLORREF cbBg4 = g_minimizeToTray ? RGB(88, 166, 255) : RGB(50, 50, 65);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg4);
        
        if (g_minimizeToTray) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x6700\x5C0F\x5316\x5230\x6258\x76D8", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Auto Start ===
        rowY += 38;
        COLORREF cbBg5 = g_autoStart ? COLOR_ACCENT : RGB(50, 55, 70);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg5);
        
        if (g_autoStart) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x5F00\x673A\x81EA\x542F", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Checkbox: Run as Admin ===
        rowY += 38;
        COLORREF cbBg6 = g_runAsAdmin ? COLOR_ACCENT : RGB(50, 55, 70);
        DrawRoundRect(memDC, checkboxX, rowY, checkboxSize, checkboxSize, 4, cbBg6);
        
        if (g_runAsAdmin) {
            HPEN tickPen = CreatePen(PS_SOLID, 2, RGB(0, 0, 0));
            HPEN oldTickPen = (HPEN)SelectObject(memDC, tickPen);
            MoveToEx(memDC, checkboxX + 6, rowY + 13, nullptr);
            LineTo(memDC, checkboxX + 10, rowY + 18);
            LineTo(memDC, checkboxX + 20, rowY + 8);
            SelectObject(memDC, oldTickPen);
            DeleteObject(tickPen);
        }
        DrawTextVCentered(memDC, L"\x7BA1\x7406\x5458\x542F\x52A8", checkboxX + checkboxSize + 8, rowY, checkboxSize, COLOR_TEXT, g_fontNormal);
        
        // === Admin Status ===
        rowY += 38;
        bool isAdmin = IsRunningAsAdmin();
        if (isAdmin) {
            DrawTextLeft(memDC, L"\x2705 \x5DF2\x662F\x7BA1\x7406\x5458\x6743\x9650", checkboxX, rowY + 4, RGB(100, 200, 120), g_fontSmall);
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
        
        // === Export Log Button ===
        rowY += 55;
        DrawButtonAnim(memDC, checkboxX, rowY, leftColW - 36 - checkboxX, 38, 
            L"\x5BFC\x51FA\x65E5\x5FD7", g_btnExportLogAnim, false);
    }
    
    // === RIGHT COLUMN: OSC Message Preview ===
    int previewH = h - contentY - CARD_PADDING;
    DrawRoundRect(memDC, rightColX, contentY, rightColW, previewH, 16, COLOR_CARD);
    DrawRoundRect(memDC, rightColX, contentY, 5, 35, 2, RGB(255, 180, 50));
    DrawTextLeft(memDC, L"OSC \x6D88\x606F\x9884\x89C8", rightColX + 18, contentY + 10, COLOR_TEXT, g_fontNormal);
    
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
        
        g_cachedPreviewMsg = FormatOSCMessage(previewInfo);
        g_lastPreviewUpdate = now;
    }
    oscMessage = g_cachedPreviewMsg;
    
    // Draw OSC message preview with word wrap
    int msgY = contentY + 55;
    int msgX = rightColX + 18;
    int lineHeight = 28;
    int maxLines = (previewH - 100) / lineHeight;  // Leave more space at bottom
    int maxTextWidth = rightColW - 40;  // Ensure text fits within card
    
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
            size_t fitLen = remaining.length();
            while (fitLen > 0 && getTextWidth(remaining.substr(0, fitLen)) > maxTextWidth) {
                fitLen--;
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
    
    // Draw byte count indicator
    std::string utf8Msg = WstringToUtf8(oscMessage);
    wchar_t byteBuf[64];
    swprintf_s(byteBuf, L"\x5B57\x8282\x6570: %zd/144", utf8Msg.length());
    COLORREF byteColor = utf8Msg.length() > 144 ? RGB(255, 100, 100) : COLOR_TEXT_DIM;
    DrawTextLeft(memDC, byteBuf, rightColX + 18, contentY + previewH - 40, byteColor, g_fontSmall);
    
    // Warning if over limit - show next to byte count if there's space
    if (utf8Msg.length() > 144) {
        DrawTextLeft(memDC, L"\x26A0\x8D85\x9650", rightColX + 130, contentY + previewH - 40, RGB(255, 180, 50), g_fontSmall);
    }
    
    // Update tray
    if (!previewInfo.title.empty()) {
        std::wstring tip = TruncateStr(previewInfo.title + L" - " + previewInfo.artist, 60);
        UpdateTrayTip(tip.c_str());
    }
    
    // === Version display at bottom left ===
    wchar_t verBuf[32];
    swprintf_s(verBuf, L"v" APP_VERSION);
    DrawTextLeft(memDC, verBuf, CARD_PADDING + 18, h - 25, COLOR_TEXT_DIM, g_fontSmall);
    
    // === Author display at bottom right ===
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
    SaveConfig(L"config_gui.json");
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
    MainDebugLog("[Main] Connect() called");
    Disconnect();  // Clean up any existing connections
    
    // Show connecting status immediately
    g_isConnected = false;
    g_moeKoeConnected = false;
    g_neteaseConnected = false;
    g_activePlatform = -1;  // No active platform yet
                g_pendingTitle = L"\x6B63\x5728\x8FDE\x63A5...";
                g_pendingArtist = L"";
                g_needsRedraw = true;    g_pendingLyrics.clear();
    InvalidateRect(g_hwnd, nullptr, FALSE);
    
    // Run connection in background thread to avoid blocking UI
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        MainDebugLog("[Connect] Starting connection attempt...");
        
        // Try to connect MoeKoe
        MainDebugLog("[Connect] Trying MoeKoe...");
        g_moeKoeClient = new moekoe::MoeKoeWS("127.0.0.1", g_moekoePort);
        g_moeKoeClient->setCallback([](const moekoe::SongInfo& info) {
            QueueUpdate(info, 0);
        });
        if (g_moeKoeClient->connect()) {
            g_moeKoeConnected = true;
            g_needsRedraw = true;
            MainDebugLog("[Connect] MoeKoe connected!");
        } else {
            MainDebugLog("[Connect] MoeKoe connection failed");
            delete g_moeKoeClient;
            g_moeKoeClient = nullptr;
        }
        
        // Try to connect Netease
        MainDebugLog("[Connect] Trying Netease (port 9222)...");
        g_neteaseClient = new moekoe::NeteaseWS(9222);
        g_neteaseClient->setCallback([](const moekoe::SongInfo& info) {
            QueueUpdate(info, 1);
        });
        if (g_neteaseClient->connect()) {
            g_neteaseConnected = true;
            g_needsRedraw = true;
            MainDebugLog("[Connect] Netease connected!");
        } else {
            MainDebugLog("[Connect] Netease connection failed");
            delete g_neteaseClient;
            g_neteaseClient = nullptr;
        }
        
        // Update connection status
        g_isConnected = g_moeKoeConnected || g_neteaseConnected;
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
    if (fabs(g_btnConnectAnim.value - g_btnConnectAnim.target) > 0.001) return true;
    if (fabs(g_btnApplyAnim.value - g_btnApplyAnim.target) > 0.001) return true;
    if (fabs(g_btnCloseAnim.value - g_btnCloseAnim.target) > 0.001) return true;
    if (fabs(g_btnMinAnim.value - g_btnMinAnim.target) > 0.001) return true;
    if (fabs(g_btnUpdateAnim.value - g_btnUpdateAnim.target) > 0.001) return true;
    if (fabs(g_btnLaunchAnim.value - g_btnLaunchAnim.target) > 0.001) return true;
    if (fabs(g_btnExportLogAnim.value - g_btnExportLogAnim.target) > 0.001) return true;
    return false;
}

void UpdateAnimations() {
    g_btnConnectAnim.setTarget(g_btnConnectHover ? 1.0 : 0.0);
    g_btnApplyAnim.setTarget(g_btnApplyHover ? 1.0 : 0.0);
    g_btnCloseAnim.setTarget(g_btnCloseHover ? 1.0 : 0.0);
    g_btnMinAnim.setTarget(g_btnMinHover ? 1.0 : 0.0);
    g_btnUpdateAnim.setTarget(g_btnUpdateHover ? 1.0 : 0.0);
    g_btnLaunchAnim.setTarget(g_btnLaunchHover ? 1.0 : 0.0);
    g_btnExportLogAnim.setTarget(g_btnExportLogHover ? 1.0 : 0.0);
    g_btnConnectAnim.update();
    g_btnApplyAnim.update();
    g_btnCloseAnim.update();
    g_btnMinAnim.update();
    g_btnUpdateAnim.update();
    g_btnLaunchAnim.update();
    g_btnExportLogAnim.update();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            MainDebugLog("[Main] WM_CREATE - Application starting");
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
            
            // Enable glass effect
            EnableBlurBehind(hwnd);
            
            // Create edit controls - positions must match OnPaint drawing
            // Note: IP and Port editing is done via config file
            // Click on IP/Port in settings tab shows a hint
            
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
            
            // Layout positions
            int leftColW = 260;
            int contentY = TITLEBAR_H + 20;
            int tabH = 32;
            int tabY = contentY + 10;  // Tabs inside card
            int rowY = contentY + 55;
            int updateBtnX = g_winW - 200;
            int checkboxX = CARD_PADDING + 18;
            
            bool oldConnect = g_btnConnectHover, oldApply = g_btnApplyHover;
            bool oldClose = g_btnCloseHover, oldMin = g_btnMinHover, oldUpdate = g_btnUpdateHover, oldLaunch = g_btnLaunchHover;
            bool oldExportLog = g_btnExportLogHover, oldAdmin = g_btnAdminHover;
            bool oldTabHover[2] = {g_tabHover[0], g_tabHover[1]};
            bool oldMkHover = g_moeKoeBoxHover, oldNcHover = g_neteaseBoxHover;
            
            // Tab hover detection
            int tabW = 80;
            for (int t = 0; t < 2; t++) {
                int tabX = CARD_PADDING + 18 + t * (tabW + 8);
                g_tabHover[t] = IsInRect(x, y, tabX, tabY, tabW, tabH);
            }
            
            // Button hover detection (Main Tab only)
            if (g_currentTab == 0) {
                int platformRowY = rowY;
                int btnY = rowY + 60;
                int launchBtnY = btnY + 55;
                
                // Platform box hover detection
                int boxW = (leftColW - 36 - 10) / 2;
                int box1X = CARD_PADDING + 18;
                int box2X = box1X + boxW + 10;
                g_moeKoeBoxHover = IsInRect(x, y, box1X, platformRowY + 22, boxW, 32);
                g_neteaseBoxHover = IsInRect(x, y, box2X, platformRowY + 22, boxW, 32);
                
                g_btnConnectHover = IsInRect(x, y, CARD_PADDING + 18, btnY, leftColW - 36, 42);
                g_btnLaunchHover = !g_neteaseConnected && IsInRect(x, y, CARD_PADDING + 18, launchBtnY, leftColW - 36, 36);
                g_btnExportLogHover = false;
                g_btnAdminHover = false;
            } else {
                g_btnConnectHover = false;
                g_btnLaunchHover = false;
                g_moeKoeBoxHover = false;
                g_neteaseBoxHover = false;
                // Settings tab - export log button and admin link
                // rowY starts at contentY + 55
                // IP: rowY, Port: rowY + 70, Perf: rowY + 135, AutoUpdate: rowY + 167, ShowPlatform: rowY + 199, Tray: rowY + 231, AutoStart: rowY + 263, RunAsAdmin: rowY + 295
                // Admin status: rowY + 330, Export button: rowY + 380
                int adminY = rowY + 70 + 65 + 32 + 32 + 32 + 32 + 35;  // RunAsAdmin + 35
                int exportLogY = adminY + 50;
                g_btnAdminHover = !IsRunningAsAdmin() && IsInRect(x, y, checkboxX, adminY, 150, 22);
                g_btnExportLogHover = IsInRect(x, y, checkboxX, exportLogY, leftColW - 36 - checkboxX, 32);
            }
            
            g_btnApplyHover = false;
            g_btnCloseHover = IsInRect(x, y, g_winW - 60, 14, 38, 38);
            g_btnMinHover = IsInRect(x, y, g_winW - 110, 14, 38, 38);
            g_btnUpdateHover = IsInRect(x, y, updateBtnX, 14, 80, 32);
            
            if (oldConnect != g_btnConnectHover || oldApply != g_btnApplyHover || 
                oldClose != g_btnCloseHover || oldMin != g_btnMinHover || oldUpdate != g_btnUpdateHover || 
                oldLaunch != g_btnLaunchHover || oldExportLog != g_btnExportLogHover ||
                oldTabHover[0] != g_tabHover[0] || oldTabHover[1] != g_tabHover[1] ||
                g_moeKoeBoxHover != oldMkHover || g_neteaseBoxHover != oldNcHover ||
                g_btnAdminHover != oldAdmin) {
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            
            // Update button click
            int updateBtnX = g_winW - 200;
            if (IsInRect(x, y, updateBtnX, 14, 80, 32)) {
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
                        SaveConfig(L"config_gui.json");
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
            for (int t = 0; t < 2; t++) {
                int tabX = CARD_PADDING + 18 + t * (tabW + 8);
                if (IsInRect(x, y, tabX, tabY, tabW, tabH)) {
                    g_currentTab = t;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            
            if (g_currentTab == 0) {
                // === MAIN TAB click handling ===
                int btnY = rowY + 55;
                int launchBtnY = btnY + 45;
                
                // Platform box clicks - manually switch active platform
                int boxW = (leftColW - 36 - 10) / 2;
                int box1X = CARD_PADDING + 18;
                int box2X = box1X + boxW + 10;
                
                // Click on MoeKoe box
                if (g_moeKoeConnected && IsInRect(x, y, box1X, rowY + 22, boxW, 32)) {
                    g_activePlatform = 0;
                    g_autoPlatformSwitch = false;  // Manual selection disables auto switch
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                // Click on Netease box
                if (g_neteaseConnected && IsInRect(x, y, box2X, rowY + 22, boxW, 32)) {
                    g_activePlatform = 1;
                    g_autoPlatformSwitch = false;  // Manual selection disables auto switch
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                
                // Connect button
                if (IsInRect(x, y, CARD_PADDING + 18, btnY, leftColW - 36, 38)) {
                    g_autoPlatformSwitch = true;  // Re-enable auto switch when reconnecting
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
        } else {
            // === SETTINGS TAB click handling ===
            // IP and Port input fields - show hint to edit config file
            int ipRowY = rowY;
            int portRowY = rowY + 75;
            int checkboxRowY = rowY + 75 + 70;
            int autoUpdateRowY = checkboxRowY + 38;
            int showPlatformRowY = autoUpdateRowY + 38;
            int trayRowY = showPlatformRowY + 38;
            int autoStartRowY = trayRowY + 38;
            int runAsAdminRowY = autoStartRowY + 38;
            int adminStatusY = runAsAdminRowY + 38;
            int exportLogRowY = adminStatusY + 55;
            
            // Click on IP or Port input field - show hint
            if (IsInRect(x, y, CARD_PADDING + 18, ipRowY + 24, leftColW - 36, 36) ||
                IsInRect(x, y, CARD_PADDING + 18, portRowY + 24, leftColW - 36, 36)) {
                ShowInfoDialog(L"\x63D0\x793A", L"\x8BF7\x7F16\x8F91 config_gui.json \x4FEE\x6539 IP \x548C\x7AEF\x53E3\n\x4FEE\x6539\x540E\x91CD\x65B0\x542F\x52A8\x7A0B\x5E8F\x751F\x6548");
                return 0;
            }
            // Checkbox for show performance
            if (IsInRect(x, y, checkboxX, checkboxRowY, checkboxSize, checkboxSize)) {
                g_showPerfOnPause = !g_showPerfOnPause;
                SaveConfig(L"config_gui.json");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for auto update
            if (IsInRect(x, y, checkboxX, autoUpdateRowY, checkboxSize, checkboxSize)) {
                g_autoUpdate = !g_autoUpdate;
                SaveConfig(L"config_gui.json");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for show platform
            if (IsInRect(x, y, checkboxX, showPlatformRowY, checkboxSize, checkboxSize)) {
                g_showPlatform = !g_showPlatform;
                SaveConfig(L"config_gui.json");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for minimize to tray
            if (IsInRect(x, y, checkboxX, trayRowY, checkboxSize, checkboxSize)) {
                g_minimizeToTray = !g_minimizeToTray;
                SaveConfig(L"config_gui.json");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for auto start
            if (IsInRect(x, y, checkboxX, autoStartRowY, checkboxSize, checkboxSize)) {
                g_autoStart = !g_autoStart;
                SetAutoStart(g_autoStart);
                SaveConfig(L"config_gui.json");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            // Checkbox for run as admin
            if (IsInRect(x, y, checkboxX, runAsAdminRowY, checkboxSize, checkboxSize)) {
                g_runAsAdmin = !g_runAsAdmin;
                SaveConfig(L"config_gui.json");
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
            // Export log button
            if (IsInRect(x, y, checkboxX, exportLogRowY, leftColW - 36 - checkboxX, 32)) {
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
        
        case WM_TIMER: {
            UpdateAnimations();
            UpdatePerfStats();
            
            // Smart redraw: only redraw when needed
            DWORD now = GetTickCount();
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
            return 0;
        }
        
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
            else if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                
                // Create a popup menu
                HMENU menu = CreatePopupMenu();
                
                AppendMenuW(menu, MF_STRING, 1, L"\x663E\x793A\x7A97\x53E3");  // 显示窗口
                AppendMenuW(menu, MF_STRING, 2, L"\x9000\x51FA\x7A0B\x5E8F");  // 退出程序
                
                // Must set foreground window before showing menu
                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
                DestroyMenu(menu);
                
                if (cmd == 1) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); }
                else if (cmd == 2) { 
                    // Exit directly
                    DestroyWindow(hwnd); 
                }
            }
            return 0;
        
        case WM_SIZE:
            g_winW = LOWORD(lParam);
            g_winH = HIWORD(lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        
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
                        SaveConfig(L"config_gui.json");
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        
        case WM_CLOSE:
            // Save window position and size before closing
            {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                g_winX = rc.left;
                g_winY = rc.top;
                g_winW = rc.right - rc.left;
                g_winH = rc.bottom - rc.top;
                SaveConfig(L"config_gui.json");
            }
            RemoveTrayIcon();
            DestroyWindow(hwnd);
            return 0;
        
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
    
    // Check if this is first run (config file doesn't exist)
    bool isFirstRun = (_waccess(L"config_gui.json", 0) == -1);
    if (isFirstRun) {
        g_minimizeToTray = ShowFirstRunDialog();
    }
    
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
    wc.lpszClassName = L"VRCLyricsDisplay_Class";
    RegisterClassExW(&wc);
    
    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, L"VRCLyricsDisplay_Class", L"",
        WS_POPUP | WS_CLIPCHILDREN | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, g_winW, g_winH, nullptr, nullptr, hInst, nullptr);
    
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
    return (int)msg.wParam;
}























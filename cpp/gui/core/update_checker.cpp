// update_checker.cpp - Update checking and installation
#include "update_checker.h"
#include "../common/utils.h"
#include "../common/logger.h"
#include "../ui/draw_helpers.h"
#include <winhttp.h>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

// External dependencies from main_gui.cpp
extern HWND g_hwnd;
extern std::wstring g_skipVersion;
extern std::string g_autoDetectedRepo;

// Version constants (defined in main_gui.cpp)
#define APP_VERSION_NUM 401
#define GITHUB_REPO "pcwl049/VRChat-lyrics-display"

// Global variable definitions
std::wstring g_latestVersion = L"";
std::wstring g_downloadUrl = L"";
std::wstring g_downloadSha256Url = L"";
std::wstring g_downloadSha256 = L"";
std::wstring g_latestChangelog = L"";
bool g_updateAvailable = false;
bool g_checkingUpdate = false;
bool g_downloadingUpdate = false;
bool g_manualCheckUpdate = false;
bool g_updateCheckComplete = false;
DWORD g_lastUpdateCheck = 0;
int g_downloadProgress = 0;

bool CheckForUpdate(bool manualCheck) {
    if (g_checkingUpdate) return false;
    g_checkingUpdate = true;
    g_manualCheckUpdate = manualCheck;
    g_latestChangelog.clear();
    
    HINTERNET hSession = WinHttpOpen(L"VRChatLyricsDisplay/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { g_checkingUpdate = false; g_updateCheckComplete = true; return false; }
    
    DWORD timeout = 5000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); g_checkingUpdate = false; g_updateCheckComplete = true; return false; }
    
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
        
        // Parse JSON for tag_name
        size_t tagPos = response.find("\"tag_name\"");
        if (tagPos != std::string::npos) {
            size_t start = response.find("\"", tagPos + 11) + 1;
            size_t end = response.find("\"", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string tag = response.substr(start, end - start);
                if (!tag.empty() && tag[0] == 'v') tag = tag.substr(1);
                
                int len = MultiByteToWideChar(CP_UTF8, 0, tag.c_str(), -1, NULL, 0);
                g_latestVersion.resize(len - 1);
                MultiByteToWideChar(CP_UTF8, 0, tag.c_str(), -1, &g_latestVersion[0], len);
                g_latestVersion.resize(len - 1);
                
                int latestVer = ParseVersion(g_latestVersion);
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
                                if (cp < 0x80) unescaped += (char)cp;
                                else if (cp < 0x800) {
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
        
        // Find download URLs
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

bool DownloadAndInstallUpdate() {
    if (g_downloadUrl.empty()) return false;
    
    g_downloadingUpdate = true;
    g_downloadProgress = 0;
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    wchar_t tempFile[MAX_PATH];
    swprintf_s(tempFile, L"%sVRCLyricsDisplay_new.exe", tempPath);
    
    HINTERNET hSession = WinHttpOpen(L"VRChatLyricsDisplay/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) { g_downloadingUpdate = false; return false; }
    
    DWORD timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    timeout = 60000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    
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
    
    DWORD contentLength = 0;
    DWORD sizeLen = sizeof(contentLength);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &sizeLen, WINHTTP_NO_HEADER_INDEX);
    
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
        DeleteFileW(tempFile);
        return false;
    }
    
    // Verify SHA256 if available
    if (!g_downloadSha256Url.empty()) {
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
        
        if (sha256Content.length() >= 64) {
            std::string expectedSha256 = sha256Content.substr(0, 64);
            for (char& c : expectedSha256) { if (c >= 'A' && c <= 'F') c += 32; }
            
            std::string actualSha256 = CalculateSHA256(tempFile);
            
            if (actualSha256 != expectedSha256) {
                DeleteFileW(tempFile);
                return false;
            }
        } else {
            LOG_ERROR("Update", "SHA256 checksum file download failed, aborting update for safety");
            DeleteFileW(tempFile);
            return false;
        }
    }
    
    // Create update batch script
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    wchar_t batchPath[MAX_PATH];
    swprintf_s(batchPath, L"%supdate_vrclayrics.bat", tempPath);
    FILE* batch = nullptr;
    if (_wfopen_s(&batch, batchPath, L"w") != 0 || !batch) {
        DeleteFileW(tempFile);
        return false;
    }
    
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
    fprintf(batch, "del \"%%~f0\"\n");
    fclose(batch);
    
    ShellExecuteW(NULL, L"open", batchPath, NULL, tempPath, SW_SHOWNORMAL);
    
    return true;
}

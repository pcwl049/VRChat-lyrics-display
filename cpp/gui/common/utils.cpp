// utils.cpp - Common utility functions
#include "utils.h"
#include <windows.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <cstdio>

// Default GitHub repository
#define GITHUB_REPO "pcwl049/VRChat-lyrics-display"

// Parse version string like "1.2.3" to number (10203)
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

// Auto-detect repository from Git config
std::string GetRepoFromGitConfig() {
    std::string repoPath = ".git/config";
    
    FILE* f = fopen(repoPath.c_str(), "r");
    if (!f) {
        return GITHUB_REPO;
    }
    
    char line[512];
    std::string originUrl = "";
    
    while (fgets(line, sizeof(line), f)) {
        std::string str = line;
        if (str.find("[remote \"origin\"]") != std::string::npos) {
            while (fgets(line, sizeof(line), f)) {
                std::string urlLine = line;
                if (urlLine.find("[") != std::string::npos) {
                    break;
                }
                if (urlLine.find("url") != std::string::npos) {
                    size_t pos = urlLine.find("=");
                    if (pos != std::string::npos) {
                        originUrl = urlLine.substr(pos + 1);
                        size_t start = originUrl.find_first_not_of(" \t\r\n");
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
        return GITHUB_REPO;
    }
    
    // Parse GitHub URL (https://github.com/owner/repo.git or git@github.com:owner/repo.git)
    std::string ownerRepo = "";
    
    if (originUrl.find("github.com") != std::string::npos) {
        size_t startPos = originUrl.find("github.com");
        if (startPos != std::string::npos) {
            startPos += 11;  // Skip "github.com"
            
            if (originUrl.find("https://") != std::string::npos) {
                startPos += 1;  // Skip "/"
            } else if (originUrl.find("git@") != std::string::npos) {
                startPos += 1;  // Skip ":"
            } else {
                startPos += 1;
            }
            
            ownerRepo = originUrl.substr(startPos);
            size_t gitPos = ownerRepo.find(".git");
            if (gitPos != std::string::npos) {
                ownerRepo = ownerRepo.substr(0, gitPos);
            }
        }
    }
    
    if (ownerRepo.empty()) {
        return GITHUB_REPO;
    }
    
    return ownerRepo;
}

// Build GitHub API URL
std::string GetGitHubApiUrl(const std::string& repo) {
    return "https://api.github.com/repos/" + repo + "/releases/latest";
}

// Check if Netease Cloud Music process is running
bool IsNeteaseRunning() {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return false;
    
    bool found = false;
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"cloudmusic.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
    return found;
}

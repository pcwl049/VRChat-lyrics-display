// hardware_detect.cpp - Hardware detection functions
#include "hardware_detect.h"
#include <windows.h>
#include <dxgi1_4.h>
#include <string>

#pragma comment(lib, "dxgi.lib")

std::wstring DetectCpuName() {
    std::wstring cpuName = L"CPU";
    
    // Read CPU name from registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, 
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t value[256] = {0};
        DWORD size = sizeof(value);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, 
                             (LPBYTE)value, &size) == ERROR_SUCCESS) {
            cpuName = value;
        }
        RegCloseKey(hKey);
    }
    
    // Extract core model
    // Intel: Intel(R) Core(TM) i5-12600KF CPU @ 3.70GHz
    // AMD: AMD Ryzen 7 5800X 8-Core Processor
    
    std::wstring result;
    
    // Find Intel i3/i5/i7/i9 pattern
    size_t intelPos = cpuName.find(L"i3");
    if (intelPos == std::wstring::npos) intelPos = cpuName.find(L"i5");
    if (intelPos == std::wstring::npos) intelPos = cpuName.find(L"i7");
    if (intelPos == std::wstring::npos) intelPos = cpuName.find(L"i9");
    
    if (intelPos != std::wstring::npos) {
        size_t endPos = cpuName.find(L' ', intelPos);
        if (endPos == std::wstring::npos) endPos = cpuName.length();
        result = cpuName.substr(intelPos, endPos - intelPos);
    }
    
    // AMD Ryzen pattern
    if (result.empty()) {
        size_t amdPos = cpuName.find(L"Ryzen");
        if (amdPos != std::wstring::npos) {
            size_t start = amdPos;
            size_t endPos = cpuName.find(L"-Core", start);
            if (endPos == std::wstring::npos) {
                size_t space1 = cpuName.find(L' ', start);
                if (space1 != std::wstring::npos) {
                    size_t space2 = cpuName.find(L' ', space1 + 1);
                    if (space2 != std::wstring::npos) {
                        size_t space3 = cpuName.find(L' ', space2 + 1);
                        if (space3 != std::wstring::npos) {
                            endPos = space3;
                        } else {
                            endPos = cpuName.length();
                        }
                    }
                }
            }
            if (endPos != std::wstring::npos && endPos > start) {
                result = cpuName.substr(start, endPos - start);
                std::wstring tmp;
                for (wchar_t c : result) {
                    if (c != L' ') tmp += c;
                }
                result = tmp;
            }
        }
    }
    
    // Simple extraction fallback
    if (result.empty()) {
        std::wstring prefixes[] = {
            L"Intel(R) ", L"Intel ", L"AMD ", L"CPU ", L"Processor ", L"(R) ", L"(TM) "
        };
        for (const auto& prefix : prefixes) {
            size_t pos = cpuName.find(prefix);
            if (pos != std::wstring::npos) {
                cpuName.erase(pos, prefix.length());
            }
        }
        size_t spacePos = cpuName.find(L' ');
        if (spacePos != std::wstring::npos) {
            result = cpuName.substr(0, spacePos);
        } else {
            result = cpuName;
        }
    }
    
    // Limit length to 10 chars
    if (result.length() > 10) {
        result = result.substr(0, 10);
    }
    
    return result.empty() ? L"CPU" : result;
}

std::wstring DetectGpuName() {
    std::wstring gpuName = L"GPU";
    
    // Use DXGI to get GPU name
    IDXGIFactory1* pFactory = nullptr;
    if (CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory) == S_OK) {
        IDXGIAdapter1* pAdapter = nullptr;
        for (UINT i = 0; pFactory->EnumAdapters1(i, &pAdapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc;
            if (pAdapter->GetDesc1(&desc) == S_OK) {
                // Skip software renderer (Microsoft Basic Render)
                if (desc.VendorId != 0x1414) {
                    gpuName = desc.Description;
                    pAdapter->Release();
                    break;
                }
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
    
    // Extract core model
    // NVIDIA: NVIDIA GeForce RTX 4070 SUPER
    // AMD: AMD Radeon RX 7800 XT, AMD Radeon RX Vega 56, AMD Radeon VII
    // Intel: Intel(R) Arc(R) A770 Graphics
    
    std::wstring result;
    
    // Find NVIDIA RTX/GTX pattern
    size_t rtxPos = gpuName.find(L"RTX");
    size_t gtxPos = gpuName.find(L"GTX");
    
    if (rtxPos != std::wstring::npos) {
        std::wstring sub = gpuName.substr(rtxPos);
        for (wchar_t c : sub) {
            if (c != L' ') result += c;
        }
    } else if (gtxPos != std::wstring::npos) {
        std::wstring sub = gpuName.substr(gtxPos);
        for (wchar_t c : sub) {
            if (c != L' ') result += c;
        }
    }
    
    // AMD pattern
    if (result.empty()) {
        size_t rxPos = gpuName.find(L"RX ");
        if (rxPos != std::wstring::npos) {
            std::wstring sub = gpuName.substr(rxPos);
            for (wchar_t c : sub) {
                if (c != L' ') result += c;
            }
        }
    }
    
    if (result.empty()) {
        size_t vegaPos = gpuName.find(L"Vega");
        if (vegaPos != std::wstring::npos) {
            std::wstring sub = gpuName.substr(vegaPos);
            for (wchar_t c : sub) {
                if (c != L' ') result += c;
            }
        }
    }
    
    if (result.empty()) {
        size_t viiPos = gpuName.find(L"Radeon VII");
        if (viiPos != std::wstring::npos) {
            result = L"RadeonVII";
        }
    }
    
    if (result.empty()) {
        size_t r7Pos = gpuName.find(L"R7 ");
        size_t r9Pos = gpuName.find(L"R9 ");
        if (r7Pos != std::wstring::npos) {
            std::wstring sub = gpuName.substr(r7Pos);
            for (wchar_t c : sub) {
                if (c != L' ') result += c;
            }
        } else if (r9Pos != std::wstring::npos) {
            std::wstring sub = gpuName.substr(r9Pos);
            for (wchar_t c : sub) {
                if (c != L' ') result += c;
            }
        }
    }
    
    // Intel Arc pattern
    if (result.empty()) {
        size_t arcPos = gpuName.find(L"Arc");
        if (arcPos != std::wstring::npos) {
            std::wstring sub = gpuName.substr(arcPos);
            for (wchar_t c : sub) {
                if (c != L' ') result += c;
            }
        }
    }
    
    // Fallback: remove prefixes
    if (result.empty()) {
        std::wstring prefixes[] = {
            L"NVIDIA ", L"GeForce ", L"AMD ", L"Radeon ", L"ATI ", 
            L"Intel(R) ", L"Intel ", L"Arc(R) "
        };
        for (const auto& prefix : prefixes) {
            size_t pos = gpuName.find(prefix);
            if (pos != std::wstring::npos) {
                gpuName.erase(pos, prefix.length());
            }
        }
        for (wchar_t c : gpuName) {
            if (c != L' ') result += c;
        }
    }
    
    // Limit length to 10 chars
    if (result.length() > 10) {
        result = result.substr(0, 10);
    }
    
    return result.empty() ? L"GPU" : result;
}

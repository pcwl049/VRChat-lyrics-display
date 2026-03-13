// config_manager.cpp - 配置文件管理
#include "config_manager.h"
#include "config.h"
#include "theme.h"
#include "logger.h"
#include "types.h"
#include <windows.h>
#include <cstdio>
#include <vector>

// 外部依赖的全局变量（定义在 main_gui.cpp）
extern std::wstring g_oscIp;
extern int g_oscPort;
extern int g_moekoePort;
extern bool g_oscEnabled;
extern bool g_minimizeToTray;
extern bool g_startMinimized;
extern bool g_showPerfOnPause;
extern bool g_autoUpdate;
extern bool g_showPlatform;
extern bool g_autoStart;
extern bool g_runAsAdmin;
extern int g_performanceMode;
extern bool g_minimalMode;
extern int g_winW, g_winH;
extern int g_winX, g_winY;
extern std::wstring g_cpuDisplayName;
extern std::wstring g_ramDisplayName;
extern std::wstring g_gpuDisplayName;
extern UINT g_oscPauseHotkey;
extern UINT g_oscPauseHotkeyMods;
extern std::wstring g_skipVersion;
extern std::vector<DisplayModule> g_displayModules;
extern Animation g_systemInfoExpandAnim;
extern std::vector<Animation> g_moduleExpandAnims;

// 常量
const int WIN_W_MIN = 400;
const int WIN_H_MIN = 300;

// 前向声明
void UpdateDisplayModuleAvailability();

// 初始化默认显示模块配置
void InitDefaultDisplayModules() {
    g_displayModules.clear();
    
    // CPU 模块
    DisplayModule cpuMod;
    cpuMod.key = L"cpu";
    cpuMod.name = g_cpuDisplayName;
    cpuMod.enabled = true;
    cpuMod.expanded = false;
    cpuMod.enabledCount = 1;
    cpuMod.subModules = {
        {SUBMOD_USAGE, L"使用率", true, true},    // CPU使用率始终可用
        {SUBMOD_TEMP, L"温度", false, false}      // 温度需要检测
    };
    g_displayModules.push_back(cpuMod);
    
    // GPU 模块
    DisplayModule gpuMod;
    gpuMod.key = L"gpu";
    gpuMod.name = g_gpuDisplayName;
    gpuMod.enabled = true;
    gpuMod.expanded = false;
    gpuMod.enabledCount = 2;
    gpuMod.subModules = {
        {SUBMOD_USAGE, L"使用率", false, true},   // 需要NVML/ADL
        {SUBMOD_VRAM, L"显存占用", true, true},   // DXGI始终可用
        {SUBMOD_TEMP, L"温度", false, false}      // 需要NVML
    };
    g_displayModules.push_back(gpuMod);
    
    // RAM 模块
    DisplayModule ramMod;
    ramMod.key = L"ram";
    ramMod.name = g_ramDisplayName;
    ramMod.enabled = true;
    ramMod.expanded = false;
    ramMod.enabledCount = 1;
    ramMod.subModules = {
        {SUBMOD_USAGE, L"使用量", true, true},    // RAM使用量始终可用
        {SUBMOD_TEMP, L"温度", false, false}      // 温度需要检测
    };
    g_displayModules.push_back(ramMod);
    
    // 初始化展开动画
    g_moduleExpandAnims.resize(g_displayModules.size());
    for (auto& anim : g_moduleExpandAnims) {
        anim.value = 0.0;
        anim.target = 0.0;
        anim.speed = 0.12;
    }
}

// 加载无歌词提示消息
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
        // Unescape basic JSON escape sequences
        std::string unescaped;
        for (size_t i = 0; i < val.size(); i++) {
            if (val[i] == '\\' && i + 1 < val.size()) {
                char next = val[i + 1];
                if (next == 'n') { unescaped += '\n'; i++; }
                else if (next == 'r') { unescaped += '\r'; i++; }
                else if (next == 't') { unescaped += '\t'; i++; }
                else if (next == '"') { unescaped += '"'; i++; }
                else if (next == '\\') { unescaped += '\\'; i++; }
                else { unescaped += val[i]; }
            } else {
                unescaped += val[i];
            }
        }
        int wlen = MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring wval(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, unescaped.c_str(), -1, &wval[0], wlen);
        return wval;
    };
    
    auto getInt = [&](const char* key, int def) -> int {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return def;
        pos = content.find(':', pos);
        if (pos == std::string::npos) return def;
        // Skip whitespace
        while (pos < content.size() && (content[pos] == ':' || content[pos] == ' ' || content[pos] == '\t' || content[pos] == '\n' || content[pos] == '\r')) {
            pos++;
        }
        if (pos >= content.size()) return def;
        return atoi(content.c_str() + pos);
    };
    
    auto getBool = [&](const char* key, bool def) -> bool {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = content.find(search);
        if (pos == std::string::npos) return def;
        // Find the colon after the key
        size_t colonPos = content.find(':', pos);
        if (colonPos == std::string::npos) return def;
        // Find the value after the colon (skip whitespace)
        size_t valStart = colonPos + 1;
        while (valStart < content.size() && (content[valStart] == ' ' || content[valStart] == '\t' || content[valStart] == '\n' || content[valStart] == '\r')) {
            valStart++;
        }
        if (valStart >= content.size()) return def;
        // Check for "true" at valStart
        if (valStart + 4 <= content.size() && content.compare(valStart, 4, "true") == 0) {
            return true;
        }
        // Check for "false" at valStart
        if (valStart + 5 <= content.size() && content.compare(valStart, 5, "false") == 0) {
            return false;
        }
        return def;  // Value not found or invalid
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
    g_minimalMode = getBool("minimal_mode", false);  // 极简模式
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
    
    // Load CPU/GPU display names
    std::wstring cpuName = getStr("cpu_name");
    if (!cpuName.empty()) g_cpuDisplayName = cpuName;
    std::wstring gpuName = getStr("gpu_name");
    if (!gpuName.empty()) g_gpuDisplayName = gpuName;
    std::wstring ramName = getStr("ram_name");
    if (!ramName.empty()) g_ramDisplayName = ramName;
    
    // Initialize display modules with default config
    InitDefaultDisplayModules();
    
    // Load display_modules configuration from JSON
    {
        size_t dmPos = content.find("\"display_modules\"");
        if (dmPos != std::string::npos) {
            // 找到 display_modules 数组（新格式）
            size_t arrStart = content.find('[', dmPos);
            if (arrStart != std::string::npos) {
                // 找到匹配的数组结束括号
                int bracketCount = 1;
                size_t arrEnd = arrStart + 1;
                while (arrEnd < content.size() && bracketCount > 0) {
                    if (content[arrEnd] == '[') bracketCount++;
                    else if (content[arrEnd] == ']') bracketCount--;
                    arrEnd++;
                }
                
                std::string arrContent = content.substr(arrStart, arrEnd - arrStart);
                
                // 解析每个模块对象
                std::vector<DisplayModule> loadedModules;
                size_t searchPos = 0;
                
                while ((searchPos = arrContent.find("{", searchPos)) != std::string::npos) {
                    // 找到模块对象的结束
                    int braceCount = 1;
                    size_t objEnd = searchPos + 1;
                    while (objEnd < arrContent.size() && braceCount > 0) {
                        if (arrContent[objEnd] == '{') braceCount++;
                        else if (arrContent[objEnd] == '}') braceCount--;
                        objEnd++;
                    }
                    
                    std::string modContent = arrContent.substr(searchPos, objEnd - searchPos);
                    
                    // 解析 key
                    DisplayModule mod;
                    size_t keyPos = modContent.find("\"key\"");
                    if (keyPos != std::string::npos) {
                        size_t keyStart = modContent.find('"', keyPos + 6);
                        if (keyStart != std::string::npos) {
                            size_t keyEnd = modContent.find('"', keyStart + 1);
                            if (keyEnd != std::string::npos) {
                                std::string keyVal = modContent.substr(keyStart + 1, keyEnd - keyStart - 1);
                                int wlen = MultiByteToWideChar(CP_UTF8, 0, keyVal.c_str(), -1, nullptr, 0);
                                if (wlen > 0) {
                                    std::wstring wkey(wlen - 1, 0);
                                    MultiByteToWideChar(CP_UTF8, 0, keyVal.c_str(), -1, &wkey[0], wlen);
                                    mod.key = wkey;
                                }
                            }
                        }
                    }
                    
                    // 解析 enabled
                    size_t enabledPos = modContent.find("\"enabled\"");
                    if (enabledPos != std::string::npos) {
                        size_t colonPos = modContent.find(':', enabledPos);
                        if (colonPos != std::string::npos) {
                            size_t valStart = colonPos + 1;
                            while (valStart < modContent.size() && (modContent[valStart] == ' ' || modContent[valStart] == '\t')) valStart++;
                            if (valStart + 4 <= modContent.size() && modContent.compare(valStart, 4, "true") == 0) {
                                mod.enabled = true;
                            } else if (valStart + 5 <= modContent.size() && modContent.compare(valStart, 5, "false") == 0) {
                                mod.enabled = false;
                            }
                        }
                    }
                    
                    // 解析 sub_modules 数组
                    size_t subArrPos = modContent.find("\"sub_modules\"");
                    if (subArrPos != std::string::npos) {
                        size_t subArrStart = modContent.find('[', subArrPos);
                        if (subArrStart != std::string::npos) {
                            int subBracketCount = 1;
                            size_t subArrEnd = subArrStart + 1;
                            while (subArrEnd < modContent.size() && subBracketCount > 0) {
                                if (modContent[subArrEnd] == '[') subBracketCount++;
                                else if (modContent[subArrEnd] == ']') subBracketCount--;
                                subArrEnd++;
                            }
                            
                            std::string subArr = modContent.substr(subArrStart, subArrEnd - subArrStart);
                            
                            size_t subSearchPos = 0;
                            while ((subSearchPos = subArr.find("{", subSearchPos)) != std::string::npos) {
                                size_t subObjEnd = subArr.find("}", subSearchPos);
                                if (subObjEnd == std::string::npos) break;
                                
                                std::string subObj = subArr.substr(subSearchPos, subObjEnd - subSearchPos + 1);
                                
                                SubModuleInfo subMod;
                                size_t typePos = subObj.find("\"type\"");
                                if (typePos != std::string::npos) {
                                    size_t typeStart = subObj.find('"', typePos + 6);
                                    if (typeStart != std::string::npos) {
                                        size_t typeEnd = subObj.find('"', typeStart + 1);
                                        if (typeEnd != std::string::npos) {
                                            std::string typeVal = subObj.substr(typeStart + 1, typeEnd - typeStart - 1);
                                            if (typeVal == "usage") subMod.type = SUBMOD_USAGE;
                                            else if (typeVal == "temp") subMod.type = SUBMOD_TEMP;
                                            else if (typeVal == "vram") subMod.type = SUBMOD_VRAM;
                                        }
                                    }
                                }
                                
                                size_t subEnabledPos = subObj.find("\"enabled\"");
                                if (subEnabledPos != std::string::npos) {
                                    size_t colonPos = subObj.find(':', subEnabledPos);
                                    if (colonPos != std::string::npos) {
                                        size_t valStart = colonPos + 1;
                                        while (valStart < subObj.size() && (subObj[valStart] == ' ' || subObj[valStart] == '\t')) valStart++;
                                        if (valStart + 4 <= subObj.size() && subObj.compare(valStart, 4, "true") == 0) {
                                            subMod.enabled = true;
                                        } else if (valStart + 5 <= subObj.size() && subObj.compare(valStart, 5, "false") == 0) {
                                            subMod.enabled = false;
                                        }
                                    }
                                }
                                
                                switch (subMod.type) {
                                    case SUBMOD_USAGE: subMod.name = L"使用率"; break;
                                    case SUBMOD_TEMP: subMod.name = L"温度"; break;
                                    case SUBMOD_VRAM: subMod.name = L"显存"; break;
                                }
                                
                                mod.subModules.push_back(subMod);
                                subSearchPos = subObjEnd + 1;
                            }
                        }
                    }
                    
                    if (!mod.key.empty()) {
                        loadedModules.push_back(mod);
                    }
                    
                    searchPos = objEnd + 1;
                }
                
                if (!loadedModules.empty()) {
                    g_displayModules = loadedModules;
                }
            }
        }
    }
    
    // 同步名称到模块
    for (auto& mod : g_displayModules) {
        if (mod.key == L"cpu") mod.name = g_cpuDisplayName;
        else if (mod.key == L"gpu") mod.name = g_gpuDisplayName;
        else if (mod.key == L"ram") mod.name = g_ramDisplayName;
        
        // 设置子项默认可用性（配置文件不保存 available 字段）
        for (auto& sub : mod.subModules) {
            // 根据类型设置默认可用性
            if (mod.key == L"cpu") {
                sub.available = (sub.type == SUBMOD_USAGE);  // CPU使用率始终可用
            } else if (mod.key == L"gpu") {
                sub.available = (sub.type == SUBMOD_VRAM);   // 显存始终可用（DXGI）
            } else if (mod.key == L"ram") {
                sub.available = (sub.type == SUBMOD_USAGE);  // RAM使用量始终可用
            }
        }
    }
    
    // 异步更新温度等传感器的可用性
    UpdateDisplayModuleAvailability();
    
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

// Escape special characters for JSON string values
std::string JsonEscape(const std::wstring& wstr) {
    // Convert to UTF-8 first
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string utf8(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    
    // Escape special characters
    std::string result;
    result.reserve(utf8.size() * 2);
    for (char c : utf8) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

void SaveConfig(const wchar_t* path) {
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    fprintf(f, "{\n  \"osc\": {\n    \"ip\": \"%s\",\n    \"port\": %d\n  },\n", JsonEscape(g_oscIp).c_str(), g_oscPort);
    fprintf(f, "  \"moekoe_port\": %d,\n  \"osc_enabled\": %s,\n  \"minimize_to_tray\": %s,\n  \"start_minimized\": %s,\n  \"show_perf_on_pause\": %s,\n  \"auto_update\": %s,\n  \"show_platform\": %s,\n  \"dark_mode\": %s,\n  \"auto_start\": %s,\n  \"run_as_admin\": %s,\n  \"performance_mode\": %d,\n  \"minimal_mode\": %s,\n",
            g_moekoePort, g_oscEnabled ? "true" : "false", g_minimizeToTray ? "true" : "false",
            g_startMinimized ? "true" : "false", g_showPerfOnPause ? "true" : "false",
            g_autoUpdate ? "true" : "false", g_showPlatform ? "true" : "false",
            "true", g_autoStart ? "true" : "false", g_runAsAdmin ? "true" : "false", g_performanceMode,
            g_minimalMode ? "true" : "false");  // g_darkMode固定为true
    fprintf(f, "  \"cpu_name\": \"%s\",\n  \"gpu_name\": \"%s\",\n  \"ram_name\": \"%s\",\n",
            JsonEscape(g_cpuDisplayName).c_str(), JsonEscape(g_gpuDisplayName).c_str(), JsonEscape(g_ramDisplayName).c_str());
    
    // Save display_modules
    fprintf(f, "  \"display_modules\": [\n");
    for (size_t i = 0; i < g_displayModules.size(); i++) {
        const auto& mod = g_displayModules[i];
        fprintf(f, "    {\n");
        fprintf(f, "      \"key\": \"%s\",\n", JsonEscape(mod.key).c_str());
        fprintf(f, "      \"enabled\": %s,\n", mod.enabled ? "true" : "false");
        fprintf(f, "      \"sub_modules\": [\n");
        for (size_t j = 0; j < mod.subModules.size(); j++) {
            const char* subName = "";
            switch (mod.subModules[j].type) {
                case SUBMOD_USAGE: subName = "usage"; break;
                case SUBMOD_TEMP: subName = "temp"; break;
                case SUBMOD_VRAM: subName = "vram"; break;
            }
            fprintf(f, "        {\"type\": \"%s\", \"enabled\": %s}", subName, mod.subModules[j].enabled ? "true" : "false");
            if (j < mod.subModules.size() - 1) fprintf(f, ",\n");
            else fprintf(f, "\n");
        }
        fprintf(f, "      ]\n");
        fprintf(f, "    }%s\n", (i < g_displayModules.size() - 1) ? "," : "");
    }
    fprintf(f, "  ],\n");
    
    fprintf(f, "  \"win_width\": %d,\n  \"win_height\": %d,\n  \"win_x\": %d,\n  \"win_y\": %d,\n",
            g_winW, g_winH, g_winX, g_winY);
    fprintf(f, "  \"osc_pause_hotkey\": %d,\n  \"osc_pause_hotkey_mods\": %d,\n",
            g_oscPauseHotkey, g_oscPauseHotkeyMods);
    fprintf(f, "  \"skip_version\": \"%s\"\n}\n", JsonEscape(g_skipVersion).c_str());
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

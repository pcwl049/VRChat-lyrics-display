# 修改草稿 - 待审核

## 1. 去掉深浅色主题切换功能

### 文件：`gui/main_gui.cpp`

#### 修改点 A：移除配置保存中的 dark_mode 字段
```cpp
// 修改前 (SaveConfig 函数)：
fprintf(f, "  \"dark_mode\": %s,\n  \"auto_start\": %s,\n",
        g_darkMode ? "true" : "false", g_autoStart ? "true" : "false");

// 修改后：
fprintf(f, "  \"auto_start\": %s,\n", g_autoStart ? "true" : "false");
```

#### 修改点 B：简化主题颜色初始化
```cpp
// 修改前：
void UpdateThemeColors() {
    if (g_darkMode) {
        // Dark theme...
    } else {
        // Light theme...
    }
}

// 修改后：只保留深色主题
void UpdateThemeColors() {
    // Dark theme (毛玻璃深空蓝主题)
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
    g_colors.glassAlpha = 100;
}

// 颜色宏也相应简化：
#define COLOR_EDIT_FOCUSED RGB(60, 60, 90)
#define COLOR_BTN_BG RGB(40, 40, 55)
#define COLOR_BTN_HOVER RGB(55, 55, 75)
// ...其他宏类似处理
```

#### 修改点 C：简化毛玻璃效果函数
```cpp
// 修改前：
if (g_darkMode) {
    // Dark mode: 深色毛玻璃
    ACCENT_POLICY policy = { ... };
} else {
    // Light mode: 浅色毛玻璃
    ACCENT_POLICY policy = { ... };
}

// 修改后：只保留深色模式
ACCENT_POLICY policy = { 4, 2, static_cast<int>((COLOR_GLASS_TINT & 0xFFFFFF) | (GLASS_ALPHA << 24)), 0 };
```

---

## 2. 修复 MoeKoe WebSocket 调试日志路径硬编码问题

### 文件：`gui/moekoe_ws.cpp`

#### 问题：原代码使用硬编码路径 `C:\temp\xxx`，如果目录不存在会导致日志写入失败

#### 修改点 A：新增辅助函数
```cpp
// 新增：获取调试日志路径的辅助函数
static std::string GetDebugLogPath(const char* filename) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string path = std::string(tempPath) + filename;
    return path;
}
```

#### 修改点 B：修复 connect() 函数中的日志路径
```cpp
// 修改前：
FILE* f = fopen("C:\\temp\\debug_ws.txt", "a");

// 修改后：
std::string logPath = GetDebugLogPath("moekoe_connect.log");
FILE* f = fopen(logPath.c_str(), "a");
```

#### 修改点 C：修复 run() 函数中的日志路径
```cpp
// 修改前：
FILE* f = fopen("C:\\temp\\debug_ws.txt", "a");

// 修改后：
std::string logPath = GetDebugLogPath("moekoe_run.log");
FILE* f = fopen(logPath.c_str(), "a");
```

#### 修改点 D：修复 parseKRC() 函数中的日志路径
```cpp
// 修改前：
FILE* f = fopen("C:\\temp\\debug_krc.txt", "a");

// 修改后：
std::string krcLogPath = GetDebugLogPath("moekoe_krc.log");
FILE* f = fopen(krcLogPath.c_str(), "a");
```

#### 修改点 E：修复 parseMessage() 函数中的日志路径
```cpp
// 修改前：
FILE* f = fopen("C:\\temp\\debug_ws.txt", "a");

// 修改后：
std::string logPath = GetDebugLogPath("moekoe_debug.log");
FILE* f = fopen(logPath.c_str(), "a");
```

#### 修改点 F：修复 OSCSender::sendChatbox() 函数中的日志路径
```cpp
// 修改前：
FILE* f = fopen("C:\\temp\\debug_osc.txt", "a");

// 修改后：
std::string oscLogPath = GetDebugLogPath("moekoe_osc.log");
FILE* f = fopen(oscLogPath.c_str(), "a");
```

---

## 3. 未修改但已检查的文件

- `gui/netease_ws.cpp` - 已正确使用 `%TEMP%` 目录，无需修改
- `gui/smtc_client.cpp` - 代码正确，无问题

---

## 审核状态

- [ ] 修改点待审核
- [ ] 测试通过后请删除此文件

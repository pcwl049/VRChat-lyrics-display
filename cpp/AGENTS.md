# VRChat Lyrics Display - 项目上下文

## 项目概述

VRChat Lyrics Display 是一个 Windows 桌面应用程序，用于将音乐歌词和系统性能信息通过 OSC 协议发送到 VRChat 聊天框。支持多种音乐平台，具有现代化的毛玻璃风格 UI。

### 主要功能
- **多平台音乐支持**: MoeKoeMusic、网易云音乐、QQ音乐 (SMTC)
- **OSC 集成**: 向 VRChat 发送歌词和性能数据
- **系统性能监控**: CPU、内存、GPU 使用率显示
- **现代化 UI**: 无边框窗口、毛玻璃效果、流畅动画
- **自动更新**: 从 GitHub Releases 检查并下载更新

---

## 技术栈

- **语言**: C++20
- **UI 框架**: Win32 API + GDI+ (自绘界面)
- **网络**: WinHTTP (HTTP/WebSocket), WinSock2 (OSC)
- **系统集成**: WinRT SMTC (System Media Transport Controls)
- **视觉效果**: DWM API (毛玻璃效果)

---

## 项目结构

```
cpp/
├── gui/                        # GUI 源码目录
│   ├── main_gui.cpp           # 主程序入口 (8378+ 行)
│   ├── glass_window.cpp/h     # 毛玻璃效果窗口组件
│   ├── moekoe_ws.cpp/h        # MoeKoeMusic WebSocket 客户端
│   ├── netease_ws.cpp/h       # 网易云音乐 CDP 客户端
│   ├── smtc_client.cpp/h      # SMTC 客户端 (QQ音乐等)
│   ├── compile_gui.bat        # 编译脚本
│   └── config_gui.json        # 默认配置文件
├── config_gui.json            # 用户配置文件
├── VRCLyricsDisplay.exe       # 编译后的可执行文件
└── backup/                    # 自动备份目录
```

### 核心模块

| 文件 | 功能 |
|------|------|
| `main_gui.cpp` | 主窗口、UI渲染、动画系统、配置管理 |
| `glass_window.cpp` | 毛玻璃效果歌词显示窗口 |
| `moekoe_ws.cpp` | MoeKoeMusic WebSocket 连接 (端口 6520) |
| `netease_ws.cpp` | 网易云音乐 Chrome DevTools Protocol |
| `smtc_client.cpp` | Windows SMTC 媒体控制接口 |

---

## 编译说明

### 依赖项
- **Visual Studio 2022** (需要 vcvars64.bat)
- **MSVC C++20** 编译器
- **Windows SDK** (WinRT, DWM, GDI+)

### 编译命令

使用提供的批处理脚本编译:

```batch
cd gui
compile_gui.bat
```

### 手动编译

```batch
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"

cl /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /await ^
    main_gui.cpp glass_window.cpp moekoe_ws.cpp netease_ws.cpp smtc_client.cpp ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib ^
    gdiplus.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib dxgi.lib ^
    /link /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
```

### 链接库
- `runtimeobject.lib` + `windowsapp.lib` - WinRT/SMTC 支持
- `dwmapi.lib` - 毛玻璃效果
- `ws2_32.lib` - WinSock2 (OSC)
- `winhttp.lib` - HTTP/WebSocket 请求
- `gdiplus.lib` - GDI+ 图形渲染
- `dxgi.lib` - GPU 信息获取

---

## 配置文件

`config_gui.json` 结构:

```json
{
  "osc": {
    "ip": "127.0.0.1",      // VRChat OSC 目标 IP
    "port": 9000              // VRChat OSC 目标端口
  },
  "moekoe_port": 6520,        // MoeKoeMusic WebSocket 端口
  "osc_enabled": true,        // 是否启用 OSC 发送
  "minimize_to_tray": true,   // 最小化到托盘
  "start_minimized": false,   // 启动时最小化
  "show_perf_on_pause": true, // 暂停时显示性能信息
  "auto_update": true,        // 自动检查更新
  "show_platform": true,      // 显示音乐平台名称
  "auto_start": false,        // 开机自启
  "perf_monitor_mode": true,  // 性能监控模式
  "cpu_name": "Intel i5",     // CPU 显示名称
  "ram_name": "31 GB",        // 内存显示名称
  "gpu_name": "RTX 4070",     // GPU 显示名称
  "display_modules": [...],   // 性能模块显示配置
  "win_width/height": 650,    // 窗口尺寸
  "win_x/y": -1               // 窗口位置 (-1 为居中)
}
```

---

## 动画系统

项目实现了完整的动画框架:

```cpp
// 核心动画结构
struct Animation {
    double value = 0.0, target = 0.0, speed = 0.25;
    void update() { value += (target - value) * speed; }
    void setTarget(double t) { target = t; }
    bool isActive() const { return fabs(target - value) > 0.001; }
};

// 平滑值过渡
struct SmoothValue {
    double value = 0.0, target = 0.0, speed = 0.15;
    void update() { value += (target - value) * speed; }
};
```

### 动画实例
| 变量名 | 用途 |
|--------|------|
| `g_windowFadeAnim` | 窗口淡入 |
| `g_tabSlideAnim` | 标签页滑动 |
| `g_menuExpandAnim` | 下拉菜单展开 |
| `g_connectPulseAnim` | 连接状态脉冲 |
| `g_charProgressAnim` | 字符数进度条 |

---

## 字体定义

UI 字体规格 (L4200 附近定义):

| 变量名 | 大小 | 粗细 | 用途 |
|--------|------|------|------|
| `g_fontTitle` | 48px | Bold | 标题 |
| `g_fontSubtitle` | 32px | SemiBold | 副标题 |
| `g_fontNormal` | 26px | Normal | 正文 |
| `g_fontSmall` | 22px | Normal | 小字 |
| `g_fontLyric` | 28px | Normal | 歌词显示 |
| `g_fontLabel` | 24px | SemiBold | 标签 |

字体名称: `Microsoft YaHei UI`

---

## 颜色主题

固定深色主题 (毛玻璃深空蓝):

```cpp
g_colors.bgStart = RGB(18, 18, 24);       // 背景渐变起始
g_colors.bgEnd = RGB(18, 18, 24);         // 背景渐变结束
g_colors.card = RGB(30, 40, 60);          // 卡片背景
g_colors.cardBorder = RGB(50, 70, 100);   // 卡片边框
g_colors.accent = RGB(80, 180, 255);      // 强调色
g_colors.text = RGB(240, 245, 255);       // 主文本
g_colors.textDim = RGB(140, 150, 170);    // 次要文本
g_colors.glassAlpha = 100;                // 毛玻璃透明度
```

---

## 开发约定

### 代码风格
- 使用 C++20 标准 (`/std:c++20`)
- UTF-8 编码 (`/utf-8`)
- Windows 子系统 (`/SUBSYSTEM:WINDOWS`)
- 宽字符字符串 (`wchar_t`, `std::wstring`)

### 日志系统
日志文件位置: `%TEMP%\vrclayrics_debug.log`
- 最大文件大小: 10MB
- 自动轮转: 保留 5 个历史文件
- 级别: DEBUG, INFO, WARNING, ERROR

```cpp
LOG_INFO("Message");
LOG_WARNING("Warning message");
LOG_ERROR("Error message");
```

### 关键常量
```cpp
#define APP_VERSION "0.4.0-beta"
#define OSC_MIN_INTERVAL 2000        // OSC 发送间隔 (ms)
#define UPDATE_CHECK_INTERVAL 3600000 // 更新检查间隔 (ms)
```

---

## Git 仓库信息

- **远程仓库**: `https://github.com/pcwl049/VRChat-lyrics-display.git`
- **当前分支**: `main`
- **最新提交**: `fc6d440` - fix: 简化覆盖层关闭逻辑 + 自动检测不显示弹窗

---

## 注意事项

1. **编译环境**: 必须使用 VS2022 的 vcvars64.bat 初始化环境
2. **中文路径**: 项目路径包含中文时 CMake 可能出现问题，建议使用 compile_gui.bat 直接编译
3. **依赖运行库**: 需要安装 Visual C++ Redistributable
4. **Windows 版本**: 需要 Windows 10 1809+ (WinRT SMTC 支持)

---

*Generated by iFlow CLI on 2026-03-11*

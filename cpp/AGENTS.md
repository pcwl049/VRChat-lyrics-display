# VRChat Lyrics Display - 项目上下文

## 项目概述

VRChat Lyrics Display 是一个 C++ Windows 应用程序，用于在 VRChat 中实时显示歌词。它通过 OSC (Open Sound Control) 协议将歌词发送到 VRChat 的聊天框。

**版本**: v0.3.6  
**仓库**: https://github.com/pcwl049/VRChat-lyrics-display  
**主程序**: `bin/VRCLyricsDisplay.exe`

## 核心功能

### 音乐源支持
| 平台 | 连接方式 | 端口 | 实现文件 |
|------|----------|------|----------|
| MoeKoeMusic | WebSocket | 6520 | `moekoe_ws.cpp/h` |
| 网易云音乐 | Chrome DevTools Protocol | 9222 | `netease_ws.cpp/h` |
| QQ音乐及其他 SMTC 播放器 | WinRT SMTC | - | `smtc_client.cpp/h` |

### VRChat 集成
- **OSC 发送**: 通过 UDP 发送到 `127.0.0.1:9000`
- **聊天框地址**: `/chatbox/input`
- **限流**: 播放时 2 秒/次，暂停时 2 秒/次（避免 VRChat 限制）

### GUI 特性
- 毛玻璃/透明效果窗口 (DWM Blur Behind)
- 固定深色主题（已移除浅色主题支持）
- 三标签页：主页、性能、设置
- 系统托盘最小化
- 自动更新检查 (GitHub Releases)
- GPU/CPU/内存性能监控显示

## 文件结构

```
cpp/
├── bin/
│   ├── VRCLyricsDisplay.exe    # 编译输出
│   └── config_gui.json         # 运行时配置
└── gui/
    ├── main_gui.cpp            # 主程序入口和 GUI 逻辑 (~5350 行)
    ├── glass_window.cpp/h      # 毛玻璃效果窗口渲染
    ├── moekoe_ws.cpp/h         # MoeKoeMusic WebSocket 客户端
    ├── netease_ws.cpp/h        # 网易云 CDP 客户端
    ├── smtc_client.cpp/h       # SMTC 媒体控制客户端
    ├── compile_gui.bat         # 主编译脚本
    └── config_gui.json         # 默认配置
```

## 编译方法

### 环境要求
- Visual Studio 2022 (MSVC)
- Windows 10+ SDK
- C++20 标准

### 编译命令
```batch
cd gui
compile_gui.bat
```

### 编译脚本内容
```batch
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"
cl /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /await ^
    main_gui.cpp glass_window.cpp moekoe_ws.cpp netease_ws.cpp smtc_client.cpp ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib gdiplus.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib dxgi.lib ^
    /Fe:..\bin\VRCLyricsDisplay.exe
```

### 依赖库
- `runtimeobject.lib` / `windowsapp.lib` - WinRT (SMTC)
- `dwmapi.lib` - 桌面窗口管理器 (毛玻璃效果)
- `gdiplus.lib` - GDI+ 图形
- `ws2_32.lib` - Winsock (WebSocket)
- `winhttp.lib` - HTTP/WebSocket 客户端
- `shell32.lib` - Shell 操作
- `ole32.lib` - COM
- `msimg32.lib` - AlphaBlend 等
- `dxgi.lib` - GPU 信息

## 代码结构 (main_gui.cpp 约5350行)

### 关键行号
| 功能 | 行号 | 说明 |
|------|------|------|
| 版本定义 | L4-7 | APP_VERSION, GITHUB_REPO |
| 主题颜色 | L65-110 | ThemeColors, UpdateThemeColors() |
| 全局变量 | L137-250 | 窗口、平台、动画等状态 |
| LoadConfig | L1181 | 配置加载 (JSON解析用lambda) |
| SaveConfig | L1272 | 配置保存 |
| CheckAutoStart | L1310 | 注册表检查开机启动 |
| SetAutoStart | L1322 | 注册表设置开机启动 |
| ExportLogs | L1340 | 导出调试日志 |
| DrawRoundRect | L1927 | GDI+圆角矩形 |
| DrawRoundRectWithBorder | L1946 | 带边框圆角矩形 |
| DrawRoundRectAlpha | L1970 | 半透明圆角矩形 |
| DrawTextLeft/Centered | L1989-2030 | 文本绘制函数 |
| DialogProc | L2032 | 自定义对话框 |
| TrayMenuProc | L2508 | 托盘菜单 |
| DrawButtonAnim | L2688 | 动画按钮 |
| DrawTitleBarButton | L2737 | 标题栏按钮 |
| DrawProgressBar | L2752 | 进度条 |
| OnPaint开始 | L2820 | WM_PAINT处理 |
| 标签页绘制 | L2915+ | 3个标签页按钮 |
| 主页内容 | L3016+ | 平台选择、连接按钮 |
| 平台下拉菜单 | L3080+ | MoeKoe/网易云/QQ音乐 |
| 性能页内容 | L3230+ | CPU/内存/GPU/FPS监控 |
| 设置页内容 | L3450+ | IP/端口/复选框 |
| 右侧OSC预览 | L3600+ | 消息预览面板 |
| IsAnimationActive | L4030 | 动画状态检查 |
| UpdateAnimations | L4070 | 更新所有动画 |
| WndProc | L4173 | 主消息处理 |
| WM_CREATE | L4174 | 初始化字体、SMTC |
| WM_MOUSEMOVE | L4230 | 鼠标悬停检测 |
| WM_LBUTTONDOWN | L4374 | 点击处理 |
| 标签页点击 | L4410 | 切换标签页动画 |
| 主页点击 | L4430 | 平台菜单、连接按钮 |
| 设置页点击 | L4770 | 复选框切换 |
| WM_TIMER | L4920 | 定时器更新 |
| WinMain | L5100+ | 程序入口 |

### 全局变量分类

**窗口状态**
- `g_winW/g_winH/g_winX/g_winY` - 窗口大小位置
- `g_hwnd` - 主窗口句柄
- `g_dragging/g_dragStart` - 拖拽状态

**平台连接**
- `g_platforms` - PlatformInfo向量 (MoeKoe/网易云/QQ音乐)
- `g_currentPlatform` - 用户选择的平台 (0/1/2)
- `g_activePlatform` - 正在播放的平台 (-1=无)
- `g_moeKoeClient/g_neteaseClient/g_smtcClient` - 客户端实例

**标签页**
- `g_currentTab` - 当前标签 (0=主页, 1=性能, 2=设置)
- `g_tabHover[3]` - 标签悬停状态
- `g_prevTab/g_tabSlideDirection` - 切换动画

**配置选项**
- `g_oscIp/g_oscPort` - OSC目标地址
- `g_minimizeToTray/g_startMinimized/g_autoStart`
- `g_showPerfOnPause/g_autoUpdate/g_showPlatform`
- `g_oscMessageMode/g_performanceMode/g_performanceOrder`

**动画状态**
- `Animation` 结构体: value, target, speed, update()
- `g_windowFadeAnim/g_windowScaleAnim` - 启动动画
- `g_tabSlideAnim/g_menuExpandAnim` - UI动画
- `g_btnXxxAnim` - 各按钮动画

### 布局常量
```cpp
TITLEBAR_H = 60       // 标题栏高度
CARD_PADDING = 25     // 卡片边距
leftColW = 260        // 左侧面板宽度
tabW = 70             // 标签页宽度
tabH = 32             // 标签页高度
checkboxSize = 26     // 复选框大小
WIN_W_DEFAULT = 650   // 默认窗口宽度
WIN_H_DEFAULT = 720   // 默认窗口高度
```

### 消息处理流程

**WM_CREATE**
1. 创建字体 (g_fontTitle/Subtitle/Normal/Small/Lyric/Label)
2. 创建画刷
3. 设置窗口圆角和毛玻璃效果
4. 创建托盘图标
5. 初始化SMTC客户端 (QQ音乐)
6. 启动定时器 (16ms)

**WM_MOUSEMOVE**
1. 检测标签页悬停
2. 检测按钮悬停 (根据当前标签页)
3. 检测平台菜单项悬停
4. 更新hover状态并重绘

**WM_LBUTTONDOWN**
1. 标题栏按钮点击 (关闭/最小化/更新)
2. 标签页切换 (触发滑动动画)
3. 主页: 平台菜单展开/选择、连接按钮
4. 设置页: 复选框切换、导出日志

**WM_TIMER**
1. UpdateAnimations() - 更新所有动画
2. UpdatePerfStats() - 更新性能统计
3. 智能重绘 (动画活跃/内容变化/空闲刷新)

### 绘图函数详解

**DrawRoundRect(hdc, x, y, w, h, radius, color)**
- 使用GDI+ GraphicsPath绘制圆角矩形
- SmoothingModeHighQuality抗锯齿

**DrawRoundRectAlpha(hdc, x, y, w, h, radius, color, alpha)**
- 半透明填充，用于毛玻璃效果面板

**DrawButtonAnim(hdc, x, y, w, h, text, anim, accent)**
- 带动画效果的按钮
- anim.value控制背景色渐变

**DrawTitleBarButton(hdc, x, y, size, symbol, anim)**
- 标题栏小按钮 (关闭/最小化/更新)
- 圆形背景，悬停时高亮

**DrawProgressBar(hdc, x, y, w, h, progress)**
- 圆角进度条
- 渐变填充效果

## 配置文件

`config_gui.json` 结构：
```json
{
  "osc": { "ip": "127.0.0.1", "port": 9000 },
  "moekoe_port": 6520,
  "osc_enabled": true,
  "minimize_to_tray": true,
  "start_minimized": false,
  "show_perf_on_pause": true,
  "auto_update": true,
  "show_platform": true,
  "auto_start": false,
  "osc_message_mode": 0,
  "performance_mode": 0,
  "performance_order": "cpu,memory,gpu,fps,process,osc",
  "win_width": 650,
  "win_height": 720,
  "win_x": -1,
  "win_y": -1,
  "skip_version": ""
}
```

## 最近修改 (2026-03-10)

1. **移除深浅色主题切换** - 固定使用深色主题
2. **移除OSC消息模式选项** - 从设置页删除
3. **移除性能排序选项** - 从设置页删除
4. **性能模式移到性能标签页顶部** - 从设置页移至性能页
5. **修复标签页超出面板边框** - 调整tabW(70)和间距(6)

## 文件结构

```
cpp/
├── bin/
│   ├── VRCLyricsDisplay.exe    # 编译输出
│   └── config_gui.json         # 运行时配置
└── gui/
    ├── main_gui.cpp            # 主程序入口和 GUI 逻辑 (~6500 行)
    ├── glass_window.cpp/h      # 毛玻璃效果窗口渲染
    ├── moekoe_ws.cpp/h         # MoeKoeMusic WebSocket 客户端
    ├── netease_ws.cpp/h        # 网易云 CDP 客户端
    ├── smtc_client.cpp/h       # SMTC 媒体控制客户端
    ├── compile_gui.bat         # 主编译脚本
    └── config_gui.json         # 默认配置
```

## 编译方法

### 环境要求
- Visual Studio 2022 (MSVC)
- Windows 10+ SDK
- C++20 标准

### 编译命令
```batch
cd gui
compile_gui.bat
```

### 编译脚本内容
```batch
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvars64.bat"
cl /std:c++20 /EHsc /W3 /O2 /MD /utf-8 /await ^
    main_gui.cpp glass_window.cpp moekoe_ws.cpp netease_ws.cpp smtc_client.cpp ^
    runtimeobject.lib windowsapp.lib dwmapi.lib ole32.lib user32.lib gdi32.lib gdiplus.lib shell32.lib ws2_32.lib msimg32.lib winhttp.lib ^
    /Fe:..\bin\VRCLyricsDisplay.exe
```

### 依赖库
- `runtimeobject.lib` / `windowsapp.lib` - WinRT (SMTC)
- `dwmapi.lib` - 桌面窗口管理器 (毛玻璃效果)
- `gdiplus.lib` - GDI+ 图形
- `ws2_32.lib` - Winsock (WebSocket)
- `winhttp.lib` - HTTP/WebSocket 客户端
- `shell32.lib` - Shell 操作
- `ole32.lib` - COM
- `msimg32.lib` - AlphaBlend 等

## 配置文件

`config_gui.json` 结构：
```json
{
  "osc": { "ip": "127.0.0.1", "port": 9000 },
  "moekoe_port": 6520,
  "osc_enabled": true,
  "minimize_to_tray": true,
  "start_minimized": false,
  "show_perf_on_pause": true,
  "auto_update": true,
  "show_platform": true,
  "dark_mode": true,
  "auto_start": false,
  "win_width": 650,
  "win_height": 720,
  "win_x": -1,
  "win_y": -1,
  "skip_version": ""
}
```

## 代码架构

### 主程序流程 (main_gui.cpp)
1. 初始化窗口、字体、画刷
2. 启动 OSC 发送器
3. 连接音乐平台（MoeKoe/网易云/QQ音乐）
4. 接收歌曲信息和歌词回调
5. 更新 GUI 显示
6. 发送歌词到 VRChat OSC

### 歌词数据结构
```cpp
struct LyricLine {
    int startTime;      // 开始时间 (ms)
    int duration;       // 持续时间 (ms)
    std::wstring text;  // 歌词文本
    std::wstring translation; // 翻译（可选）
};
```

### 平台自动切换
- 支持多平台同时连接
- 自动检测正在播放的平台
- 用户可手动选择平台

## 调试

调试日志写入临时目录：
```
%TEMP%\vrclayrics_debug.log
```

使用 `MainDebugLog()` 函数记录调试信息。

## 更新机制

1. 检查 GitHub Releases API: `https://api.github.com/repos/pcwl049/VRChat-lyrics-display/releases/latest`
2. 比较版本号
3. 下载新版本 ZIP
4. SHA256 校验
5. 提示用户安装

## 注意事项

1. **VRChat OSC 限制**: 发送频率不要超过 2 秒/次，否则会被限流
2. **网易云 CDP**: 需要网易云音乐开启远程调试端口 9222
3. **QQ音乐**: 通过 WinRT SMTC 工作，需要 Windows 10+
4. **GPU 监控**: 支持 NVIDIA (NVML)、AMD (ADL)、Intel (PDH)

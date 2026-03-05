MoeKoeGUI - WinUI 3 玻璃拟态界面
=====================================

## 环境要求

1. **Visual Studio 2022** (17.8 或更高版本)
   - 工作负载: "使用 C++ 的桌面开发"
   - 工作负载: "Windows 应用 SDK"

2. **Windows App SDK** (通过 NuGet 自动安装)
   - 版本: 1.4.231115003

3. **Windows SDK**
   - 版本: 10.0.26100.0 或更高

## 编译步骤

### 方法一: Visual Studio

1. 打开 Visual Studio 2022
2. 打开解决方案: `MoeKoeGUI.vcxproj`
3. 右键解决方案 → "还原 NuGet 包"
4. 选择配置: `Release | x64`
5. 编译 (Ctrl+Shift+B)

### 方法二: 命令行 (需要已安装 VS 和 NuGet)

```batch
# 1. 还原 NuGet 包
nuget restore MoeKoeGUI.vcxproj

# 2. 编译
msbuild MoeKoeGUI.vcxproj /p:Configuration=Release /p:Platform=x64
```

## 功能特性

- **玻璃拟态 (Glassmorphism)**
  - Mica 背景
  - 半透明效果
  - 模糊背景

- **非线性动画**
  - 歌词切换淡入淡出
  - 进度条平滑过渡
  - 悬停缩放效果

- **实时音乐信息**
  - GSMTC 精确进度
  - 歌曲标题/歌手
  - 歌词同步显示

- **播放统计**
  - 今日播放次数
  - 总播放歌曲数

## 文件结构

```
gui/
├── pch.h / pch.cpp      # 预编译头
├── App.h / App.cpp      # 应用入口
├── App.idl              # App 接口定义
├── MainWindow.h/cpp     # 主窗口实现
├── MainWindow.idl       # MainWindow 接口定义
├── MoeKoeGUI.vcxproj    # 项目文件
└── packages.config      # NuGet 包配置
```

## 扩展开发

### 添加歌词获取

在 `MainWindow.cpp` 中添加 Kugou API 调用:

```cpp
#include "http_client.h"

void MainWindow::FetchLyrics(std::wstring title, std::wstring artist)
{
    std::thread([this, title, artist]() {
        // 搜索歌曲
        auto searchUrl = L"https://mobileservice.kugou.com/api/v3/search/song?keyword=" 
                       + title + L" " + artist;
        auto response = HttpClient::Get(searchUrl);
        
        // 解析并获取歌词...
        // 更新 UI
        m_dispatcher.TryEnqueue([this, lyric]() {
            UpdateLyric(lyric);
        });
    }).detach();
}
```

### 添加 OSC 发送

```cpp
#include "osc_sender.h"

void MainWindow::SendToVRChat(std::wstring message)
{
    OSCSender osc("127.0.0.1", 9000);
    osc.Connect();
    osc.SendChatboxMessage(message);
}
```

## 注意事项

1. WinUI 3 应用需要 Windows 10 1809 (17763) 或更高版本
2. 某些玻璃效果需要 Windows 11 才能正常显示
3. 调试时建议使用 Debug 配置
4. 发布时使用 Release 配置以获得更好的性能

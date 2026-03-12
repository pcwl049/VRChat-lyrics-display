# VRChat Lyrics Display

在 VRChat 聊天框显示当前播放的音乐信息和歌词。

## 功能

- 支持 MoeKoeMusic、网易云音乐、QQ音乐、汽水音乐
- 显示歌曲名、歌手、进度条、歌词
- 显示性能监控功能
- 自动获取歌词
- 支持多音乐平台自动切换/手动切换
- 毛玻璃效果界面
- 系统托盘运行
- 自动更新检查

## 使用

1. 启动 `VRCLyricsDisplay.exe`
2. 开启 MoeKoeMusic 的 API 模式（端口 6520）
3. 或启动网易云音乐并开启远程调试（端口 9222）
4. 在 VRChat 中开启 OSC

## 构建

需要 Visual Studio 2022，运行 `cpp/gui/compile_gui.bat`

## 配置

编辑 `config_gui.json`:
- `osc.ip`: VRChat OSC 地址（默认 127.0.0.1）
- `osc.port`: VRChat OSC 端口（默认 9000）
- `auto_update`: 自动检查更新
- `show_platform`: 显示音乐平台名称
- `minimize_to_tray`: 最小化到托盘
- `auto_start`: 开机自启动

## 协议

[GPL-3.0](LICENSE) - 任何衍生作品必须开源并使用相同协议

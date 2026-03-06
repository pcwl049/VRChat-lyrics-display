# VRChat-lyrics-display

在 VRChat 聊天框显示当前播放的音乐信息。

## 功能

- 支持 MoeKoeMusic 和网易云音乐
- 显示歌曲名、歌手、进度条、歌词
- 自动获取逐字歌词
- 支持多音乐平台自动切换

## 使用

1. 启动 `MoeKoeGUI.exe`
2. 开启 MoeKoeMusic 的 API 模式（端口 6520）
3. 或启动网易云音乐并开启远程调试（端口 9222）
4. 在 VRChat 中开启 OSC

## 构建

需要 Visual Studio 2022，运行 `cpp/gui/compile_gui.bat`

## 配置

编辑 `config_gui.json`:
- `osc.ip`: VRChat OSC 地址
- `osc.port`: VRChat OSC 端口
- `auto_update`: 自动检查更新
- `show_platform`: 显示音乐平台名称

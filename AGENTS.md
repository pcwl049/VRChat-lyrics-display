# MoeKoeMusic → VRChat 聊天框显示程序

通过 WebSocket 连接 MoeKoeMusic 客户端，实时获取播放状态和歌词，通过 OSC 协议在 VRChat 聊天框显示歌曲信息、歌词、进度条和播放统计。

## 项目结构

```
D:\Project\音乐显示\
├── main.py              # 主程序入口，监控循环
├── moekoe_client.py     # MoeKoeMusic WebSocket 客户端
├── vrc_osc.py           # VRChat OSC 通信（发送/接收）
├── lyrics.py            # 歌词获取模块（酷狗 API，逐字歌词）
├── translator.py        # 歌词翻译模块（Google/DeepL/百度）
├── ui.py                # 命令行美化输出
├── hmd_battery.py       # VR头显电量监测（OpenVR/ADB）
├── voicemeeter_ctrl.py  # Voicemeeter 音频控制
├── config.json          # 配置文件
├── play_stats.json      # 播放统计（持久化）
├── requirements.txt     # Python 依赖
├── start.bat            # Windows 启动脚本
├── KuGouMusicApi/       # 酷狗音乐 API 服务（Node.js，备用歌词源）
└── lyrics_cache/        # 歌词缓存目录
```

## 技术栈

- **Python 3.11**
- **websocket-client** - MoeKoeMusic WebSocket 通信
- **python-osc** - OSC 协议通信
- **openvr** - SteamVR 头显电量获取
- **voicemeeter-api** - Voicemeeter 音频路由控制
- **deep-translator** - 歌词翻译（Google/DeepL/百度）

## 运行命令

```bash
# 安装依赖
pip install -r requirements.txt
pip install websocket-client  # MoeKoeMusic 通信必需

# 持续监控模式（默认）
python main.py

# 自定义刷新间隔
python main.py -i 0.5

# 只发送一次当前歌曲
python main.py --once

# 查看状态诊断
python main.py --status

# 使用 Windows 启动脚本
start.bat
```

## 配置文件 (config.json)

```json
{
    "interval": 0.3,
    "min_send_interval": 3.0,
    "resend_interval": 12.0,
    "show_lyrics": true,
    "display_mode": "full",
    "emoji_theme": "default",
    "pause_display": {
        "show_full_info": true,
        "show_lyrics_preview": true,
        "preview_lines": 3
    },
    "osc": {
        "ip": "127.0.0.1",
        "port": 9000
    },
    "osc_server": {
        "enabled": true,
        "port": 9001
    },
    "hmd_battery": {
        "enabled": false,
        "interval": 10.0
    },
    "moekoe": {
        "host": "127.0.0.1",
        "port": 6520
    }
}
```

### 关键参数说明

| 参数 | 说明 | 推荐值 |
|------|------|--------|
| `interval` | 检测循环间隔（秒） | 0.3 |
| `min_send_interval` | OSC消息最小发送间隔（秒） | 3.0 |
| `resend_interval` | 暂停状态刷新间隔（秒） | 12.0 |
| `moekoe.port` | MoeKoeMusic WebSocket 端口 | 6520 |

**注意：** VRChat OSC Chatbox 有速率限制，发送间隔过短（<3秒）可能触发发言限制。

## 显示模式

| 模式 | 说明 |
|------|------|
| `full` | 完整模式：歌曲信息 + 进度条 + 歌词 + 统计 |
| `compact` | 简洁模式：歌曲信息 + 进度时间 |
| `lyric` | 歌词优先：最大化歌词显示空间 |
| `minimal` | 极简模式：仅歌曲名 |

## 显示格式

### 播放状态
```
♫ 歌曲名 - 歌手
▓▓▓▓▓▱▱▱▱ 1:23/4:29 | 🔋电量
  上一句歌词
▶ 当前歌词
  下一句歌词
```

### 暂停状态
```
⏸ ♫ 歌曲名 - 歌手 [4:29]
▓▓▓▓▓▱▱▱▱▱ 1:23/4:29
今日 5 首 | 共 12 首歌 | 🔋电量
当前歌词
下一句
...
```

## 核心模块说明

### moekoe_client.py - MoeKoeMusic 客户端

通过 WebSocket 连接 MoeKoeMusic，实时获取播放状态。

**连接要求：**
- MoeKoeMusic 设置中开启 "API模式"
- 默认端口：6520

**数据结构：**
- `MoeKoeSongInfo` - 歌曲信息（名称、歌手、时长、封面）
- `MoeKoeLyricLine` - 逐字歌词行（字符、时间）
- `MoeKoeLyricChar` - 逐字字符（char、startTime、endTime）
- `MoeKoePlayerState` - 播放状态（is_playing、current_time）

**事件类型：**
| 事件 | 说明 |
|------|------|
| `lyrics` | 歌词数据更新 |
| `playerState` | 播放状态更新 |
| `welcome` | 连接成功 |
| `songChange` | 歌曲切换 |

**使用：**
```python
from moekoe_client import MoeKoeClient

client = MoeKoeClient('127.0.0.1', 6520)
if client.connect():
    # 获取当前播放信息
    song = client.data.song.name
    is_playing = client.data.player_state.is_playing
    current_time = client.data.player_state.current_time
    
    # 获取当前歌词
    line, idx = client.get_current_lyric(current_time)
```

### lyrics.py - 歌词获取（酷狗 API）

当 MoeKoeMusic 没有歌词时，从酷狗 API 获取歌词和逐字信息。

**数据结构：**
- `SongInfo` - 歌曲完整信息（时长、歌词、逐字信息、翻译）
- `LyricLine` - 单行歌词（时间、文本、逐字数据、翻译）
- `LyricWord` - 逐字歌词单元

**关键函数：**
- `LyricFetcher.get_song_info()` - 获取歌曲信息（时长+歌词+逐字）
- `parse_lrc()` - 解析 LRC 格式歌词
- `parse_krc()` - 解析 KRC 逐字歌词格式

**缓存机制：**
- 内存缓存（LRU）：最多 50 条
- 磁盘缓存：`lyrics_cache/` 目录

### vrc_osc.py - OSC 通信

**发送功能：**
- `send_chat_message()` - 发送消息到 VRChat 聊天框
- `clear_chatbox()` - 清除聊天框

**接收功能（OSC 服务端）：**
- 监听端口 9001，接收 VRChat 快捷键命令

**预定义 OSC 命令：**
| 地址 | 说明 |
|------|------|
| `/music/share` | 分享当前歌曲到聊天框 |
| `/music/mode` | 切换显示模式 |
| `/music/mode/set` | 设置特定显示模式 |
| `/music/calibrate` | 歌词进度校准 |

### main.py - 主程序

**关键类：**
- `PlayStats` - 播放统计（持久化到 play_stats.json）
  - 今日播放次数
  - 总播放歌曲数
  - 每天自动重置

**单例检测：**
使用 Windows 互斥体防止程序多开，避免 OSC 通信冲突。

## 常见问题

### MoeKoeMusic 连接失败
1. 确保 MoeKoeMusic 已启动
2. 在 MoeKoeMusic 设置中开启 "API模式"
3. 检查端口 6520 是否被占用

### 歌词不显示
1. MoeKoeMusic 歌词会自动获取
2. 如果没有歌词，程序会自动从酷狗 API 获取
3. 检查网络连接

### VRChat OSC 未连接
1. 确保 VRChat 正在运行
2. 在 VRChat 设置中开启 OSC
3. 检查防火墙是否阻止端口 9000

### 程序提示"已在运行中"
- 程序使用单例模式，同一时间只能运行一个实例
- 如需重启，先关闭现有进程

### 头显电量获取失败
- OpenVR 模式：确保 SteamVR 运行中
- ADB 模式：安装 ADB 并连接 Quest 设备

## 开发约定

- 使用 Python 3.11+ 特性
- 类型注解：使用 `X | None` 或 `Optional[X]`
- 日志：使用 `logging` 模块，避免 `print` 调试
- 命令行输出：统一使用 `ui.py` 中的函数

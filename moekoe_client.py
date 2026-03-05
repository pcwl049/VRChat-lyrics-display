"""
MoeKoeMusic WebSocket 适配器

通过 WebSocket 连接 MoeKoeMusic 客户端，实时获取播放状态和歌词信息。

使用方式：
1. 在 MoeKoeMusic 设置中开启 API 模式
2. 运行程序，自动连接 ws://127.0.0.1:6520

数据格式：
- lyrics: 歌曲信息 + 歌词数据（base64编码，无时间戳）
- playerState: 播放状态（isPlaying, currentTime）

注意：MoeKoeMusic 返回的歌词没有时间戳，仅用于显示歌词文本。
      如需逐字同步，请使用酷狗 API 获取带时间戳的歌词。
"""

import json
import logging
import threading
import time
import base64
from dataclasses import dataclass, field
from typing import Callable, Optional, Any
from enum import Enum

logger = logging.getLogger(__name__)

# 尝试导入 websocket 库
try:
    import websocket
    WEBSOCKET_AVAILABLE = True
except ImportError:
    WEBSOCKET_AVAILABLE = False
    logger.warning("websocket-client 未安装，MoeKoeMusic 适配器不可用。运行: pip install websocket-client")


class MoeKoeEventType(Enum):
    """MoeKoeMusic 事件类型"""
    LYRICS = "lyrics"           # 歌词更新
    PLAYER_STATE = "playerState"  # 播放状态更新
    WELCOME = "welcome"         # 连接成功
    SONG_CHANGE = "songChange"  # 歌曲切换（自定义事件）


@dataclass
class MoeKoeLoudnessNormalization:
    """音量标准化信息"""
    volume: float = 0.0         # 原始音量（dB）
    volume_gain: float = 0.0    # 增益值
    volume_peak: float = 1.0    # 峰值


@dataclass
class MoeKoeSongInfo:
    """MoeKoeMusic 歌曲信息"""
    id: int = 0                 # 歌曲数字ID
    name: str = ""              # 歌曲名
    author: str = ""            # 歌手
    img: str = ""               # 封面图URL
    hash: str = ""              # 歌曲Hash（用于酷狗API查询）
    duration: float = 0.0       # 时长（秒）
    url: str = ""               # 播放URL
    loudness_normalization: Optional[MoeKoeLoudnessNormalization] = None  # 音量标准化


@dataclass
class MoeKoeLyricChar:
    """逐字歌词字符"""
    char: str = ""              # 字符
    startTime: float = 0.0      # 开始时间（毫秒）
    endTime: float = 0.0        # 结束时间（毫秒）
    duration: float = 0.0       # 持续时间（毫秒）


@dataclass
class MoeKoeLyricLine:
    """逐字歌词行"""
    characters: list = field(default_factory=list)  # 字符列表
    text: str = ""              # 整行文本
    
    def get_text(self) -> str:
        """获取歌词文本"""
        if self.text:
            return self.text
        return "".join(c.char for c in self.characters) if self.characters else ""
    
    def get_start_time(self) -> float:
        """获取行开始时间（秒）"""
        if self.characters:
            return self.characters[0].startTime / 1000.0
        return 0.0
    
    def get_end_time(self) -> float:
        """获取行结束时间（秒）"""
        if self.characters:
            return self.characters[-1].endTime / 1000.0
        return 0.0
    
    def get_word_at_time(self, current_time: float) -> tuple[int, str, str]:
        """
        获取指定时间点的逐字信息
        
        Args:
            current_time: 当前时间（秒）
        
        Returns:
            (当前字符索引, 已唱部分, 未唱部分)
        """
        if not self.characters:
            return (0, "", self.text)
        
        current_ms = current_time * 1000
        sung = ""
        unsung = ""
        current_idx = -1
        
        for i, char in enumerate(self.characters):
            if char.endTime <= current_ms:
                sung += char.char
            elif char.startTime <= current_ms < char.endTime:
                current_idx = i
                sung += char.char
            else:
                unsung += char.char
        
        if current_idx == -1 and sung:
            current_idx = len(self.characters) - 1
        
        return (current_idx, sung, unsung)


@dataclass
class MoeKoePlayerState:
    """MoeKoeMusic 播放状态"""
    is_playing: bool = False    # 是否正在播放
    current_time: float = 0.0   # 当前播放时间（秒）


@dataclass
class MoeKoeData:
    """MoeKoeMusic 完整数据"""
    song: MoeKoeSongInfo = field(default_factory=MoeKoeSongInfo)
    lyrics: list = field(default_factory=list)  # List[MoeKoeLyricLine] - 无时间戳的歌词行
    raw_lyrics: Any = None                       # 解码后的原始歌词数据
    player_state: MoeKoePlayerState = field(default_factory=MoeKoePlayerState)
    connected: bool = False
    last_update: float = 0.0


class MoeKoeClient:
    """MoeKoeMusic WebSocket 客户端"""
    
    DEFAULT_HOST = "127.0.0.1"
    DEFAULT_PORT = 6520
    
    def __init__(self, host: str = None, port: int = None):
        """
        初始化客户端
        
        Args:
            host: WebSocket 主机地址
            port: WebSocket 端口
        """
        self.host = host or self.DEFAULT_HOST
        self.port = port or self.DEFAULT_PORT
        self.ws_url = f"ws://{self.host}:{self.port}"
        
        self.ws: Optional[websocket.WebSocketApp] = None
        self.ws_thread: Optional[threading.Thread] = None
        self.running = False
        self.reconnect_interval = 5.0  # 重连间隔
        
        # 数据存储
        self.data = MoeKoeData()
        
        # 回调函数
        self._callbacks: dict[MoeKoeEventType, list[Callable]] = {
            MoeKoeEventType.LYRICS: [],
            MoeKoeEventType.PLAYER_STATE: [],
            MoeKoeEventType.WELCOME: [],
            MoeKoeEventType.SONG_CHANGE: [],
        }
        
        # 上一次歌曲hash，用于检测歌曲切换
        self._last_song_hash: str = ""
    
    def is_available(self) -> bool:
        """检查 WebSocket 库是否可用"""
        return WEBSOCKET_AVAILABLE
    
    def register_callback(self, event_type: MoeKoeEventType, callback: Callable):
        """
        注册事件回调
        
        Args:
            event_type: 事件类型
            callback: 回调函数，接收 MoeKoeData 参数
        """
        if event_type in self._callbacks:
            self._callbacks[event_type].append(callback)
    
    def _trigger_callbacks(self, event_type: MoeKoeEventType):
        """触发回调"""
        for callback in self._callbacks.get(event_type, []):
            try:
                callback(self.data)
            except Exception as e:
                logger.error(f"回调执行失败: {e}")
    
    def connect(self) -> bool:
        """
        连接到 MoeKoeMusic
        
        Returns:
            是否成功建立连接
        """
        if not WEBSOCKET_AVAILABLE:
            logger.error("websocket-client 未安装")
            return False
        
        if self.running:
            return self.data.connected
        
        try:
            self.running = True
            self.ws = websocket.WebSocketApp(
                self.ws_url,
                on_open=self._on_open,
                on_message=self._on_message,
                on_error=self._on_error,
                on_close=self._on_close
            )
            
            self.ws_thread = threading.Thread(
                target=self._run_websocket,
                daemon=True
            )
            self.ws_thread.start()
            
            # 等待连接建立（最多3秒）
            for _ in range(30):
                if self.data.connected:
                    logger.info(f"MoeKoeMusic 已连接: {self.ws_url}")
                    return True
                time.sleep(0.1)
            
            # 超时，连接失败
            logger.error("MoeKoeMusic 连接超时")
            self.running = False
            return False
            
        except Exception as e:
            logger.error(f"连接失败: {e}")
            self.running = False
            return False
    
    def _run_websocket(self):
        """运行 WebSocket（在独立线程中）"""
        retry_count = 0
        max_retries = 3  # 最大重连次数
        
        while self.running and retry_count < max_retries:
            try:
                self.ws.run_forever()
            except Exception as e:
                logger.error(f"WebSocket 运行错误: {e}")
            
            if self.running:
                retry_count += 1
                if retry_count < max_retries:
                    logger.info(f"{self.reconnect_interval}秒后重连 ({retry_count}/{max_retries})...")
                    time.sleep(self.reconnect_interval)
                else:
                    logger.error("重连次数已达上限，停止重连")
                    self.running = False
    
    def disconnect(self):
        """断开连接"""
        self.running = False
        if self.ws:
            try:
                self.ws.close()
            except:
                pass
        self.data.connected = False
        logger.info("MoeKoeMusic 客户端已断开")
    
    def _on_open(self, ws):
        """WebSocket 连接打开"""
        logger.info("MoeKoeMusic WebSocket 已连接")
        self.data.connected = True
        self._trigger_callbacks(MoeKoeEventType.WELCOME)
    
    def _on_close(self, ws, close_status_code, close_msg):
        """WebSocket 连接关闭"""
        logger.info(f"MoeKoeMusic WebSocket 已断开: {close_status_code} - {close_msg}")
        self.data.connected = False
    
    def _on_error(self, ws, error):
        """WebSocket 错误"""
        logger.error(f"MoeKoeMusic WebSocket 错误: {error}")
    
    def _on_message(self, ws, message):
        """接收消息"""
        try:
            data = json.loads(message)
            msg_type = data.get("type")
            msg_data = data.get("data")
            
            if msg_type == "welcome":
                logger.info(f"MoeKoeMusic 欢迎消息: {msg_data}")
            
            elif msg_type == "lyrics":
                self._handle_lyrics(msg_data)
            
            elif msg_type == "playerState":
                self._handle_player_state(msg_data)
                
        except json.JSONDecodeError as e:
            logger.error(f"JSON 解析失败: {e}")
        except Exception as e:
            logger.error(f"消息处理失败: {e}")
            # 打印原始消息用于调试
            logger.debug(f"原始消息: {message[:200] if len(message) > 200 else message}")
    
    def _handle_lyrics(self, data):
        """
        处理歌词数据
        
        实际数据格式:
        {
            currentTime: float,           // 当前播放时间（秒）
            duration: float,              // 总时长（秒）
            currentSong: {
                id: int,                  // 歌曲数字ID
                name: str,                // 歌曲名
                author: str,              // 歌手
                hash: str,                // 歌曲Hash
                img: str,                 // 封面URL
                url: str,                 // 播放URL
                timeLength: int,          // 时长（可能是秒或毫秒）
                loudnessNormalization: {  // 音量标准化
                    volume: float,
                    volumeGain: float,
                    volumePeak: float
                }
            },
            lyricsData: str               // base64编码的歌词（无时间戳）
        }
        
        注意：MoeKoeMusic 的歌词没有时间戳，仅包含罗马音分词。
              如需逐字同步，请使用酷狗 API 获取带时间戳的歌词。
        """
        if not isinstance(data, dict):
            logger.error(f"歌词数据格式错误: {type(data)}")
            return
            
        # 更新歌曲信息
        song_data = data.get("currentSong", {})
        if isinstance(song_data, dict):
            self.data.song.id = song_data.get("id", 0)
            self.data.song.name = song_data.get("name", "")
            self.data.song.author = song_data.get("author", "")
            self.data.song.img = song_data.get("img", "")
            self.data.song.hash = song_data.get("hash", "")
            self.data.song.url = song_data.get("url", "")
            
            # 音量标准化信息
            loudness = song_data.get("loudnessNormalization", {})
            if loudness:
                self.data.song.loudness_normalization = MoeKoeLoudnessNormalization(
                    volume=loudness.get("volume", 0.0),
                    volume_gain=loudness.get("volumeGain", 0.0),
                    volume_peak=loudness.get("volumePeak", 1.0)
                )
        
        # 更新时长（优先使用 duration 字段）
        if data.get("duration"):
            self.data.song.duration = data.get("duration", 0.0)
        elif song_data.get("timeLength"):
            # timeLength 单位不明确，可能是秒
            self.data.song.duration = float(song_data.get("timeLength", 0))
        
        # 解析歌词
        lyrics_data = data.get("lyricsData", "")
        if lyrics_data:
            # 解码 base64 歌词
            decoded_lyrics = self._decode_lyrics(lyrics_data)
            self.data.raw_lyrics = decoded_lyrics  # 保存原始解码后的歌词
            
            # 尝试提取纯文本歌词（无时间戳）
            if decoded_lyrics:
                self.data.lyrics = self._extract_lyric_lines(decoded_lyrics)
                logger.debug(f"歌词解析完成: {len(self.data.lyrics)} 行")
        
        # 更新时间
        if data.get("currentTime") is not None:
            self.data.player_state.current_time = data.get("currentTime", 0.0)
        
        self.data.last_update = time.time()
        
        # 检测歌曲切换
        current_hash = self.data.song.hash
        if current_hash and current_hash != self._last_song_hash:
            self._last_song_hash = current_hash
            self._trigger_callbacks(MoeKoeEventType.SONG_CHANGE)
            logger.info(f"歌曲切换: {self.data.song.name} - {self.data.song.author}")
        
        self._trigger_callbacks(MoeKoeEventType.LYRICS)
    
    def _decode_lyrics(self, lyrics_str: str) -> Any:
        """
        解码 MoeKoeMusic 歌词字符串
        
        歌词格式可能是：
        1. 纯 LRC 格式：[ti:歌名]... 
        2. LRC + 罗马音：[ti:歌名]...[language:base64...]
        3. 纯 base64 编码的 JSON
        
        Returns:
            解码后的歌词数据（通常是 dict 或 list），或原始字符串
        """
        if not isinstance(lyrics_str, str):
            return lyrics_str
        
        # 检查是否包含 [language:base64...] 格式（罗马音）
        if '[language:' in lyrics_str:
            # 提取 base64 部分
            idx = lyrics_str.find('[language:')
            b64_part = lyrics_str[idx + 10:]  # 去掉 '[language:' 前缀
            
            # 尝试解码罗马音
            try:
                decoded = base64.b64decode(b64_part).decode('utf-8')
                # 解码后应该是 JSON
                import json
                return json.loads(decoded)
            except Exception as e:
                logger.debug(f"罗马音解码失败: {e}")
            
            # 返回 LRC 部分（如果有）
            lrc_part = lyrics_str[:idx]
            if lrc_part.strip():
                return lrc_part
            return lyrics_str
        
        # 尝试直接 base64 解码
        try:
            decoded = base64.b64decode(lyrics_str).decode('utf-8')
            
            # 检查解码后是否是 JSON
            try:
                import json
                return json.loads(decoded)
            except:
                pass
            
            # 检查解码后是否是 [language:...] 格式（双重编码）
            if decoded.startswith('[language:'):
                b64_part = decoded[10:]
                try:
                    inner = base64.b64decode(b64_part).decode('utf-8')
                    import json
                    return json.loads(inner)
                except:
                    pass
            
            return decoded
        except Exception as e:
            logger.debug(f"歌词解码失败: {e}")
            return lyrics_str
    
    def _extract_lyric_lines(self, decoded_data: Any) -> list[MoeKoeLyricLine]:
        """
        从解码后的歌词数据中提取歌词行
        
        支持两种格式：
        1. KRC 逐字格式：[开始时间,持续时间]<相对时间,持续时间,参数>字...
           例如：[0,4812]<0,481,0>草<481,481,0>东...
        
        2. JSON 罗马音格式：{"content": [{"lyricContent": [...]}]}
        """
        lines = []
        
        # 首先尝试解析 KRC 格式（字符串）
        if isinstance(decoded_data, str) and '[' in decoded_data:
            return self._parse_krc_lyrics(decoded_data)
        
        # 然后尝试解析 JSON 格式
        if not isinstance(decoded_data, dict):
            return lines
        
        content = decoded_data.get("content", [])
        if not isinstance(content, list):
            return lines
        
        for item in content:
            if not isinstance(item, dict):
                continue
            
            lyric_content = item.get("lyricContent", [])
            if not isinstance(lyric_content, list):
                continue
            
            # 每个 lyricContent 是一个句子，包含多个音节
            for syllables in lyric_content:
                if isinstance(syllables, list):
                    # 合并音节为一行文本
                    text = "".join(str(s) for s in syllables if s)
                    text = text.strip()
                    
                    if text:
                        line = MoeKoeLyricLine()
                        line.text = text
                        lines.append(line)
        
        return lines
    
    def _parse_krc_lyrics(self, krc_text: str) -> list[MoeKoeLyricLine]:
        """
        解析 KRC 逐字歌词格式
        
        格式示例：
        [0,4812]<0,481,0>草<481,481,0>东<962,481,0>没<1443,481,0>有
        [4812,4331]<0,481,0>作<481,481,0>词<962,481,0>：
        
        - [开始时间ms,持续时间ms] 行级时间戳
        - <相对时间ms,持续时间ms,参数>字 逐字时间戳
        """
        import re
        lines = []
        
        # 正则匹配行：[开始时间,持续时间] 后面跟逐字数据
        line_pattern = re.compile(r'\[(\d+),(\d+)\]([^\[]+)')
        # 正则匹配逐字：<相对时间,持续时间,参数>字
        char_pattern = re.compile(r'<(\d+),(\d+),(\d+)>([^<]+)')
        
        for match in line_pattern.finditer(krc_text):
            line_start_ms = int(match.group(1))
            line_duration_ms = int(match.group(2))
            chars_data = match.group(3)
            
            line = MoeKoeLyricLine()
            line_text = ""
            
            # 解析逐字数据
            for char_match in char_pattern.finditer(chars_data):
                char_rel_time = int(char_match.group(1))  # 相对于行的开始时间
                char_duration = int(char_match.group(2))
                char_text = char_match.group(4)
                
                # 计算绝对时间（毫秒）
                abs_start_time = line_start_ms + char_rel_time
                abs_end_time = abs_start_time + char_duration
                
                char = MoeKoeLyricChar(
                    char=char_text,
                    startTime=abs_start_time,
                    endTime=abs_end_time,
                    duration=char_duration
                )
                line.characters.append(char)
                line_text += char_text
            
            line.text = line_text.strip()
            if line.text:
                lines.append(line)
        
        logger.debug(f"KRC 歌词解析完成: {len(lines)} 行")
        return lines
    
    def _parse_lyrics(self, lyrics_data: list) -> list[MoeKoeLyricLine]:
        """解析歌词数据"""
        lyrics = []
        for line_data in lyrics_data:
            if not isinstance(line_data, dict):
                continue
                
            line = MoeKoeLyricLine()
            
            # 解析字符
            chars_data = line_data.get("characters", [])
            if isinstance(chars_data, list):
                for char_data in chars_data:
                    if not isinstance(char_data, dict):
                        continue
                    char = MoeKoeLyricChar(
                        char=char_data.get("char", ""),
                        startTime=char_data.get("startTime", 0),
                        endTime=char_data.get("endTime", 0),
                        duration=char_data.get("duration", 0)
                    )
                    line.characters.append(char)
            
            # 生成文本
            line.text = line.get_text()
            
            if line.text:  # 只保留有内容的行
                lyrics.append(line)
        
        return lyrics
    
    def _handle_player_state(self, data):
        """
        处理播放状态
        
        数据格式:
        {isPlaying: bool, currentTime: float}
        """
        if not isinstance(data, dict):
            return
            
        self.data.player_state.is_playing = data.get("isPlaying", False)
        self.data.player_state.current_time = data.get("currentTime", 0.0)
        self.data.last_update = time.time()
        
        self._trigger_callbacks(MoeKoeEventType.PLAYER_STATE)
    
    def get_current_lyric(self, current_time: float = None) -> tuple[Optional[MoeKoeLyricLine], int]:
        """
        获取当前时间对应的歌词行
        
        注意：MoeKoeMusic 的歌词没有时间戳！
        此方法通过当前进度比例估算歌词行位置。
        如需精确同步，请使用酷狗 API 获取带时间戳的歌词。
        
        Args:
            current_time: 当前时间（秒），默认使用播放器时间
        
        Returns:
            (歌词行, 行索引) - 基于进度比例估算
        """
        if current_time is None:
            current_time = self.data.player_state.current_time
        
        if not self.data.lyrics:
            return (None, -1)
        
        # 检查是否有时间戳信息
        first_line = self.data.lyrics[0]
        has_timestamp = first_line.get_start_time() > 0 or first_line.get_end_time() > 0
        
        if has_timestamp:
            # 有时间戳，精确查找
            for i, line in enumerate(self.data.lyrics):
                start_time = line.get_start_time()
                end_time = line.get_end_time()
                
                if start_time <= current_time < end_time:
                    return (line, i)
                
                # 如果当前时间在两行之间，返回上一行
                if i > 0:
                    prev_line = self.data.lyrics[i - 1]
                    if prev_line.get_end_time() <= current_time < start_time:
                        return (prev_line, i - 1)
        else:
            # 无时间戳，基于进度比例估算
            total_duration = self.data.song.duration
            if total_duration > 0 and len(self.data.lyrics) > 0:
                progress = current_time / total_duration
                estimated_idx = int(progress * len(self.data.lyrics))
                estimated_idx = max(0, min(estimated_idx, len(self.data.lyrics) - 1))
                return (self.data.lyrics[estimated_idx], estimated_idx)
        
        # 如果超出范围，返回最后一行
        if self.data.lyrics:
            return (self.data.lyrics[-1], len(self.data.lyrics) - 1)
        
        return (None, -1)
    
    def is_connected(self) -> bool:
        """检查是否已连接"""
        return self.data.connected
    
    def get_song_info(self) -> tuple[str, str, float]:
        """
        获取歌曲信息
        
        Returns:
            (歌曲名, 歌手, 时长)
        """
        return (
            self.data.song.name,
            self.data.song.author,
            self.data.song.duration
        )


# 便捷函数
def create_moekoe_client(host: str = None, port: int = None) -> MoeKoeClient:
    """创建 MoeKoeMusic 客户端"""
    return MoeKoeClient(host, port)

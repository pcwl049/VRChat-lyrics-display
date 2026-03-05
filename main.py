"""
MoeKoeMusic → VRChat 聊天框显示程序
通过 WebSocket 连接 MoeKoeMusic 客户端，实时获取播放状态和歌词

特性:
- WebSocket 实时推送，无延迟检测
- 精确播放进度和暂停状态
- 内置逐字歌词支持
- VRChat内快捷键控制
- 多种显示模式
"""
import json
import time
import argparse
import os
import random
import logging
from dataclasses import dataclass, field
from typing import Optional
from enum import Enum

# 配置日志
logger = logging.getLogger(__name__)

from vrc_osc import VRChatOSC, OSCCommandServer, MessagePriority
from lyrics import format_time, SongInfo, LyricLine, LyricFetcher, parse_lrc
from ui import (
    print_banner, print_status, print_song_change,
    print_config_loaded, print_connection_status,
    print_exit, colorize, Color, Icon,
    get_display_width, truncate_by_display_width
)
from hmd_battery import HMDBatteryMonitor, HMDBatteryInfo
from moekoe_client import MoeKoeClient, MoeKoeEventType, MoeKoeData, MoeKoeLyricLine

# GSMTC 客户端（用于精确进度）
try:
    from gsmtc_client import GSMTCCient, GSMTCConfig, APP_IDS
    GSMTCAVAILABLE = True
except ImportError:
    GSMTCAVAILABLE = False


# === 常量定义 ===
VRCHAT_CHATBOX_MAX_LENGTH = 144  # VRChat chatbox 消息最大长度
STATS_FILE = "play_stats.json"   # 播放统计文件


class PlayStats:
    """播放统计（持久化）"""
    
    def __init__(self, stats_file: str = STATS_FILE):
        self.stats_file = stats_file
        self.today_plays = 0          # 今日播放次数
        self.total_plays = 0          # 总播放次数
        self.songs_played = set()     # 播放过的歌曲
        self.last_date = ""           # 上次日期
        self.last_song = ""           # 上一首歌
        self._load()
    
    def _load(self):
        """从文件加载统计数据"""
        if os.path.exists(self.stats_file):
            try:
                with open(self.stats_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    self.today_plays = data.get('today_plays', 0)
                    self.total_plays = data.get('total_plays', 0)
                    self.songs_played = set(data.get('songs_played', []))
                    self.last_date = data.get('last_date', '')
                    self.last_song = data.get('last_song', '')
                    
                    # 检查是否新的一天
                    today = time.strftime('%Y-%m-%d')
                    if self.last_date != today:
                        self.today_plays = 0
                        self.last_date = today
                        self._save()
            except Exception as e:
                print(f"加载统计失败: {e}")
    
    def _save(self):
        """保存统计数据到文件"""
        try:
            data = {
                'today_plays': self.today_plays,
                'total_plays': self.total_plays,
                'songs_played': list(self.songs_played),
                'last_date': self.last_date,
                'last_song': self.last_song
            }
            with open(self.stats_file, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except Exception as e:
            print(f"保存统计失败: {e}")
    
    def record_song(self, song: str, artist: str = "") -> bool:
        """
        记录一首歌曲播放
        
        Returns:
            是否是新歌曲（首次播放）
        """
        song_key = f"{song} - {artist}" if artist else song
        
        # 检查是否是同一首歌（避免重复计数）
        if song_key == self.last_song:
            return False
        
        self.last_song = song_key
        self.today_plays += 1
        self.total_plays += 1
        
        is_new = song_key not in self.songs_played
        self.songs_played.add(song_key)
        
        # 更新日期
        today = time.strftime('%Y-%m-%d')
        if self.last_date != today:
            self.today_plays = 1
            self.last_date = today
        
        self._save()
        return is_new
    
    def get_stats_str(self) -> str:
        """获取统计字符串"""
        return f"今日 {self.today_plays} 首 | 共 {len(self.songs_played)} 首歌"


CALIBRATION_FILE = "calibration.json"

class CalibrationManager:
    """校准偏移管理器 - 按歌曲记录校准偏移量"""
    
    def __init__(self, calib_file: str = CALIBRATION_FILE):
        self.calib_file = calib_file
        self.offsets: dict[str, float] = {}  # song_hash -> offset(seconds)
        self.global_offset: float = 0  # 全局偏移
        self._load()
    
    def _load(self):
        """加载校准数据"""
        try:
            if os.path.exists(self.calib_file):
                with open(self.calib_file, 'r', encoding='utf-8') as f:
                    data = json.load(f)
                    self.offsets = data.get('offsets', {})
                    self.global_offset = data.get('global_offset', 0)
        except Exception as e:
            logger.debug(f"加载校准数据失败: {e}")
    
    def _save(self):
        """保存校准数据"""
        try:
            data = {
                'offsets': self.offsets,
                'global_offset': self.global_offset
            }
            with open(self.calib_file, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except Exception as e:
            logger.debug(f"保存校准数据失败: {e}")
    
    def get_offset(self, song_hash: str = None) -> float:
        """获取校准偏移（歌曲偏移 + 全局偏移）"""
        offset = self.global_offset
        if song_hash and song_hash in self.offsets:
            offset += self.offsets[song_hash]
        return offset
    
    def set_offset(self, offset: float, song_hash: str = None):
        """设置校准偏移"""
        if song_hash:
            self.offsets[song_hash] = offset
        else:
            self.global_offset = offset
        self._save()
    
    def adjust_offset(self, delta: float, song_hash: str = None):
        """调整校准偏移"""
        if song_hash:
            current = self.offsets.get(song_hash, 0)
            self.offsets[song_hash] = current + delta
        else:
            self.global_offset += delta
        self._save()
    
    def reset_offset(self, song_hash: str = None):
        """重置校准偏移"""
        if song_hash and song_hash in self.offsets:
            del self.offsets[song_hash]
        elif song_hash is None:
            self.global_offset = 0
        self._save()


class DisplayMode(Enum):
    """显示模式枚举"""
    FULL = "full"           # 完整模式：歌曲信息 + 进度条 + 歌词 + 电量
    COMPACT = "compact"     # 简洁模式：歌曲信息 + 进度时间
    LYRIC = "lyric"         # 歌词优先：最大化歌词显示空间
    MINIMAL = "minimal"     # 极简模式：仅歌曲名
    
    @classmethod
    def from_string(cls, value: str) -> 'DisplayMode':
        """从字符串创建枚举"""
        try:
            return cls(value.lower())
        except ValueError:
            return cls.FULL


class EmojiTheme(Enum):
    """Emoji主题枚举"""
    DEFAULT = "default"     # 默认主题：🎵 ♫ ▓░
    MUSIC = "music"         # 音乐主题：🎶 🎧 ▮▯
    MIC = "mic"             # 麦克风主题：🎤 🎙 ▣▢
    HEART = "heart"         # 心形主题：❤️ 💕 ▮▯
    STAR = "star"           # 星形主题：⭐ ✨ ▓░
    NEON = "neon"           # 霓虹主题：💡 🔮 █░
    KAWAII = "kawaii"       # 可爱主题：🌸 💖 ●○
    
    @classmethod
    def from_string(cls, value: str) -> 'EmojiTheme':
        """从字符串创建枚举"""
        try:
            return cls(value.lower())
        except ValueError:
            return cls.DEFAULT
    
    def get_icons(self) -> dict:
        """获取主题图标配置"""
        themes = {
            EmojiTheme.DEFAULT: {
                'music': '🎵', 'note': '♫', 'pause': '⏸', 
                'bar_filled': '▓', 'bar_empty': '░',
                'playing': '▶', 'done': '✓', 'separator': '|'
            },
            EmojiTheme.MUSIC: {
                'music': '🎶', 'note': '🎧', 'pause': '⏸',
                'bar_filled': '▮', 'bar_empty': '▯',
                'playing': '▶', 'done': '✓', 'separator': '│'
            },
            EmojiTheme.MIC: {
                'music': '🎤', 'note': '🎙', 'pause': '⏸',
                'bar_filled': '▣', 'bar_empty': '▢',
                'playing': '▶', 'done': '✓', 'separator': '┃'
            },
            EmojiTheme.HEART: {
                'music': '❤️', 'note': '💕', 'pause': '💔',
                'bar_filled': '♥', 'bar_empty': '♡',
                'playing': '▶', 'done': '✓', 'separator': '│'
            },
            EmojiTheme.STAR: {
                'music': '⭐', 'note': '✨', 'pause': '💫',
                'bar_filled': '★', 'bar_empty': '☆',
                'playing': '▶', 'done': '✓', 'separator': '|'
            },
            EmojiTheme.NEON: {
                'music': '💡', 'note': '🔮', 'pause': '🌑',
                'bar_filled': '█', 'bar_empty': '░',
                'playing': '▶', 'done': '✓', 'separator': '║'
            },
            EmojiTheme.KAWAII: {
                'music': '🌸', 'note': '💖', 'pause': '🥺',
                'bar_filled': '●', 'bar_empty': '○',
                'playing': '▶', 'done': '✓', 'separator': '│'
            }
        }
        return themes.get(self, themes[EmojiTheme.DEFAULT])


class SongTag(Enum):
    """歌曲标签枚举"""
    HOT = "hot"             # 热门
    NEW = "new"             # 新歌
    FAVORITE = "favorite"   # 收藏
    VIP = "vip"             # VIP专属
    ORIGINAL = "original"   # 原创
    COVER = "cover"         # 翻唱
    LIVE = "live"           # 现场
    REMIX = "remix"         # 混音
    
    def get_display(self) -> str:
        """获取标签显示文本"""
        tags = {
            SongTag.HOT: '🔥',
            SongTag.NEW: '🆕',
            SongTag.FAVORITE: '❤️',
            SongTag.VIP: '👑',
            SongTag.ORIGINAL: '🎵',
            SongTag.COVER: '🎤',
            SongTag.LIVE: '🎪',
            SongTag.REMIX: '🎧'
        }
        return tags.get(self, '')


@dataclass
class Config:
    """配置数据类"""
    interval: float = 1.0  # 检测间隔
    osc_ip: str = "127.0.0.1"
    osc_port: int = 9000
    min_send_interval: float = 4.0  # 最小发送间隔（避免VRChat速率限制，建议4秒以上）
    resend_interval: float = 15.0  # 强制重发间隔
    show_lyrics: bool = True  # 是否显示歌词
    prefer_chinese: bool = True  # 优先显示中文歌词/翻译
    cache_dir: str = "lyrics_cache"  # 歌词缓存目录
    display_mode: DisplayMode = DisplayMode.FULL  # 显示模式
    emoji_theme: EmojiTheme = EmojiTheme.DEFAULT  # Emoji主题
    show_tags: bool = True  # 是否显示歌曲标签
    show_album: bool = False  # 是否显示专辑名
    show_loudness: bool = True  # 是否显示音量标准化信息
    # 歌词显示设置
    show_next_line: bool = True  # 显示下一句预览
    word_highlight: bool = True  # 逐字高亮
    show_progress_in_lyric: bool = True  # 在歌词优先模式显示进度
    lyric_advance: float = 0.5  # 歌词提前显示时间（秒），补偿延迟
    # 翻译设置
    translation_enabled: bool = False  # 是否启用歌词翻译
    translation_service: str = "google"  # 翻译服务
    translation_target_lang: str = "zh-CN"  # 目标语言
    translation_show_original: bool = True  # 是否同时显示原文
    # 暂停显示设置
    pause_show_full_info: bool = True  # 暂停时显示完整信息
    pause_show_lyrics_preview: bool = True  # 暂停时显示歌词预览
    pause_preview_lines: int = 3  # 预览歌词行数
    pause_show_translation: bool = True  # 暂停时显示翻译
    # 歌词翻译配置
    show_translation: bool = True  # 是否显示歌词翻译
    max_lyric_lines: int = 4  # 最大歌词显示行数
    # 翻译服务配置
    translation_enabled: bool = False  # 是否启用翻译
    translation_service: str = "google"  # 翻译服务
    translation_target_lang: str = "zh-CN"  # 目标语言
    # 缓存配置
    cache_max_size_mb: int = 50  # 缓存最大大小（MB）
    cache_max_age_days: int = 30  # 缓存最大天数
    # OSC服务端配置（接收VRChat命令）
    osc_server_enabled: bool = False
    osc_server_port: int = 9001
    # 头显电量监测配置
    show_hmd_battery: bool = True  # 是否显示头显电量
    hmd_battery_interval: float = 10.0  # 电量刷新间隔（秒）
    # MoeKoeMusic 配置
    moekoe_host: str = "127.0.0.1"  # WebSocket 主机
    moekoe_port: int = 6520  # WebSocket 端口
    # 无歌词提示句子
    no_lyric_messages: list = field(default_factory=lambda: [
        "🎵 歌词离家出走了，暂未找回",
        "🎵 没找到歌词，但旋律很美",
    ])


@dataclass
class PlaybackState:
    """播放状态管理类"""
    last_song: Optional[str] = None
    last_song_name: str = ""  # 实际歌曲名（用于模糊匹配）
    last_artist_name: str = ""  # 实际歌手名（用于模糊匹配）
    current_message: str = ""
    last_send_time: float = 0
    song_info_data: Optional[SongInfo] = None
    lyric_lines: list = field(default_factory=list)
    song_start_time: float = 0
    total_paused_time: float = 0
    pause_start_time: Optional[float] = None
    last_progress: float = 0
    is_paused: bool = False
    last_lyric_time: Optional[float] = None  # 上次歌词时间点
    last_lyric_text: str = ""  # 上次歌词文本（用于检测歌词变化）
    calibration_offset: float = 0  # 进度校准偏移量
    last_calibration_time: float = 0  # 上次校准时间
    lyrics_loading: bool = False  # 歌词是否正在加载
    translation_pending: bool = False  # 翻译是否正在进行
    has_translation: bool = False  # 是否有翻译结果
    song_just_changed: bool = False  # 歌曲刚切换，需要立即发送
    last_lyric_line_index: int = -1  # 上次发送的歌词行索引
    # 自动校准相关
    auto_calibrate_samples: list = field(default_factory=list)  # 校准采样
    auto_calibrate_done: bool = False  # 当前歌曲是否已完成自动校准
    
    def reset(self, current_time: float = 0, reset_song: bool = True):
        """重置播放状态"""
        if reset_song:
            self.last_song = None
            self.last_song_name = ""
            self.last_artist_name = ""
        self.current_message = ""
        # 保留last_send_time，避免歌曲切换后立即发送
        self.song_info_data = None
        self.lyric_lines = []
        self.song_start_time = current_time
        self.total_paused_time = 0
        self.pause_start_time = None
        self.last_progress = 0
        self.is_paused = False
        self.last_lyric_time = None
        self.last_lyric_text = ""
        self.calibration_offset = 0
        self.last_calibration_time = 0
        self.lyrics_loading = False
        self.song_just_changed = True  # 标记歌曲刚切换，需要等待后发送
        self.last_lyric_line_index = -1  # 重置歌词行索引
        self.auto_calibrate_samples = []  # 重置自动校准采样
        self.auto_calibrate_done = False  # 重置自动校准状态
        self.calibration_window = []  # 重置校准窗口
        self.large_offset_count = 0  # 重置大偏移计数


def load_config(config_path: str = "config.json") -> Config:
    """加载配置文件"""
    config = Config()
    
    if os.path.exists(config_path):
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            # 基本参数（带验证）
            config.interval = max(0.1, min(2.0, data.get('interval', config.interval)))
            config.min_send_interval = max(2.0, min(30.0, data.get('min_send_interval', config.min_send_interval)))
            config.resend_interval = max(5.0, min(60.0, data.get('resend_interval', config.resend_interval)))
            config.show_lyrics = data.get('show_lyrics', config.show_lyrics)
            config.prefer_chinese = data.get('prefer_chinese', config.prefer_chinese)
            config.cache_dir = data.get('cache_dir', config.cache_dir)
            config.display_mode = DisplayMode.from_string(data.get('display_mode', 'full'))
            config.emoji_theme = EmojiTheme.from_string(data.get('emoji_theme', 'default'))
            config.show_tags = data.get('show_tags', config.show_tags)
            config.show_album = data.get('show_album', config.show_album)
            config.show_loudness = data.get('show_loudness', config.show_loudness)
            
            # 歌词显示设置
            if 'lyric_settings' in data:
                lyric_settings = data['lyric_settings']
                config.show_next_line = lyric_settings.get('show_next_line', config.show_next_line)
                config.word_highlight = lyric_settings.get('word_highlight', config.word_highlight)
                config.show_progress_in_lyric = lyric_settings.get('show_progress_in_lyric', config.show_progress_in_lyric)
                config.show_translation = lyric_settings.get('show_translation', config.show_translation)
                config.max_lyric_lines = max(2, min(8, lyric_settings.get('max_lines', config.max_lyric_lines)))
                config.lyric_advance = lyric_settings.get('advance', config.lyric_advance)
            
            # 翻译设置
            if 'translation' in data:
                translation = data['translation']
                config.translation_enabled = translation.get('enabled', config.translation_enabled)
                config.translation_service = translation.get('service', config.translation_service)
                config.translation_target_lang = translation.get('target_lang', config.translation_target_lang)
            
            # 缓存设置
            if 'cache' in data:
                cache = data['cache']
                config.cache_max_size_mb = max(10, min(500, cache.get('max_size_mb', config.cache_max_size_mb)))
                config.cache_max_age_days = max(1, min(365, cache.get('max_age_days', config.cache_max_age_days)))
            
            # 暂停显示设置
            if 'pause_display' in data:
                pause_display = data['pause_display']
                config.pause_show_full_info = pause_display.get('show_full_info', config.pause_show_full_info)
                config.pause_show_lyrics_preview = pause_display.get('show_lyrics_preview', config.pause_show_lyrics_preview)
                config.pause_preview_lines = max(1, min(10, pause_display.get('preview_lines', config.pause_preview_lines)))
                config.pause_show_translation = pause_display.get('show_translation', config.pause_show_translation)
            
            if 'osc' in data:
                config.osc_ip = data['osc'].get('ip', config.osc_ip)
                config.osc_port = max(1, min(65535, data['osc'].get('port', config.osc_port)))
            
            # OSC服务端配置
            if 'osc_server' in data:
                osc_server = data['osc_server']
                config.osc_server_enabled = osc_server.get('enabled', config.osc_server_enabled)
                config.osc_server_port = max(1, min(65535, osc_server.get('port', config.osc_server_port)))
            
            # 头显电量监测配置
            if 'hmd_battery' in data:
                hmd_battery = data['hmd_battery']
                config.show_hmd_battery = hmd_battery.get('enabled', config.show_hmd_battery)
                config.hmd_battery_interval = max(5.0, min(60.0, hmd_battery.get('interval', config.hmd_battery_interval)))
            
            # MoeKoeMusic 配置
            if 'moekoe' in data:
                moekoe = data['moekoe']
                config.moekoe_host = moekoe.get('host', config.moekoe_host)
                config.moekoe_port = max(1, min(65535, moekoe.get('port', config.moekoe_port)))
            
            # 无歌词提示句子
            if 'no_lyric_messages' in data and isinstance(data['no_lyric_messages'], list):
                config.no_lyric_messages = data['no_lyric_messages']
            
            print_config_loaded(config_path)
            
        except Exception as e:
            print_status('warning', f"配置文件读取失败，使用默认配置: {e}")
    else:
        print_status('info', "未找到配置文件，使用默认配置")
    
    return config


def check_environment() -> bool:
    """检查 VRChat OSC 环境"""
    osc = VRChatOSC()
    vrc_status = osc.get_status()
    vrc_connected = vrc_status.is_connected
    
    print_connection_status(vrc_connected)
    
    return vrc_connected


def _truncate_song_info(song: str, artist: str, max_width: int = 50) -> str:
    """
    智能截断歌曲信息，优先保留核心内容
    
    处理策略：
    1. 移除括号后缀中的常见冗余词（如"Remix"、"翻唱"等保留，其他移除）
    2. 多歌手时只显示前2个，后续用"等"替代
    3. 按显示宽度截断，确保不超限
    """
    import re
    
    # 清理歌曲名中的冗余括号内容
    def clean_song_name(name: str) -> str:
        # 保留的有意义的括号内容关键词
        keep_keywords = ['remix', 'mix', 'ver', 'version', 'edit', 'cover', '翻唱', 
                         '原唱', 'live', 'acoustic', 'demo', 'inst', '伴奏']
        
        # 移除纯数字或年份括号 [2023]、(2024) 等
        name = re.sub(r'[\[（(]\s*\d{4}\s*[\]）)]', '', name)
        
        # 移除包含"高品质"、"MV"、"VIP"等的括号
        name = re.sub(r'[\[（(][^\]）)]*(?:高品质|MV|VIP|试听|独家|官方|HD|HQ)[^\]）)]*[\]）)]', '', name, flags=re.IGNORECASE)
        
        # 检查括号内容是否需要保留
        def process_brackets(text):
            result = text
            # 找到所有括号
            brackets = re.findall(r'([\[（(][^\]）)]*[\]）)])', text)
            for bracket in brackets:
                inner = bracket[1:-1].strip()
                # 如果括号内容包含保留关键词，则保留
                should_keep = any(kw in inner.lower() for kw in keep_keywords)
                if not should_keep and len(inner) > 8:  # 过长的括号内容移除
                    result = result.replace(bracket, '')
            return result
        
        name = process_brackets(name)
        name = re.sub(r'\s+', ' ', name).strip()
        return name
    
    # 处理多歌手（只显示第一个）
    def simplify_artist(artist_str: str) -> str:
        # 分隔符：中文顿号、英文逗号、&符号
        artists = re.split(r'[、,，&]+', artist_str)
        artists = [a.strip() for a in artists if a.strip()]
        
        if len(artists) > 1:
            return f"{artists[0]}等"
        return artist_str
    
    # 清理后的歌曲名和歌手
    clean_song = clean_song_name(song)
    clean_artist = simplify_artist(artist) if artist and artist != "未知歌手" else ""
    
    # 构建显示文本
    if clean_artist:
        result = f"{clean_song} - {clean_artist}"
    else:
        result = clean_song
    
    # 按显示宽度截断
    if get_display_width(result) > max_width:
        # 优先截断歌手部分
        if clean_artist and "等" not in clean_artist:
            # 尝试缩短歌手
            simple_artist = clean_artist.split('、')[0] if '、' in clean_artist else clean_artist[:6]
            result = f"{clean_song} - {simple_artist}.."
        
        # 如果还是太长，截断歌曲名
        if get_display_width(result) > max_width:
            available = max_width - (get_display_width(f" - {clean_artist}") if clean_artist else 0) - 2
            truncated_song = truncate_by_display_width(clean_song, max(10, available))
            result = f"{truncated_song}.. - {clean_artist}" if clean_artist else f"{truncated_song}.."
    
    return result


def build_display_message(song: str, artist: str, lyric: str = None,
                          current_time: float = 0, total_time: float = 0,
                          status_suffix: str = "", hmd_battery: HMDBatteryInfo = None,
                          mode: DisplayMode = DisplayMode.FULL,
                          current_lyric_line: LyricLine = None,
                          next_lyric_line: LyricLine = None,
                          config: 'Config' = None,
                          tags: list = None) -> str:
    """
    构建显示消息（支持多种显示模式和Emoji主题）
    
    模式说明：
    - FULL: 完整模式 - 歌曲信息 + 进度条 + 歌词 + 电量
    - COMPACT: 简洁模式 - 歌曲信息 + 进度时间（无歌词）
    - LYRIC: 歌词优先 - 最大化歌词显示空间（当前句+下一句预览）
    - MINIMAL: 极简模式 - 仅歌曲名和歌手
    
    Emoji主题：
    - DEFAULT: 🎵 ♫ ▓░
    - MUSIC: 🎶 🎧 ▮▯
    - MIC: 🎤 🎙 ▣▢
    - HEART: ❤️ 💕 ♥♡
    - STAR: ⭐ ✨ ★☆
    - NEON: 💡 🔢 █░
    - KAWAII: 🌸 💖 ●○
    
    歌曲标签：
    - 🔥热门 🆕新歌 ❤️收藏 👑VIP
    - 🎵原创 🎤翻唱 🎪现场 🎧混音
    """
    # 获取配置（如果没有传入则使用默认值）
    show_next_line = config.show_next_line if config else True
    word_highlight = config.word_highlight if config else True
    emoji_theme = config.emoji_theme if config else EmojiTheme.DEFAULT
    show_tags = config.show_tags if config else True
    
    # 获取主题图标
    icons = emoji_theme.get_icons()
    icon_music = icons['music']
    icon_note = icons['note']
    icon_pause = icons['pause']
    icon_bar_filled = icons['bar_filled']
    icon_bar_empty = icons['bar_empty']
    icon_playing = icons['playing']
    icon_done = icons['done']
    icon_separator = icons['separator']
    
    # 构建标签字符串
    tag_str = ""
    if show_tags and tags:
        tag_icons = [tag.get_display() for tag in tags if isinstance(tag, SongTag)]
        if tag_icons:
            tag_str = " " + " ".join(tag_icons)
    
    # 根据模式决定歌曲信息的最大宽度
    if mode == DisplayMode.LYRIC:
        song_max_width = 18 if tag_str else 20  # 有标签时进一步限制
    elif mode == DisplayMode.FULL and lyric:
        song_max_width = 32 if tag_str else 35
    else:
        song_max_width = 45 if tag_str else 50
    
    # 智能截断歌名和歌手
    song_part = _truncate_song_info(song, artist, song_max_width)
    
    # 构建逐字高亮歌词
    display_lyric = lyric
    if current_lyric_line and current_lyric_line.words and word_highlight:
        # 有逐字信息，高亮显示当前正在唱的字
        idx, sung, unsung = current_lyric_line.get_word_at_time(current_time)
        if unsung:  # 还有未唱的字
            if idx < len(current_lyric_line.words):
                current_word = current_lyric_line.words[idx]
                # 使用「」标记当前正在唱的字
                display_lyric = f"{sung}「{current_word.text}」{unsung[len(current_word.text):]}"
            else:
                display_lyric = lyric
        elif sung:  # 这句已经唱完
            display_lyric = f"{icon_done}{sung}"
        
        # 添加下一句预览（在歌词优先模式）
        if mode == DisplayMode.LYRIC and show_next_line and next_lyric_line and next_lyric_line.text:
            # 截取下一句的开头部分
            next_preview = truncate_by_display_width(next_lyric_line.text, 15)
            display_lyric = f"{display_lyric} › {next_preview}"
    
    # 构建主题化进度条
    def build_themed_progress_bar(current: float, total: float, width: int) -> str:
        if total <= 0:
            return icon_bar_empty * width
        progress = min(current / total, 1.0)
        filled = int(progress * width)
        return icon_bar_filled * filled + icon_bar_empty * (width - filled)
    
    # 极简模式：只显示歌曲名
    if mode == DisplayMode.MINIMAL:
        return f"{icon_note} {song_part}{tag_str}{status_suffix}"
    
    # 简洁模式：歌曲信息 + 进度时间
    if mode == DisplayMode.COMPACT:
        line1 = f"{icon_note} {song_part}{tag_str}{status_suffix}"
        if hmd_battery:
            line1 += f" {icon_separator} {hmd_battery}"
        
        if total_time > 0:
            time_str = f"{format_time(current_time)}/{format_time(total_time)}"
            bar = build_themed_progress_bar(current_time, total_time, 6)
            return f"{line1}\n{bar} {time_str}"
        return line1
    
    # 歌词优先模式：最大化歌词空间
    if mode == DisplayMode.LYRIC:
        line1 = f"{icon_note} {song_part}{tag_str}{status_suffix}"
        if hmd_battery:
            line1 += f" {icon_separator} {hmd_battery}"
        
        if display_lyric:
            # 计算歌词最大可用宽度
            max_lyric_width = VRCHAT_CHATBOX_MAX_LENGTH - get_display_width(line1) - 1  # -1 for \n
            if get_display_width(display_lyric) > max_lyric_width:
                final_lyric = truncate_by_display_width(display_lyric, max_lyric_width - 2) + ".."
            else:
                final_lyric = display_lyric
            return f"{line1}\n{final_lyric}"
        elif total_time > 0:
            # 没有歌词时显示进度
            bar = build_themed_progress_bar(current_time, total_time, 10)
            time_str = f"{format_time(current_time)}/{format_time(total_time)}"
            return f"{line1}\n{bar} {time_str}"
        return line1
    
    # 完整模式（默认）
    # 第一行：歌名 + 状态 + 电量
    line1_parts = [f"{icon_note} {song_part}{tag_str}{status_suffix}"]
    if hmd_battery:
        line1_parts.append(str(hmd_battery))
    line1 = f" {icon_separator} ".join(line1_parts)
    
    # 第二行：进度条 + 时间 + 歌词
    line2_parts = []
    if total_time > 0:
        bar = build_themed_progress_bar(current_time, total_time, 8)
        time_str = f"{format_time(current_time)}/{format_time(total_time)}"
        line2_parts.append(f"{bar} {time_str}")
    
    if display_lyric:
        used = get_display_width(f" {icon_separator} ") + (get_display_width(line2_parts[0]) if line2_parts else 0)
        max_lyric_width = VRCHAT_CHATBOX_MAX_LENGTH - get_display_width(line1) - 1 - used
        if max_lyric_width > 15:
            if get_display_width(display_lyric) > max_lyric_width:
                final_lyric = truncate_by_display_width(display_lyric, max_lyric_width - 2) + ".."
            else:
                final_lyric = display_lyric
            line2_parts.append(final_lyric)    
    if line2_parts:
        line2 = f" {icon_separator} ".join(line2_parts)
        return f"{line1}\n{line2}"
    
    return line1


def get_lyric_display_text(lines: list, current_idx: int, max_length: int = 144, show_translation: bool = False) -> str:
    """
    获取歌词显示文本（简化版）
    
    只显示当前正在唱的歌词行
    """
    if not lines or current_idx < 0 or current_idx >= len(lines):
        return None
    
    current_line = lines[current_idx]
    if not current_line.text:
        return None
    
    # 只返回当前行歌词，不添加翻译
    current_text = current_line.text.strip()
    
    # 如果过长，智能截断
    current_width = get_display_width(current_text)
    if current_width > max_length:
        truncated = truncate_by_display_width(current_text, max_length - 2)
        return truncated + ".."
    
    return current_text


def run_monitor(config: Config):
    """运行音乐监控程序"""
    print_banner()
    
    print(colorize('\n环境检测', Color.BRIGHT_WHITE))
    print(colorize('─' * 54, Color.DIM))
    vrc_connected = check_environment()
    
    if not vrc_connected:
        print()
        print_status('warning', "VRChat OSC未连接，消息将无法发送")
        print_status('info', "请在VRChat设置中开启OSC功能")
    
    # 缓存清理
    try:
        from lyrics import LyricCache
        cache = LyricCache(config.cache_dir)
        result = cache.cleanup(
            max_size_mb=config.cache_max_size_mb,
            max_age_days=config.cache_max_age_days
        )
        if result['deleted'] > 0:
            print_status('info', f"缓存清理: 删除 {result['deleted']} 个文件，释放 {result['freed_mb']} MB")
    except Exception as e:
        logger.debug(f"缓存清理失败: {e}")
    
    # 当前歌曲状态（用于分享功能）
    current_song_state = {'song': None, 'artist': None}
    
    # 播放统计
    play_stats = PlayStats()
    print_status('info', play_stats.get_stats_str())
    
    # 校准管理器（按歌曲记住偏移量）
    calibration_mgr = CalibrationManager()
    if calibration_mgr.global_offset != 0:
        print_status('info', f"全局校准偏移: {calibration_mgr.global_offset:+.1f}秒")
    
    # 初始化翻译器（如果启用）
    translator = None
    if config.translation_enabled:
        try:
            from translator import LyricTranslator, TranslationConfig
            trans_config = TranslationConfig(
                enabled=True,
                service=config.translation_service,
                target_lang=config.translation_target_lang
            )
            translator = LyricTranslator(trans_config)
            if translator.is_available():
                print_status('success', f"翻译服务已启用 ({config.translation_service})")
            else:
                print_status('warning', "翻译服务初始化失败")
                translator = None
        except ImportError:
            print_status('warning', "翻译模块未安装，请安装 deep-translator")
        except Exception as e:
            logger.debug(f"翻译器初始化失败: {e}")
    
    # 初始化OSC命令服务端
    osc_server = None
    if config.osc_server_enabled:
        osc_server = OSCCommandServer(config.osc_server_port)
        
        def on_share_song(address, *args):
            """处理分享歌曲命令"""
            song = current_song_state.get('song')
            artist = current_song_state.get('artist')
            if song:
                share_osc = VRChatOSC(config.osc_ip, config.osc_port)
                if artist and artist != "未知歌手":
                    msg = f"🎵 正在听: {song} - {artist}"
                else:
                    msg = f"🎵 正在听: {song}"
                # 使用立即发送模式，跳过队列
                share_osc.send_immediate(msg)
                print_status('success', f"已分享歌曲: {song}")
            else:
                print_status('warning', "当前没有播放歌曲")
        
        def on_switch_mode(address, *args):
            """处理切换显示模式命令"""
            modes = list(DisplayMode)
            current_idx = modes.index(config.display_mode)
            next_idx = (current_idx + 1) % len(modes)
            config.display_mode = modes[next_idx]
            
            mode_names = {
                DisplayMode.FULL: "完整",
                DisplayMode.COMPACT: "简洁",
                DisplayMode.LYRIC: "歌词优先",
                DisplayMode.MINIMAL: "极简"
            }
            print_status('info', f"显示模式: {mode_names[config.display_mode]}")
        
        def on_set_mode(address, *args):
            """处理设置特定显示模式命令"""
            if args and isinstance(args[0], str):
                try:
                    config.display_mode = DisplayMode.from_string(args[0])
                    mode_names = {
                        DisplayMode.FULL: "完整",
                        DisplayMode.COMPACT: "简洁",
                        DisplayMode.LYRIC: "歌词优先",
                        DisplayMode.MINIMAL: "极简"
                    }
                    print_status('success', f"显示模式已设为: {mode_names[config.display_mode]}")
                except Exception:
                    print_status('warning', f"无效的显示模式: {args[0]}")
        
        def on_calibrate(address, *args):
            """处理进度校准命令"""
            current_hash = moekoe_data.song.hash if moekoe_data and moekoe_data.song else None
            
            if address.endswith('/forward') or address.endswith('/slow'):
                # 歌词显示慢了，往前调整
                state.calibration_offset += 0.3
                if current_hash:
                    calibration_mgr.adjust_offset(0.3, current_hash)
                print_status('info', f"歌词提前: +0.3秒 (累计: {state.calibration_offset:+.1f}秒)")
            elif address.endswith('/back') or address.endswith('/fast'):
                # 歌词显示快了，往后调整
                state.calibration_offset -= 0.3
                if current_hash:
                    calibration_mgr.adjust_offset(-0.3, current_hash)
                print_status('info', f"歌词延后: -0.3秒 (累计: {state.calibration_offset:+.1f}秒)")
            elif address.endswith('/reset'):
                state.calibration_offset = 0
                if current_hash:
                    calibration_mgr.reset_offset(current_hash)
                print_status('info', "进度校准已重置")
            else:
                # 显示当前校准状态
                print_status('info', f"当前校准偏移: {state.calibration_offset:+.1f}秒")
                print_status('info', f"全局偏移: {calibration_mgr.global_offset:+.1f}秒")
        
        def on_stats(address, *args):
            """处理查看统计命令"""
            stats = osc.stats
            print_status('info', f"发送统计: 成功{stats['total_sent']} 失败{stats['total_failed']} 丢弃{stats['total_dropped']}")
            print_status('info', f"速率限制触发: {stats['rate_limit_hits']}次 队列: {stats['queue_size']}")
        
        # 注册命令回调
        osc_server.register_callback("/music/share", on_share_song)
        osc_server.register_callback("/music/mode", on_switch_mode)
        osc_server.register_callback("/music/mode/set", on_set_mode)
        osc_server.register_callback("/music/calibrate", on_calibrate)
        osc_server.register_callback("/music/calibrate/forward", on_calibrate)
        osc_server.register_callback("/music/calibrate/back", on_calibrate)
        osc_server.register_callback("/music/calibrate/reset", on_calibrate)
        # 快捷命令：歌词慢了/快了
        osc_server.register_callback("/music/slow", on_calibrate)   # 歌词慢了，往前调
        osc_server.register_callback("/music/fast", on_calibrate)   # 歌词快了，往后调
        
        if osc_server.start():
            print_status('success', f"OSC命令服务已启动 (端口 {config.osc_server_port})")
        else:
            print_status('warning', "OSC命令服务启动失败")
            osc_server = None
    
    # 初始化头显电量监测器
    hmd_monitor = None
    hmd_battery_info = None
    last_battery_check_time = 0
    
    if config.show_hmd_battery:
        hmd_monitor = HMDBatteryMonitor()
        if hmd_monitor.init():
            print_status('success', "头显电量监测已启用")
            # 立即获取一次电量
            hmd_battery_info = hmd_monitor.get_battery_info(force_refresh=True)
            if hmd_battery_info:
                print_status('info', f"头显电量: {hmd_battery_info}")
        else:
            print_status('warning', f"头显电量监测初始化失败: {hmd_monitor.init_error}")
            hmd_monitor = None
    
    # 初始化 MoeKoeMusic 客户端
    moekoe_client = MoeKoeClient(config.moekoe_host, config.moekoe_port)
    moekoe_data = None
    
    # 初始化 GSMTC 客户端（用于精确进度）
    gsmtc_client = None
    if GSMTCAVAILABLE:
        gsmtc_config = GSMTCConfig(target_app_id=APP_IDS['moekoe'], poll_interval=0.1)
        gsmtc_client = GSMTCCient(gsmtc_config)
        if gsmtc_client.start():
            print_status('success', "GSMTC 已启用 (精确进度)")
        else:
            gsmtc_client = None
            print_status('warning', "GSMTC 启动失败，将使用 WebSocket 进度")
    
    if not moekoe_client.is_available():
        print_status('error', "需要安装 websocket-client 库")
        print_status('info', "请运行: pip install websocket-client")
        return
    
    if not moekoe_client.connect():
        print_status('error', "MoeKoeMusic 连接失败")
        print_status('info', "请确保 MoeKoeMusic 已启动并开启 API 模式")
        return
    
    print_status('success', f"MoeKoeMusic 已连接 ({config.moekoe_host}:{config.moekoe_port})")
    
    # MoeKoeMusic 回调：更新本地数据
    def on_moekoe_lyrics(data: MoeKoeData):
        nonlocal moekoe_data
        moekoe_data = data
    
    def on_moekoe_song_change(data: MoeKoeData):
        """MoeKoeMusic 歌曲切换回调"""
        song = data.song.name
        artist = data.song.author
        if song:
            print_song_change(song, artist)
            current_song_state['song'] = song
            current_song_state['artist'] = artist
            
            # 重置自动校准状态
            state.auto_calibrate_samples = []
            state.auto_calibrate_done = False
            
            # 自动应用已保存的校准偏移
            song_hash = data.song.hash
            if song_hash:
                saved_offset = calibration_mgr.get_offset(song_hash)
                if saved_offset != 0:
                    state.calibration_offset = saved_offset
                    print_status('info', f"自动应用校准: {saved_offset:+.1f}秒")
                else:
                    state.calibration_offset = calibration_mgr.global_offset
    
    moekoe_client.register_callback(MoeKoeEventType.LYRICS, on_moekoe_lyrics)
    moekoe_client.register_callback(MoeKoeEventType.SONG_CHANGE, on_moekoe_song_change)
    
    # 打印启动信息
    print()
    ts = colorize(f'[{time.strftime("%H:%M:%S")}]', Color.DIM)
    print(f"{ts} {Icon.INFO} 检测间隔: {colorize(f'{config.interval}秒', Color.BRIGHT_CYAN)}")
    print(f"{ts} {Icon.INFO} 最小发送间隔: {colorize(f'{config.min_send_interval}秒', Color.BRIGHT_CYAN)} (防止VRChat速率限制)")
    if config.show_lyrics:
        print(f"{ts} {Icon.INFO} 歌词显示: {colorize('开启', Color.BRIGHT_GREEN)}")
    if config.show_hmd_battery and hmd_monitor:
        print(f"{ts} {Icon.INFO} 电量监测: {colorize('开启', Color.BRIGHT_GREEN)}")
    
    # 显示当前显示模式
    mode_names = {
        DisplayMode.FULL: "完整",
        DisplayMode.COMPACT: "简洁",
        DisplayMode.LYRIC: "歌词优先",
        DisplayMode.MINIMAL: "极简"
    }
    print(f"{ts} {Icon.INFO} 显示模式: {colorize(mode_names[config.display_mode], Color.BRIGHT_CYAN)}")
    
    # 显示Emoji主题
    theme_names = {
        EmojiTheme.DEFAULT: "默认 🎵",
        EmojiTheme.MUSIC: "音乐 🎶",
        EmojiTheme.MIC: "麦克风 🎤",
        EmojiTheme.HEART: "心形 ❤️",
        EmojiTheme.STAR: "星形 ⭐",
        EmojiTheme.NEON: "霓虹 💡",
        EmojiTheme.KAWAII: "可爱 🌸"
    }
    print(f"{ts} {Icon.INFO} Emoji主题: {colorize(theme_names.get(config.emoji_theme, '默认'), Color.BRIGHT_CYAN)}")
    
    # 显示标签设置
    tags_status = colorize('开启', Color.BRIGHT_GREEN) if config.show_tags else colorize('关闭', Color.DIM)
    print(f"{ts} {Icon.INFO} 歌曲标签: {tags_status}")
    
    print(f"{ts} {Icon.INFO} 重发间隔: {colorize(f'{config.resend_interval}秒', Color.BRIGHT_CYAN)}")
    
    # 显示快捷键提示
    if osc_server:
        print()
        print(colorize('VRChat内快捷键:', Color.BRIGHT_WHITE))
        print(colorize('─' * 54, Color.DIM))
        print(f"  需在VRChat OSC配置中添加按键绑定:")
        print(f"  /music/share      - 分享当前歌曲")
        print(f"  /music/mode       - 切换显示模式")
        print(f"  /music/mode/set   - 设置模式 (参数: full/compact/lyric/minimal)")
        print(f"  /music/calibrate  - 查看校准偏移")
        print(f"  /music/slow       - 歌词慢了，提前0.3秒")
        print(f"  /music/fast       - 歌词快了，延后0.3秒")
        print(f"  /music/calibrate/reset - 重置校准")
        print(f"  /music/calibrate/reset   - 重置校准")
        print(f"  /music/stats      - 查看发送统计")
    
    print()
    print(colorize('─' * 54, Color.DIM))
    print(f"{ts} {Icon.ARROW} 开始监控... (按 Ctrl+C 停止)")
    print(colorize('─' * 54, Color.DIM))
    
    osc = VRChatOSC(config.osc_ip, config.osc_port, min_interval=config.min_send_interval)
    
    # 获取主题图标
    icons = config.emoji_theme.get_icons()
    icon_music = icons['music']
    icon_note = icons['note']
    icon_pause = icons['pause']
    icon_bar_filled = icons['bar_filled']
    icon_bar_empty = icons['bar_empty']
    icon_playing = icons['playing']
    icon_separator = icons['separator']
    
    # 构建进度条函数
    def build_themed_bar(current: float, total: float, width: int) -> str:
        if total <= 0:
            return icon_bar_empty * width
        progress = min(current / total, 1.0)
        filled = int(progress * width)
        return icon_bar_filled * filled + icon_bar_empty * (width - filled)
    
    # 播放状态管理
    state = PlaybackState()
    
    # === 性能优化：预编译正则表达式 ===
    import re as _re_module
    _NORMALIZE_PATTERN = _re_module.compile(r'[\s\-、，,\.\'\"()（）\[\]【】]')
    _song_compare_cache: dict[str, str] = {}  # 歌曲比较缓存
    
    def _normalize_for_compare(text: str) -> str:
        """标准化文本用于比较（移除空格、标点等）- 带缓存"""
        if not text:
            return ""
        # 检查缓存
        if text in _song_compare_cache:
            return _song_compare_cache[text]
        # 移除空格、标点、分隔符
        result = _NORMALIZE_PATTERN.sub('', text.lower())
        # 缓存结果（限制缓存大小）
        if len(_song_compare_cache) < 100:
            _song_compare_cache[text] = result
        return result
    
    def _is_same_song(new_song: str, new_artist: str, old_song: str, old_artist: str) -> bool:
        """判断是否是同一首歌（模糊匹配）"""
        if not old_song or not old_artist:
            return False
        # 标准化后比较
        return (_normalize_for_compare(new_song) == _normalize_for_compare(old_song) and
                _normalize_for_compare(new_artist) == _normalize_for_compare(old_artist))
    
    try:
        while True:
            current_time = time.time()
            
            # 检查 MoeKoeMusic 连接状态
            if not moekoe_client or not moekoe_client.is_connected():
                print_status('warning', "MoeKoeMusic 连接断开，等待重连...")
                time.sleep(2)
                continue
            
            # 获取最新数据
            moekoe_data = moekoe_client.data
            
            if not moekoe_data or not moekoe_data.song.name:
                time.sleep(config.interval)
                continue
            
            # 使用 MoeKoeMusic 实时数据
            song_name = moekoe_data.song.name
            artist_name = moekoe_data.song.author
            total_duration = moekoe_data.song.duration
            is_playing = moekoe_data.player_state.is_playing
            
            # 优先使用 GSMTC 精确进度
            if gsmtc_client and gsmtc_client.info.connected:
                gsmtc_info = gsmtc_client.get_current_info()
                current_play_time = gsmtc_info.current_time
                # GSMTC 的总时长更精确
                if gsmtc_info.total_time > 0:
                    total_duration = gsmtc_info.total_time
            else:
                # 回退到 WebSocket 进度
                current_play_time = moekoe_data.player_state.current_time
            
            # 添加歌词提前量（配置项，用于补偿显示延迟）
            current_play_time = current_play_time + config.lyric_advance
            
            # 检测歌曲变化（立即响应）
            is_new_song = not _is_same_song(
                song_name, artist_name,
                state.last_song_name, state.last_artist_name
            )
            
            if is_new_song:
                state.last_song = f"{song_name} - {artist_name}"
                state.last_song_name = song_name
                state.last_artist_name = artist_name
                
                # 更新当前歌曲状态
                current_song_state['song'] = song_name
                current_song_state['artist'] = artist_name
                
                # 记录播放统计
                is_new = play_stats.record_song(song_name, artist_name)
                
                # 重置状态
                state.reset(current_time, reset_song=False)
                state.song_info_data = SongInfo(
                    duration=total_duration,
                    lyrics="",
                    is_instrumental=False
                )
                state.lyrics_loading = False
                
                print_song_change(song_name, artist_name)
                
                # 歌词获取策略（优化：非阻塞）：
                # 始终从酷狗获取带时间戳的歌词（更准确）
                # MoeKoeMusic 歌词可能版本不匹配（如 MIX/剪辑版）
                
                state.lyric_lines = []
                state.lyrics_loading = True
                
                # 检查 MoeKoeMusic 是否有歌词（仅用于翻译映射）
                moekoe_has_lyrics = bool(moekoe_data.lyrics)
                moekoe_lyric_count = len(moekoe_data.lyrics) if moekoe_has_lyrics else 0
                
                # 检测 MoeKoeMusic 歌词是否是翻译（中文）
                def is_chinese_text(text: str) -> bool:
                    """检测文本是否主要是中文"""
                    if not text:
                        return False
                    chinese_chars = sum(1 for c in text if '\u4e00' <= c <= '\u9fff')
                    return chinese_chars / len(text) > 0.3
                
                moekoe_is_translation = False
                if moekoe_has_lyrics:
                    # 检查前几行是否是中文
                    sample_lines = [line.text for line in moekoe_data.lyrics[:5] if line.text]
                    if sample_lines:
                        chinese_count = sum(1 for line in sample_lines if is_chinese_text(line))
                        moekoe_is_translation = chinese_count > len(sample_lines) * 0.5
                
                # 始终从酷狗获取歌词
                current_song_hash = moekoe_data.song.hash if moekoe_data.song else None
                
                # 异步加载酷狗歌词
                def make_callback(target_hash, translator_ref, moekoe_is_trans, moekoe_lines):
                    """创建回调闭包，捕获当前歌曲hash、翻译状态和MoeKoe歌词"""
                    def on_lyrics_loaded(song, artist, song_info):
                        try:
                            if song_info and song_info.lyric_lines:
                                # 检测酷狗歌词语言
                                kugou_is_chinese = False
                                sample_lines = [line.text for line in song_info.lyric_lines[:5] if line.text]
                                if sample_lines:
                                    chinese_count = sum(1 for line in sample_lines if is_chinese_text(line))
                                    kugou_is_chinese = chinese_count > len(sample_lines) * 0.5
                                
                                # 如果 MoeKoe 是翻译（中文）且酷狗是外语，保留翻译映射
                                if moekoe_is_trans and not kugou_is_chinese and moekoe_lines:
                                    # 尝试按行数或内容匹配翻译
                                    moekoe_text_list = list(moekoe_lines.values())
                                    for i, line in enumerate(song_info.lyric_lines):
                                        # 优先按索引匹配
                                        if i < len(moekoe_text_list):
                                            song_info.translated_lines[i] = moekoe_text_list[i]
                                    if song_info.translated_lines:
                                        print_status('info', f"已映射 MoeKoe 翻译 ({len(song_info.translated_lines)}行)")
                                
                                # 更新歌词
                                state.lyric_lines = song_info.lyric_lines
                                state.song_info_data = song_info
                                has_words = any(line.words for line in song_info.lyric_lines if line.words)
                                print_status('success', f"酷狗歌词更新 ({len(state.lyric_lines)}行{'，有逐字' if has_words else ''})")
                                
                                # 异步翻译歌词（如果启用且无现有翻译）
                                if translator_ref and config.show_translation and not song_info.translated_lines:
                                    def translate_callback():
                                        try:
                                            translated = {}
                                            for i, line in enumerate(song_info.lyric_lines[:20]):
                                                if line.text and len(line.text) > 2:
                                                    trans = translator_ref.translate_text(line.text)
                                                    if trans:
                                                        translated[i] = trans
                                            if translated:
                                                song_info.translated_lines = translated
                                                print_status('info', f"翻译完成 ({len(translated)}行)")
                                        except Exception as e:
                                            logger.debug(f"翻译失败: {e}")
                                    
                                    import threading
                                    threading.Thread(target=translate_callback, daemon=True).start()
                        finally:
                            state.lyrics_loading = False
                    return on_lyrics_loaded
                
                # 启动异步加载
                try:
                    fetcher = LyricFetcher()
                    
                    # 构建 MoeKoe 翻译行字典
                    moekoe_trans_dict = {}
                    if moekoe_has_lyrics:
                        for i, line in enumerate(moekoe_data.lyrics):
                            moekoe_trans_dict[i] = line.text
                    
                    if current_song_hash:
                        # 使用 hash 异步查询
                        future = fetcher.preload_by_hash(current_song_hash, callback=make_callback(current_song_hash, translator, moekoe_is_translation, moekoe_trans_dict))
                    else:
                        # 使用歌名异步查询
                        future = fetcher.preload_song_info(song_name, artist_name, callback=make_callback(None, translator, moekoe_is_translation, moekoe_trans_dict))
                    
                    # 如果异步任务为空（命中缓存但无结果）
                    if future is None:
                        print_status('warning', "未找到歌词")
                        state.lyrics_loading = False
                        
                except Exception as e:
                    print_status('error', f"歌词加载失败: {e}")
                    state.lyrics_loading = False
            
            # 实时更新暂停状态和进度
            was_paused = state.is_paused
            state.is_paused = not is_playing
            estimated_progress = current_play_time
            
            # 暂停状态变化提示
            if state.is_paused and not was_paused:
                print_status('info', "已暂停")
            elif not state.is_paused and was_paused:
                print_status('info', "继续播放")
            
            # 获取当前歌词（直接使用时间戳匹配，GSMTC 提供精确进度）
            current_lyric_text = None
            current_lyric_obj = None
            next_lyric_obj = None
            current_lyric_line_index = -1
            
            if config.show_lyrics and state.lyric_lines:
                # 直接使用时间戳查找（精确模式）
                for i, line in enumerate(state.lyric_lines):
                    next_time = state.lyric_lines[i + 1].time if i + 1 < len(state.lyric_lines) else float('inf')
                    if line.time <= current_play_time < next_time:
                        current_lyric_text = line.text
                        current_lyric_obj = line
                        current_lyric_line_index = i
                        if i + 1 < len(state.lyric_lines):
                            next_lyric_obj = state.lyric_lines[i + 1]
                        break
            
            # 统一歌曲信息
            display_song = song_name
            display_artist = artist_name
            
            # 构建标签字符串（MoeKoeMusic 暂不支持标签）
            tag_str = ""
            
            # 构建消息
            display_progress = estimated_progress
            
            # 如果没有歌曲信息，跳过本次循环
            if not display_song:
                time.sleep(config.interval)
                continue
            
            # 暂停状态特殊处理
            if state.is_paused and config.pause_show_full_info:
                # 第一行：歌曲信息 + 时长
                duration_str = format_time(total_duration) if total_duration > 0 else ""
                song_info_short = _truncate_song_info(display_song, display_artist, max_width=40)
                pause_line1 = f"{icon_pause} {icon_note} {song_info_short}"
                if duration_str:
                    pause_line1 += f" [{duration_str}]"
                
                # 第二行：进度条 + 时间
                if total_duration > 0:
                    bar = build_themed_bar(display_progress, total_duration, 10)
                    time_str = f"{format_time(display_progress)}/{format_time(total_duration)}"
                    pause_line2 = f"{bar} {time_str}"
                else:
                    pause_line2 = ""
                
                # 第三行：播放统计 + 电量 + 音量标准化 + 专辑名
                stats_str = play_stats.get_stats_str()
                info_parts = [stats_str]
                if hmd_battery_info:
                    info_parts.append(f"🔋 {hmd_battery_info}")
                # 添加音量标准化信息
                if config.show_loudness and moekoe_data.song.loudness_normalization:
                    loudness = moekoe_data.song.loudness_normalization
                    info_parts.append(f"🔊 {loudness.volume:.1f}dB")
                pause_line3 = " | ".join(info_parts)
                
                # 专辑名（如果启用且有数据）
                album_line = ""
                if config.show_album and state.song_info_data and state.song_info_data.album_name:
                    album_line = f"💿 {state.song_info_data.album_name}"
                
                # 第四行：歌词预览（完整显示多行）
                lyrics_preview = ""
                if config.pause_show_lyrics_preview and state.lyric_lines:
                    preview_lines = []
                    start_idx = max(0, current_lyric_line_index - 1) if current_lyric_line_index > 0 else 0
                    for i in range(start_idx, min(start_idx + 4, len(state.lyric_lines))):
                        line = state.lyric_lines[i]
                        if line.text:
                            preview_lines.append(line.text)
                    
                    if preview_lines:
                        lyrics_preview = "\n".join(preview_lines)
                
                # 组合消息
                lines = [pause_line1]
                if pause_line2:
                    lines.append(pause_line2)
                lines.append(pause_line3)
                if album_line:
                    lines.append(album_line)
                if lyrics_preview:
                    lines.append(lyrics_preview)
                
                new_message = "\n".join(lines)
            else:
                # 正常播放状态 - 完整歌词显示
                # 第一行：歌曲信息（截断过长歌手名）
                song_info_short = _truncate_song_info(display_song, display_artist, max_width=45)
                line1 = f"{icon_note} {song_info_short}"
                # 添加专辑名到第一行（如果启用）
                if config.show_album and state.song_info_data and state.song_info_data.album_name:
                    album_name = state.song_info_data.album_name
                    if len(album_name) > 15:
                        album_name = album_name[:15] + "..."
                    line1 += f" 💿{album_name}"
                
                # 第二行：进度条 + 时间 + 电量（播放时不显示音量，节省空间给歌词）
                line2 = ""
                if total_duration > 0:
                    bar = build_themed_bar(display_progress, total_duration, 8)
                    time_str = f"{format_time(display_progress)}/{format_time(total_duration)}"
                    line2 = f"{bar} {time_str}"
                    if hmd_battery_info:
                        line2 += f" | {hmd_battery_info}"
                elif hmd_battery_info:
                    line2 = f"🔋 {hmd_battery_info}"
                
                # 歌词显示（自适应行数 + 翻译支持）
                lyric_lines_display = []
                if state.lyric_lines and not state.lyrics_loading:
                    # 判断是否是开头介绍部分
                    is_intro = current_lyric_obj and current_lyric_obj.time < 5.0 and current_lyric_line_index < 8
                    
                    # 计算可用空间（总长度144，减去前两行）
                    header_len = len(line1) + len(line2) + 2  # +2 是换行符
                    available_chars = VRCHAT_CHATBOX_MAX_LENGTH - header_len - 10  # 预留空间
                    
                    if is_intro:
                        # 开头介绍部分：只显示当前行，快速切换
                        if current_lyric_obj and current_lyric_obj.text:
                            lyric_lines_display.append(f"▶ {current_lyric_obj.text}")
                    else:
                        # 正常歌词：显示当前行和预览行
                        max_lines = config.max_lyric_lines
                        
                        # 计算显示范围：当前行前后各取部分
                        start_idx = max(0, current_lyric_line_index - 1)
                        end_idx = min(len(state.lyric_lines), start_idx + max_lines)
                        
                        # 如果接近末尾，调整起始位置
                        if end_idx - start_idx < max_lines:
                            start_idx = max(0, end_idx - max_lines)
                        
                        current_chars = 0
                        for i in range(start_idx, end_idx):
                            line = state.lyric_lines[i]
                            if not line.text:
                                continue
                            
                            # 构建歌词行
                            if i == current_lyric_line_index:
                                lyric_text = f"▶ {line.text}"
                            else:
                                lyric_text = f"  {line.text}"
                            
                            # 检查是否超出空间
                            if current_chars + len(lyric_text) > available_chars:
                                break
                            
                            lyric_lines_display.append(lyric_text)
                            current_chars += len(lyric_text) + 1  # +1 换行符
                            
                            # 添加翻译（如果启用且有翻译数据）
                            if config.show_translation and state.song_info_data:
                                translation = state.song_info_data.translated_lines.get(i) if state.song_info_data.translated_lines else None
                                if translation:
                                    trans_text = f"   {translation}"
                                    if current_chars + len(trans_text) <= available_chars:
                                        lyric_lines_display.append(trans_text)
                                        current_chars += len(trans_text) + 1
                
                elif not state.lyrics_loading and song_name:
                    # 无歌词时显示有趣的提示（从配置文件读取，随机显示）
                    if config.no_lyric_messages:
                        lyric_lines_display = [random.choice(config.no_lyric_messages)]
                    else:
                        lyric_lines_display = ["🎵 没找到歌词"]
                
                # 组合消息
                if lyric_lines_display:
                    if line2:
                        new_message = f"{line1}\n{line2}\n" + "\n".join(lyric_lines_display)
                    else:
                        new_message = f"{line1}\n" + "\n".join(lyric_lines_display)
                elif line2:
                    new_message = f"{line1}\n{line2}"
                else:
                    new_message = line1
            
            # === 发送逻辑 ===
            # 检查VRChat连接状态
            if not osc.is_connected:
                if current_time - state.last_send_time >= 5.0:
                    if osc.check_connection():
                        print_status('success', "VRChat OSC 已重新连接")
                    else:
                        state.last_send_time = current_time
                        time.sleep(config.interval)
                        continue
            
            # 检测歌词行变化
            lyric_line_changed = current_lyric_line_index != state.last_lyric_line_index
            
            # 消息发送间隔控制
            min_interval = config.min_send_interval
            
            # 开头介绍部分（歌词时间戳 < 8秒且前10行）使用较短间隔
            is_intro_section = current_lyric_obj and current_lyric_obj.time < 8.0 and current_lyric_line_index < 10
            if is_intro_section:
                min_interval = 1.0  # 开头介绍使用较短间隔（VRChat最小约1.5秒）
            elif state.song_just_changed:
                min_interval = 0.5  # 歌曲切换快速发送
            elif lyric_line_changed:
                min_interval = config.min_send_interval  # 歌词变化
            elif state.is_paused:
                min_interval = config.resend_interval  # 暂停状态下定期刷新
            
            # 检查发送间隔
            time_since_last = current_time - state.last_send_time
            if time_since_last < min_interval:
                time.sleep(config.interval)
                continue
            
            # 发送消息
            success = osc.send_chat_message(new_message)
            
            if success:
                state.current_message = new_message
                state.last_send_time = current_time
                state.last_lyric_line_index = current_lyric_line_index
                if state.song_just_changed:
                    state.song_just_changed = False
            
            time.sleep(config.interval)
            
    except KeyboardInterrupt:
        pass
    finally:
        # 清理资源
        if osc_server:
            osc_server.stop()
        if hmd_monitor:
            hmd_monitor.shutdown()
        if gsmtc_client:
            gsmtc_client.stop()
        print_exit()


def run_once(config: Config):
    """只发送一次当前播放的歌曲信息"""
    print_banner()
    
    print(colorize('环境检测', Color.BRIGHT_WHITE))
    print(colorize('─' * 54, Color.DIM))
    vrc_connected = check_environment()
    print()
    
    if not vrc_connected:
        print_status('error', "VRChat OSC未连接")
        return
    
    print_status('info', "连接 MoeKoeMusic...")
    
    # 初始化 MoeKoeMusic 客户端
    moekoe_client = MoeKoeClient(config.moekoe_host, config.moekoe_port)
    
    if not moekoe_client.connect():
        print_status('error', "无法连接 MoeKoeMusic，请确保应用已启动并开启 API 模式")
        print_status('info', "在 MoeKoeMusic 设置中开启 'API模式'")
        return
    
    # 等待数据
    time.sleep(0.5)
    moekoe_data = moekoe_client.data
    
    if moekoe_data and moekoe_data.song.name:
        osc = VRChatOSC(config.osc_ip, config.osc_port, min_interval=config.min_send_interval)
        
        song_name = moekoe_data.song.name
        artist_name = moekoe_data.song.author
        message = build_display_message(song_name, artist_name, config=config)
        
        print_song_change(song_name, artist_name)
        
        if osc.send_chat_message(message):
            print_status('success', f"已发送到VRChat: {message}")
        else:
            print_status('error', "发送失败")
    else:
        print_status('warning', "MoeKoeMusic 当前没有播放音乐")
    
    moekoe_client.disconnect()


def run_status():
    """显示当前状态（诊断模式）"""
    print_banner()
    
    print(colorize('状态诊断', Color.BRIGHT_WHITE))
    print(colorize('─' * 54, Color.DIM))
    
    osc = VRChatOSC()
    vrc_status = osc.get_status()
    
    if vrc_status.is_connected:
        print_status('success', f"VRChat OSC: {vrc_status.ip}:{vrc_status.port}")
    else:
        print_status('error', f"VRChat OSC: {vrc_status.message}")
    
    # 检查 MoeKoeMusic 连接
    config = load_config("config.json")
    moekoe_client = MoeKoeClient(config.moekoe_host, config.moekoe_port)
    
    if moekoe_client.connect():
        print_status('success', f"MoeKoeMusic: 已连接 (ws://{config.moekoe_host}:{config.moekoe_port})")
        time.sleep(0.5)
        moekoe_data = moekoe_client.data
        
        if moekoe_data and moekoe_data.song.name:
            print_status('music', f"当前播放: {moekoe_data.song.name} - {moekoe_data.song.author}")
        else:
            print_status('warning', "当前没有播放音乐")
        
        moekoe_client.disconnect()
    else:
        print_status('error', "MoeKoeMusic: 未连接")
        print_status('info', "请确保 MoeKoeMusic 已启动并开启 API 模式")


def main():
    # === 单例检测：防止多开 ===
    import ctypes
    from ctypes import wintypes
    
    SINGLE_INSTANCE_MUTEX_NAME = "Global\\MoeKoeMusic_VRChat_Display_SingleInstance"
    kernel32 = ctypes.windll.kernel32
    
    # 创建互斥体
    mutex = kernel32.CreateMutexW(None, False, SINGLE_INSTANCE_MUTEX_NAME)
    last_error = kernel32.GetLastError()
    
    # ERROR_ALREADY_EXISTS = 183，表示互斥体已存在
    if last_error == 183:
        print_banner()
        print()
        print_status('error', "程序已在运行中！")
        print_status('info', "请勿重复启动，以免造成 OSC 通信冲突")
        print()
        return
    
    # 确保互斥体在程序退出时释放
    import atexit
    atexit.register(lambda: kernel32.ReleaseMutex(mutex) if mutex else None)
    
    parser = argparse.ArgumentParser(
        description='MoeKoeMusic → VRChat 聊天框显示程序',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
示例:
  python main.py                # 持续监控模式
  python main.py -i 1           # 1秒刷新间隔
  python main.py --once         # 只发送一次
  python main.py --status       # 查看状态诊断

配置文件:
  程序会自动读取同目录下的 config.json
  
MoeKoeMusic 设置:
  请在 MoeKoeMusic 设置中开启 "API模式"
  默认 WebSocket 端口: 6520
'''
    )
    
    parser.add_argument('-i', '--interval', type=float, help='刷新间隔（秒），默认2秒')
    parser.add_argument('--once', action='store_true', help='只发送一次当前歌曲信息')
    parser.add_argument('--status', action='store_true', help='显示状态诊断信息')
    parser.add_argument('-c', '--config', type=str, default='config.json', help='配置文件路径')
    
    args = parser.parse_args()
    
    config = load_config(args.config)
    
    if args.interval:
        config.interval = args.interval
    
    if args.status:
        run_status()
    elif args.once:
        run_once(config)
    else:
        run_monitor(config)


if __name__ == '__main__':
    main()
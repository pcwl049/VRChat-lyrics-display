"""
Windows Global System Media Transport Controls (GSMTC) 客户端

通过 Windows 系统 API 获取媒体播放状态，支持任何实现了 GSMTC 的播放器。
比 WebSocket 方式更精确、更轻量。

支持的应用：
- MoeKoeMusic (完整支持：歌曲名、歌手、进度、状态)
- 网易云音乐 (完整支持)
- Spotify (完整支持)
- 酷狗官方客户端 (部分支持：歌曲名、歌手、状态，但无进度)
"""

import asyncio
import logging
import threading
import time
from dataclasses import dataclass, field
from typing import Callable, Optional
from enum import Enum

logger = logging.getLogger(__name__)

# 检查 winrt 是否可用
try:
    from winrt.windows.media.control import (
        GlobalSystemMediaTransportControlsSessionManager as MediaManager,
        GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus
    )
    GSMTCAVAILABLE = True
except ImportError:
    GSMTCAVAILABLE = False
    logger.warning("winrt 未安装，GSMTC 客户端不可用。运行: pip install winrt-Windows.Media.Control")


class PlaybackState(Enum):
    """播放状态枚举"""
    UNKNOWN = 0
    CLOSED = 1
    OPENED = 2
    CHANGING = 3
    STOPPED = 4
    PLAYING = 5
    PAUSED = 6


@dataclass
class MediaInfo:
    """媒体信息"""
    app_id: str = ""                    # 应用ID (如 cn.MoeKoe.Music, kugou)
    title: str = ""                     # 歌曲名
    artist: str = ""                    # 歌手
    album: str = ""                     # 专辑
    current_time: float = 0.0           # 当前播放时间（秒）
    total_time: float = 0.0             # 总时长（秒）
    state: PlaybackState = PlaybackState.UNKNOWN  # 播放状态
    connected: bool = False             # 是否连接
    
    @property
    def is_playing(self) -> bool:
        return self.state == PlaybackState.PLAYING
    
    @property
    def is_paused(self) -> bool:
        return self.state == PlaybackState.PAUSED
    
    @property
    def progress_percent(self) -> float:
        if self.total_time > 0:
            return min(1.0, self.current_time / self.total_time)
        return 0.0


@dataclass
class GSMTCConfig:
    """GSMTC 配置"""
    target_app_id: str = ""             # 目标应用ID，空则使用当前焦点应用
    poll_interval: float = 0.3          # 轮询间隔（秒）
    auto_reconnect: bool = True         # 自动重连


class GSMTCCient:
    """
    GSMTC 客户端
    
    使用 Windows 系统 API 获取媒体播放状态。
    比 WebSocket 方式更精确、更轻量。
    """
    
    def __init__(self, config: GSMTCConfig = None):
        """
        初始化客户端
        
        Args:
            config: 配置对象
        """
        self.config = config or GSMTCConfig()
        self._running = False
        self._thread: Optional[threading.Thread] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        
        # 媒体信息
        self.info = MediaInfo()
        
        # 上一次的歌曲信息（用于检测歌曲切换）
        self._last_title: str = ""
        self._last_artist: str = ""
        
        # 回调函数
        self._on_song_change: Optional[Callable[[MediaInfo], None]] = None
        self._on_state_change: Optional[Callable[[MediaInfo], None]] = None
        self._on_progress: Optional[Callable[[MediaInfo], None]] = None
    
    def is_available(self) -> bool:
        """检查 GSMTC 是否可用"""
        return GSMTCAVAILABLE
    
    def on_song_change(self, callback: Callable[[MediaInfo], None]):
        """注册歌曲切换回调"""
        self._on_song_change = callback
    
    def on_state_change(self, callback: Callable[[MediaInfo], None]):
        """注册播放状态变化回调"""
        self._on_state_change = callback
    
    def on_progress(self, callback: Callable[[MediaInfo], None]):
        """注册进度更新回调"""
        self._on_progress = callback
    
    def start(self) -> bool:
        """
        启动监控
        
        Returns:
            是否成功启动
        """
        if not GSMTCAVAILABLE:
            logger.error("GSMTC 不可用")
            return False
        
        if self._running:
            return True
        
        self._running = True
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()
        
        # 等待连接建立
        for _ in range(20):
            if self.info.connected:
                logger.info("GSMTC 客户端已启动")
                return True
            time.sleep(0.1)
        
        logger.warning("GSMTC 客户端启动超时")
        return True  # 即使超时也返回 True，后台会继续尝试
    
    def stop(self):
        """停止监控"""
        self._running = False
        # 不强制停止事件循环，让 _monitor_loop 自然退出
        # 设置超时等待线程结束
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        self.info.connected = False
        logger.info("GSMTC 客户端已停止")
    
    def _run_loop(self):
        """运行事件循环"""
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        
        try:
            self._loop.run_until_complete(self._monitor_loop())
        except Exception as e:
            logger.error(f"GSMTC 监控错误: {e}")
        finally:
            self._loop.close()
    
    async def _monitor_loop(self):
        """监控循环"""
        session_manager = None
        
        while self._running:
            try:
                # 获取会话管理器
                if session_manager is None:
                    session_manager = await MediaManager.request_async()
                
                # 获取当前会话
                session = session_manager.get_current_session()
                
                if session:
                    # 检查是否是目标应用
                    app_id = session.source_app_user_model_id
                    if self.config.target_app_id and app_id != self.config.target_app_id:
                        await asyncio.sleep(self.config.poll_interval)
                        continue
                    
                    # 获取播放状态
                    playback_info = session.get_playback_info()
                    state = PlaybackState(playback_info.playback_status + 1)  # +1 因为枚举从 1 开始
                    
                    # 获取播放进度
                    timeline = session.get_timeline_properties()
                    current_time = timeline.position.total_seconds()
                    total_time = timeline.end_time.total_seconds()
                    
                    # 获取媒体属性
                    media_props = await session.try_get_media_properties_async()
                    title = media_props.title or ""
                    artist = media_props.artist or ""
                    album = media_props.album_title or ""
                    
                    # 更新信息
                    old_title = self.info.title
                    old_artist = self.info.artist
                    old_state = self.info.state
                    
                    self.info.app_id = app_id
                    self.info.title = title
                    self.info.artist = artist
                    self.info.album = album
                    self.info.current_time = current_time
                    self.info.total_time = total_time
                    self.info.state = state
                    self.info.connected = True
                    
                    # 检测歌曲切换
                    if title != old_title or artist != old_artist:
                        if title and old_title:  # 不是初始化
                            logger.info(f"歌曲切换: {title} - {artist}")
                            if self._on_song_change:
                                self._on_song_change(self.info)
                    
                    # 检测状态变化
                    if state != old_state and old_state != PlaybackState.UNKNOWN:
                        logger.debug(f"状态变化: {old_state.name} -> {state.name}")
                        if self._on_state_change:
                            self._on_state_change(self.info)
                    
                    # 进度回调
                    if self._on_progress:
                        self._on_progress(self.info)
                    
                else:
                    self.info.connected = False
                    self.info.state = PlaybackState.STOPPED
                
            except Exception as e:
                logger.debug(f"GSMTC 轮询错误: {e}")
                self.info.connected = False
            
            await asyncio.sleep(self.config.poll_interval)
    
    def get_current_info(self) -> MediaInfo:
        """获取当前媒体信息"""
        return self.info


# 便捷函数
def create_gsmtc_client(target_app: str = "", poll_interval: float = 0.3) -> GSMTCCient:
    """
    创建 GSMTC 客户端
    
    Args:
        target_app: 目标应用ID，空则使用当前焦点应用
        poll_interval: 轮询间隔（秒）
    
    Returns:
        GSMTCClient 实例
    """
    config = GSMTCConfig(
        target_app_id=target_app,
        poll_interval=poll_interval
    )
    return GSMTCCient(config)


# 常见应用ID
APP_IDS = {
    'moekoe': 'cn.MoeKoe.Music',
    'kugou': 'kugou',
    'netease': 'cloudmusic',
    'spotify': 'Spotify.exe',
    'qqmusic': 'QQMusic',
}

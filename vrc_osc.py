"""
VRChat OSC 通信模块
通过OSC协议向VRChat发送聊天消息
支持连接检测和状态诊断
支持OSC接收，监听VRChat快捷键命令
优化：消息队列、智能速率限制、连接状态检测、发送统计
"""
import socket
import time
import threading
import logging
import os
from datetime import datetime
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Optional
from collections import deque
from pythonosc import udp_client
from pythonosc.osc_message_builder import OscMessageBuilder
from pythonosc.dispatcher import Dispatcher
from pythonosc.osc_server import ThreadingOSCUDPServer

# === OSC 消息日志配置 ===
OSC_LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "osc_logs")
OSC_LOG_FILE = os.path.join(OSC_LOG_DIR, "osc_messages.log")

def _ensure_log_dir():
    """确保日志目录存在"""
    if not os.path.exists(OSC_LOG_DIR):
        os.makedirs(OSC_LOG_DIR)

def _log_osc_message(message: str, raw_bytes: bytes = None):
    """
    记录 OSC 消息到日志文件
    
    Args:
        message: 发送的消息文本
        raw_bytes: 原始字节（可选，用于调试编码问题）
    """
    _ensure_log_dir()
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    
    log_line = f"[{timestamp}] {message}"
    if raw_bytes:
        log_line += f" | bytes: {raw_bytes.hex()}"
    log_line += "\n"
    
    try:
        with open(OSC_LOG_FILE, "a", encoding="utf-8") as f:
            f.write(log_line)
    except Exception as e:
        logging.warning(f"无法写入 OSC 日志: {e}")


# VRChat chatbox 消息最大长度
VRCHAT_CHATBOX_MAX_LENGTH = 144

# VRChat OSC 速率限制（官方限制约每1.8秒1条，建议2秒以上）
VRCHAT_RATE_LIMIT_SECONDS = 1.8
# 歌词更新最小间隔（比普通消息更快）
LYRIC_UPDATE_INTERVAL = 2.0


class VRCConnectionStatus(Enum):
    """VRChat连接状态枚举"""
    CONNECTED = "connected"
    DISCONNECTED = "disconnected"
    UNKNOWN = "unknown"


class MessagePriority(Enum):
    """消息优先级枚举"""
    HIGH = 0      # 高优先级：歌曲切换、分享命令（立即发送）
    NORMAL = 1    # 普通优先级：歌词更新（较快发送）
    LOW = 2       # 低优先级：进度更新（正常间隔）


@dataclass
class QueuedMessage:
    """队列中的消息"""
    content: str
    priority: MessagePriority
    timestamp: float
    retry_count: int = 0


@dataclass
class VRCStatus:
    """VRChat状态信息"""
    is_connected: bool
    status: VRCConnectionStatus
    ip: str
    port: int
    message: str


@dataclass
class SendStats:
    """发送统计"""
    total_sent: int = 0
    total_failed: int = 0
    total_dropped: int = 0
    last_send_time: float = 0
    last_success_time: float = 0
    consecutive_failures: int = 0
    rate_limit_hits: int = 0


class VRChatOSC:
    """VRChat OSC客户端 - 优化版"""
    
    # VRChat默认OSC监听地址和端口
    DEFAULT_IP = "127.0.0.1"
    DEFAULT_PORT = 9000
    
    # OSC地址
    CHATBOX_ADDRESS = "/chatbox/input"
    TYPING_ADDRESS = "/chatbox/typing"
    AVATAR_CHANGE_ADDRESS = "/avatar/change"
    
    # 配置
    MAX_QUEUE_SIZE = 10           # 消息队列最大长度
    MAX_RETRY_COUNT = 3           # 最大重试次数
    CONNECTION_CHECK_INTERVAL = 5.0  # 连接检测间隔（秒）
    
    def __init__(self, ip: str = DEFAULT_IP, port: int = DEFAULT_PORT,
                 min_interval: float = 4.0):
        """
        初始化OSC客户端
        
        Args:
            ip: VRChat监听的IP地址，默认为本地
            port: VRChat监听的端口，默认为9000
            min_interval: 最小发送间隔（秒），用于智能速率限制
        """
        self.ip = ip
        self.port = port
        self.min_interval = min_interval
        
        self._client = None
        self._socket = None
        self._last_send_time = 0
        self._last_content = ""  # 上次发送的内容（用于去重）
        
        # 消息队列（按优先级排序）
        self._message_queue: deque[QueuedMessage] = deque(maxlen=self.MAX_QUEUE_SIZE)
        self._queue_lock = threading.Lock()
        
        # 连接状态
        self._is_connected = False
        self._last_connection_check = 0
        self._connection_lock = threading.Lock()
        
        # 发送统计
        self._stats = SendStats()
        self._stats_lock = threading.Lock()
        
    @property
    def client(self):
        """延迟初始化OSC客户端"""
        if self._client is None:
            self._client = udp_client.SimpleUDPClient(self.ip, self.port)
        return self._client
    
    @property
    def socket(self):
        """延迟初始化原始socket"""
        if self._socket is None:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return self._socket
    
    def check_connection(self, force: bool = False) -> bool:
        """
        检测VRChat连接状态（带缓存）
        
        Args:
            force: 是否强制检测（忽略缓存）
        
        Returns:
            bool: 是否已连接
        """
        current_time = time.time()
        
        # 使用缓存，避免频繁检测
        if not force and (current_time - self._last_connection_check) < self.CONNECTION_CHECK_INTERVAL:
            return self._is_connected
        
        try:
            # 创建临时socket检测
            test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            test_socket.settimeout(0.5)
            test_socket.connect((self.ip, self.port))
            test_socket.close()
            
            with self._connection_lock:
                self._is_connected = True
                self._last_connection_check = current_time
            return True
            
        except Exception:
            with self._connection_lock:
                self._is_connected = False
                self._last_connection_check = current_time
            return False
    
    def queue_message(self, message: str, priority: MessagePriority = MessagePriority.NORMAL) -> bool:
        """
        将消息加入队列（非阻塞）
        
        Args:
            message: 消息内容
            priority: 消息优先级
        
        Returns:
            bool: 是否成功加入队列
        """
        if not message or not message.strip():
            return True
        
        # 去重检查：相同内容不重复加入
        if message == self._last_content:
            return True
        
        with self._queue_lock:
            # 检查队列中是否已有相同内容
            for msg in self._message_queue:
                if msg.content == message:
                    return True  # 已存在，不重复添加
            
            # 高优先级消息插入队列开头
            queued_msg = QueuedMessage(
                content=message,
                priority=priority,
                timestamp=time.time()
            )
            
            if priority == MessagePriority.HIGH:
                # 插入到第一个非高优先级消息之前
                insert_idx = 0
                for i, msg in enumerate(self._message_queue):
                    if msg.priority != MessagePriority.HIGH:
                        insert_idx = i
                        break
                    insert_idx = i + 1
                
                # 由于deque不支持中间插入，高优先级直接appendleft
                self._message_queue.appendleft(queued_msg)
            else:
                self._message_queue.append(queued_msg)
            
            return True
    
    def send_queued_message(self) -> tuple[bool, str]:
        """
        发送队列中的下一条消息（遵守速率限制）
        
        Returns:
            tuple: (是否发送成功, 发送的消息内容)
        """
        current_time = time.time()
        
        # 速率限制检查
        time_since_last = current_time - self._last_send_time
        if time_since_last < max(self.min_interval, VRCHAT_RATE_LIMIT_SECONDS):
            # 还在冷却中
            with self._stats_lock:
                self._stats.rate_limit_hits += 1
            return False, ""
        
        with self._queue_lock:
            if not self._message_queue:
                return False, ""
            
            # 取出优先级最高的消息
            queued_msg = self._message_queue.popleft()
        
        # 发送消息
        success = self._send_raw(queued_msg.content)
        
        if success:
            self._last_content = queued_msg.content
            self._last_send_time = current_time
            with self._stats_lock:
                self._stats.total_sent += 1
                self._stats.last_send_time = current_time
                self._stats.last_success_time = current_time
                self._stats.consecutive_failures = 0
            return True, queued_msg.content
        else:
            # 发送失败，考虑重新入队或丢弃
            queued_msg.retry_count += 1
            with self._stats_lock:
                self._stats.total_failed += 1
                self._stats.consecutive_failures += 1
            
            if queued_msg.retry_count < self.MAX_RETRY_COUNT:
                # 重新入队
                with self._queue_lock:
                    if len(self._message_queue) < self.MAX_QUEUE_SIZE - 1:
                        self._message_queue.appendleft(queued_msg)
            else:
                # 超过重试次数，丢弃
                with self._stats_lock:
                    self._stats.total_dropped += 1
            
            return False, ""
    
    def _send_raw(self, message: str) -> bool:
        """
        底层发送方法（不检查速率限制）
        
        Args:
            message: 消息内容
        
        Returns:
            bool: 发送是否成功
        """
        try:
            # VRChat chatbox消息长度限制
            if len(message) > VRCHAT_CHATBOX_MAX_LENGTH:
                message = message[:VRCHAT_CHATBOX_MAX_LENGTH - 3] + "..."
            
            # 构建OSC消息
            builder = OscMessageBuilder(address=self.CHATBOX_ADDRESS)
            builder.add_arg(message)
            builder.add_arg(True)  # True = 立即发送
            msg = builder.build()
            
            # 记录发送的消息（包含原始字节用于调试编码问题）
            _log_osc_message(message, msg.dgram)
            
            # 发送
            self.socket.sendto(msg.dgram, (self.ip, self.port))
            return True
            
        except Exception as e:
            print(f"OSC发送错误: {e}")
            return False
    
    def send_chat_message(self, message: str, send_notification: bool = False,
                          priority: MessagePriority = MessagePriority.NORMAL,
                          immediate: bool = False) -> bool:
        """
        发送消息到VRChat聊天框
        
        根据优先级使用不同的发送间隔：
        - HIGH: 遵守VRChat硬限制 (1.8秒)
        - NORMAL: 歌词更新间隔 (2.0秒)
        - LOW: 用户配置的min_interval
        
        Args:
            message: 要发送的消息内容（最多144字符）
            send_notification: 未使用（保持接口兼容）
            priority: 消息优先级
            immediate: 是否立即发送（跳过队列）
        
        Returns:
            bool: 发送是否成功
        """
        # 空消息不发送
        if not message or not message.strip():
            return True
        
        # 去重：相同内容直接返回成功
        if message == self._last_content:
            return True
        
        # 根据优先级确定最小间隔
        if priority == MessagePriority.HIGH:
            min_interval = VRCHAT_RATE_LIMIT_SECONDS  # 1.8秒
        elif priority == MessagePriority.NORMAL:
            min_interval = LYRIC_UPDATE_INTERVAL  # 2.0秒
        else:
            min_interval = self.min_interval  # 用户配置
        
        current_time = time.time()
        time_since_last = current_time - self._last_send_time
        
        # 检查是否满足最小间隔
        if time_since_last < min_interval:
            # 如果是立即模式，等待剩余时间
            if immediate:
                wait_time = min_interval - time_since_last
                if wait_time > 0:
                    time.sleep(wait_time)
            else:
                # 非立即模式，加入队列或直接返回
                self.queue_message(message, priority)
                return False
        
        # 直接发送
        success = self._send_raw(message)
        if success:
            self._last_content = message
            self._last_send_time = time.time()
            with self._stats_lock:
                self._stats.total_sent += 1
                self._stats.last_send_time = current_time
                self._stats.last_success_time = current_time
                self._stats.consecutive_failures = 0
        else:
            with self._stats_lock:
                self._stats.total_failed += 1
                self._stats.consecutive_failures += 1
        
        return success
    
    def send_immediate(self, message: str) -> bool:
        """
        立即发送消息（用于紧急消息，如分享命令）
        
        Args:
            message: 消息内容
        
        Returns:
            bool: 发送是否成功
        """
        return self.send_chat_message(message, immediate=True, priority=MessagePriority.HIGH)
    
    def clear_chatbox(self) -> bool:
        """清除 chatbox 显示"""
        try:
            builder = OscMessageBuilder(address=self.CHATBOX_ADDRESS)
            builder.add_arg(" ")
            builder.add_arg(True)
            msg = builder.build()
            self.socket.sendto(msg.dgram, (self.ip, self.port))
            self._last_content = " "
            return True
        except Exception:
            return False
    
    def send_chat_typing(self, is_typing: bool = True) -> bool:
        """
        发送正在输入状态
        
        Args:
            is_typing: 是否正在输入
        
        Returns:
            bool: 发送是否成功
        """
        try:
            self.client.send_message(self.TYPING_ADDRESS, is_typing)
            return True
        except Exception:
            return False
    
    def check_port_open(self) -> bool:
        """
        检测VRChat OSC端口是否开放
        
        Returns:
            bool: 端口是否开放
        """
        return self.check_connection(force=True)
    
    def test_connection(self) -> bool:
        """
        测试OSC连接是否正常
        
        Returns:
            bool: 连接是否正常
        """
        return self.check_connection(force=True)
    
    def get_status(self) -> VRCStatus:
        """
        获取VRChat OSC状态
        
        Returns:
            VRCStatus: 包含连接状态信息的对象
        """
        is_connected = self.check_connection()
        
        if is_connected:
            return VRCStatus(
                is_connected=True,
                status=VRCConnectionStatus.CONNECTED,
                ip=self.ip,
                port=self.port,
                message="VRChat OSC端口可访问"
            )
        else:
            return VRCStatus(
                is_connected=False,
                status=VRCConnectionStatus.DISCONNECTED,
                ip=self.ip,
                port=self.port,
                message="VRChat OSC端口不可访问，请确保VRChat已开启OSC"
            )
    
    @property
    def stats(self) -> dict:
        """获取发送统计信息"""
        with self._stats_lock:
            return {
                'total_sent': self._stats.total_sent,
                'total_failed': self._stats.total_failed,
                'total_dropped': self._stats.total_dropped,
                'last_send_time': self._stats.last_send_time,
                'last_success_time': self._stats.last_success_time,
                'consecutive_failures': self._stats.consecutive_failures,
                'rate_limit_hits': self._stats.rate_limit_hits,
                'queue_size': len(self._message_queue),
                'is_connected': self._is_connected
            }
    
    @property
    def is_connected(self) -> bool:
        """当前连接状态"""
        return self._is_connected
    
    @property
    def queue_size(self) -> int:
        """当前队列大小"""
        return len(self._message_queue)


class OSCCommandServer:
    """
    OSC命令服务端
    监听VRChat发送的OSC命令（通过快捷键触发）
    """
    
    DEFAULT_PORT = 9001  # VRChat发送命令的默认端口
    
    # 预定义的命令地址
    CMD_MUSIC_TOGGLE = "/music/toggle"      # 开关音乐
    CMD_MUSIC_MUTE = "/music/mute"          # 静音音乐
    CMD_MUSIC_UNMUTE = "/music/unmute"      # 取消静音
    CMD_SHARE_SONG = "/music/share"         # 分享歌曲信息
    
    def __init__(self, port: int = None):
        """
        初始化OSC命令服务端
        
        Args:
            port: 监听端口，默认从VRChat接收命令的端口
        """
        self.port = port or self.DEFAULT_PORT
        self._server: Optional[ThreadingOSCUDPServer] = None
        self._thread: Optional[threading.Thread] = None
        self._running = False
        
        # 命令回调函数
        self._callbacks: dict[str, Callable] = {}
        
    def register_callback(self, address: str, callback: Callable):
        """
        注册命令回调函数
        
        Args:
            address: OSC地址
            callback: 回调函数，接收参数 (address, *args)
        """
        self._callbacks[address] = callback
    
    def _handle_message(self, address: str, *args):
        """处理接收到的OSC消息"""
        if address in self._callbacks:
            try:
                self._callbacks[address](address, *args)
            except Exception as e:
                print(f"OSC命令处理错误 [{address}]: {e}")
    
    def start(self) -> bool:
        """
        启动OSC服务端
        
        Returns:
            bool: 是否启动成功
        """
        if self._running:
            return True
        
        try:
            dispatcher = Dispatcher()
            
            # 注册所有回调
            for address in self._callbacks:
                dispatcher.map(address, self._handle_message)
            
            # 同时监听通配符地址
            dispatcher.set_default_handler(self._handle_message)
            
            self._server = ThreadingOSCUDPServer(
                ("0.0.0.0", self.port), 
                dispatcher
            )
            
            self._running = True
            self._thread = threading.Thread(
                target=self._server.serve_forever,
                daemon=True
            )
            self._thread.start()
            
            return True
            
        except OSError as e:
            if "10048" in str(e) or "Address already in use" in str(e):
                print(f"端口 {self.port} 已被占用")
            else:
                print(f"OSC服务端启动失败: {e}")
            return False
        except Exception as e:
            print(f"OSC服务端启动失败: {e}")
            return False
    
    def stop(self):
        """停止OSC服务端"""
        if self._server and self._running:
            self._running = False
            self._server.shutdown()
            if self._thread:
                self._thread.join(timeout=2)
    
    @property
    def is_running(self) -> bool:
        """服务端是否正在运行"""
        return self._running


def print_osc_help():
    """打印OSC设置帮助信息"""
    help_text = """
VRChat OSC 设置指南:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
1. 在VRChat中打开快捷菜单 (按ESC或菜单键)
2. 点击 Settings (设置)
3. 找到并点击 OSC 选项
4. 确保 Enable OSC 已开启 (显示绿色)
5. 点击 "Reset Config" 可重置OSC配置

如果仍然无法连接，尝试:
- 确保VRChat正在运行
- 检查防火墙是否阻止了端口9000
- 重启VRChat
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"""
    print(help_text)


if __name__ == '__main__':
    from ui import print_status, print_banner, colorize, Color
    
    print_banner()
    
    osc = VRChatOSC()
    print(f"正在检测VRChat OSC连接...")
    print(f"目标地址: {osc.ip}:{osc.port}")
    print()
    
    status = osc.get_status()
    
    if status.is_connected:
        print_status('success', status.message)
    else:
        print_status('error', status.message)
        print_osc_help()

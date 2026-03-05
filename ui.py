"""
命令行美化模块
提供彩色输出、图标和动画效果
使用Windows兼容的刷新方式
"""
import os
import sys
from datetime import datetime
from enum import Enum


class Color(Enum):
    """ANSI颜色代码"""
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    
    BLACK = "\033[30m"
    RED = "\033[31m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    CYAN = "\033[36m"
    WHITE = "\033[37m"
    
    BRIGHT_RED = "\033[91m"
    BRIGHT_GREEN = "\033[92m"
    BRIGHT_YELLOW = "\033[93m"
    BRIGHT_BLUE = "\033[94m"
    BRIGHT_MAGENTA = "\033[95m"
    BRIGHT_CYAN = "\033[96m"
    BRIGHT_WHITE = "\033[97m"


class Icon:
    """Unicode图标"""
    MUSIC = "🎵"
    PLAYING = "▶"
    PAUSED = "⏸"
    STOPPED = "⏹"
    SEND = "📤"
    SUCCESS = "✅"
    ERROR = "❌"
    WARNING = "⚠️"
    INFO = "ℹ️"
    HEART = "❤️"
    NOTE = "🎶"
    MICROPHONE = "🎤"
    HEADPHONES = "🎧"
    CHECK = "✓"
    CROSS = "✗"
    ARROW = "→"
    DOT = "●"
    CIRCLE = "○"


def enable_ansi():
    """启用Windows ANSI转义序列支持"""
    if sys.platform == 'win32':
        # 启用虚拟终端处理
        kernel32 = None
        try:
            kernel32 = ctypes.windll.kernel32
            ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
            STD_OUTPUT_HANDLE = -11
            
            handle = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
            mode = ctypes.c_ulong()
            kernel32.GetConsoleMode(handle, ctypes.byref(mode))
            mode.value |= ENABLE_VIRTUAL_TERMINAL_PROCESSING
            kernel32.SetConsoleMode(handle, mode)
        except:
            pass


def colorize(text: str, color: Color) -> str:
    """为文本添加颜色"""
    return f"{color.value}{text}{Color.RESET.value}"


def get_timestamp() -> str:
    """获取当前时间戳"""
    return datetime.now().strftime("%H:%M:%S")


def clear_screen():
    """清屏（Windows兼容）"""
    os.system('cls' if os.name == 'nt' else 'clear')


def format_time(seconds: float) -> str:
    """格式化时间为 mm:ss"""
    minutes = int(seconds // 60)
    secs = int(seconds % 60)
    return f"{minutes:02d}:{secs:02d}"


# 显示宽度缓存（性能优化）
_display_width_cache: dict[str, int] = {}
_CACHE_MAX_SIZE = 200


def get_display_width(text: str) -> int:
    """
    计算字符串的显示宽度（带缓存）
    中文字符等全角字符宽度为2，英文字符等半角字符宽度为1
    
    性能优化：
    - 使用缓存避免重复计算
    - 使用字符码点范围判断，比条件判断更高效
    """
    global _display_width_cache
    
    if not text:
        return 0
    
    # 检查缓存
    if text in _display_width_cache:
        return _display_width_cache[text]
    
    width = 0
    for char in text:
        code = ord(char)
        # 使用码点范围判断（更高效）
        # CJK统一汉字: U+4E00..U+9FFF
        # CJK扩展A: U+3400..U+4DBF
        # CJK标点: U+3000..U+303F
        # 全角字符: U+FF00..U+FFEF
        # 中文标点符号
        if (0x4E00 <= code <= 0x9FFF or  # CJK统一汉字
            0x3400 <= code <= 0x4DBF or  # CJK扩展A
            0x3000 <= code <= 0x303F or  # CJK标点
            0xFF00 <= code <= 0xFFEF or  # 全角字符
            code in (0x300C, 0x300D, 0x300E, 0x300F,  # 「」『』
                     0x3010, 0x3011, 0x300A, 0x300B,  # 【】《》
                     0x3008, 0x3009, 0xFF08, 0xFF09)):  # 〈〉（）
            width += 2
        else:
            width += 1
    
    # 缓存结果（限制缓存大小）
    if len(_display_width_cache) < _CACHE_MAX_SIZE:
        _display_width_cache[text] = width
    
    return width


def clear_width_cache():
    """清空显示宽度缓存"""
    global _display_width_cache
    _display_width_cache.clear()


def truncate_by_display_width(text: str, max_width: int) -> str:
    """
    按显示宽度截断字符串
    确保截断后的字符串显示宽度不超过 max_width
    """
    if not text:
        return ""
    
    result = ""
    current_width = 0
    
    for char in text:
        # 计算当前字符的显示宽度
        if '\u4e00' <= char <= '\u9fff':  # 中文字符
            char_width = 2
        elif '\u3000' <= char <= '\u303f':  # CJK标点符号
            char_width = 2
        elif '\uff00' <= char <= '\uffef':  # 全角字符
            char_width = 2
        elif char in '「」『』【】〈〉《》（）':  # 中文标点
            char_width = 2
        else:
            char_width = 1
        
        # 检查添加当前字符是否会超出宽度限制
        if current_width + char_width > max_width:
            break
        
        result += char
        current_width += char_width
    
    return result


def create_progress_bar(current: float, total: float, width: int = 20) -> str:
    """创建进度条"""
    if total <= 0:
        return "░" * width
    
    progress = min(current / total, 1.0)
    filled = int(progress * width)
    empty = width - filled
    
    bar = "█" * filled + "░" * empty
    
    current_time = format_time(current)
    total_time = format_time(total)
    
    return f"[{bar}] {current_time} / {total_time}"


# 导入ctypes用于Windows API
import ctypes


def move_cursor_up(lines: int):
    """移动光标向上"""
    sys.stdout.write(f"\033[{lines}A")


def move_cursor_to_start():
    """移动光标到行首"""
    sys.stdout.write("\r")


def clear_line():
    """清除当前行"""
    sys.stdout.write("\033[2K")


def clear_from_cursor_to_end():
    """清除从光标到屏幕末尾"""
    sys.stdout.write("\033[J")


def print_banner():
    """打印程序启动横幅"""
    banner = f"""
{colorize('╔══════════════════════════════════════════════════════╗', Color.BRIGHT_CYAN)}
{colorize('║', Color.BRIGHT_CYAN)}                                                      {colorize('║', Color.BRIGHT_CYAN)}
{colorize('║', Color.BRIGHT_CYAN)}   {Icon.HEADPHONES} {colorize('MoeKoeMusic → VRChat 聊天框显示程序', Color.BRIGHT_WHITE)}        {colorize('║', Color.BRIGHT_CYAN)}
{colorize('║', Color.BRIGHT_CYAN)}                                                      {colorize('║', Color.BRIGHT_CYAN)}
{colorize('╚══════════════════════════════════════════════════════╝', Color.BRIGHT_CYAN)}
"""
    print(banner)


def print_status(status_type: str, message: str, timestamp: bool = True):
    """打印带状态图标的消息"""
    ts = f"{colorize(f'[{get_timestamp()}]', Color.DIM)} " if timestamp else ""
    
    status_config = {
        'success': (Icon.SUCCESS, Color.BRIGHT_GREEN),
        'error': (Icon.ERROR, Color.BRIGHT_RED),
        'warning': (Icon.WARNING, Color.BRIGHT_YELLOW),
        'info': (Icon.INFO, Color.BRIGHT_BLUE),
        'send': (Icon.SEND, Color.BRIGHT_MAGENTA),
        'music': (Icon.MUSIC, Color.BRIGHT_CYAN),
        'playing': (Icon.PLAYING, Color.BRIGHT_GREEN),
        'detect': (Icon.DOT, Color.BRIGHT_YELLOW),
    }
    
    icon, color = status_config.get(status_type, (Icon.INFO, Color.WHITE))
    print(f"{ts}{colorize(icon, color)} {colorize(message, Color.WHITE)}")


def print_song_change(song: str, artist: str):
    """打印歌曲变化信息"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    icon = colorize(Icon.PLAYING, Color.BRIGHT_GREEN)
    song_text = colorize(f'{song}', Color.BRIGHT_WHITE)
    artist_text = colorize(f'{artist}', Color.CYAN)
    
    print(f"\n{ts} {icon} 正在播放: {song_text} - {artist_text}")


def print_send_success(message: str):
    """打印发送成功信息"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    icon = colorize(Icon.SEND, Color.BRIGHT_MAGENTA)
    text = colorize(message, Color.BRIGHT_WHITE)
    
    print(f"{ts} {icon} {text}")


def print_monitoring_status(song: str | None, is_same: bool = False):
    """打印监控状态"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    
    if song:
        icon = colorize(Icon.MUSIC, Color.BRIGHT_CYAN)
        status = colorize('(继续播放)', Color.DIM) if is_same else colorize('(新歌曲)', Color.BRIGHT_GREEN)
        song_text = colorize(song, Color.WHITE)
        print(f"{ts} {icon} {song_text} {status}")
    else:
        icon = colorize(Icon.CIRCLE, Color.DIM)
        text = colorize('等待 MoeKoeMusic 播放...', Color.DIM)
        print(f"{ts} {icon} {text}")


def print_config_loaded(config_path: str):
    """打印配置加载信息"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    icon = colorize(Icon.CHECK, Color.BRIGHT_GREEN)
    text = colorize(f'配置文件已加载: {config_path}', Color.WHITE)
    print(f"{ts} {icon} {text}")


def print_connection_status(vrc_connected: bool):
    """打印连接状态"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    
    vrc_icon = colorize(Icon.CHECK, Color.BRIGHT_GREEN) if vrc_connected else colorize(Icon.CROSS, Color.BRIGHT_RED)
    vrc_text = colorize('VRChat OSC', Color.WHITE)
    vrc_status = colorize('已连接', Color.BRIGHT_GREEN) if vrc_connected else colorize('未连接', Color.BRIGHT_RED)
    
    print(f"{ts} {vrc_icon} {vrc_text}: {vrc_status}")


def print_startup_info(interval: int, message_format: str):
    """打印启动信息"""
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    
    print()
    interval_text = colorize(f'{interval}秒', Color.BRIGHT_CYAN)
    print(f"{ts} {Icon.INFO} 检测间隔: {interval_text}")
    
    format_text = colorize(message_format, Color.BRIGHT_WHITE)
    print(f"{ts} {Icon.INFO} 消息格式: {format_text}")
    
    print()
    print(colorize('─' * 54, Color.DIM))
    print(f"{ts} {Icon.ARROW} 开始监控... (按 Ctrl+C 停止)")
    print(colorize('─' * 54, Color.DIM))


def print_exit():
    """打印退出信息"""
    print()
    print(colorize('─' * 54, Color.DIM))
    ts = colorize(f'[{get_timestamp()}]', Color.DIM)
    print(f"{ts} {Icon.STOPPED} 程序已停止")
    print()


def build_lyrics_display(
    song: str,
    artist: str,
    current_time: float,
    total_time: float,
    prev_lyric: str | None,
    current_lyric: str | None,
    next_lyric: str | None,
    is_instrumental: bool = False
) -> str:
    """
    构建歌词显示界面（返回字符串）
    """
    lines = []
    
    # 顶部边框
    lines.append(colorize('┌' + '─' * 52 + '┐', Color.BRIGHT_CYAN))
    
    # 歌曲信息
    song_display = f"♪ {song} - {artist}"
    padding = max(0, (52 - len(song_display)) // 2)
    lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * padding + colorize(song_display, Color.BRIGHT_WHITE) + " " * max(0, 52 - padding - len(song_display)) + colorize('│', Color.BRIGHT_CYAN))
    
    # 分隔线
    lines.append(colorize('├' + '─' * 52 + '┤', Color.BRIGHT_CYAN))
    
    # 进度条
    progress_bar = create_progress_bar(current_time, total_time, 20)
    progress_line = f"  {progress_bar}  "
    padding = max(0, (52 - len(progress_line)) // 2)
    lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * padding + progress_line + " " * max(0, 52 - padding - len(progress_line)) + colorize('│', Color.BRIGHT_CYAN))
    
    # 分隔线
    lines.append(colorize('├' + '─' * 52 + '┤', Color.BRIGHT_CYAN))
    
    # 歌词显示区域
    if is_instrumental or not current_lyric:
        # 纯音乐显示
        lyric_text = "🎵 纯音乐，请欣赏 🎵"
        padding = max(0, (52 - len(lyric_text)) // 2)
        lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * padding + colorize(lyric_text, Color.BRIGHT_MAGENTA) + " " * max(0, 52 - padding - len(lyric_text)) + colorize('│', Color.BRIGHT_CYAN))
        lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * 52 + colorize('│', Color.BRIGHT_CYAN))
        lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * 52 + colorize('│', Color.BRIGHT_CYAN))
    else:
        # 显示歌词
        # 上一行（暗淡）- 使用显示宽度截断
        prev_text = truncate_by_display_width(prev_lyric, 48) if prev_lyric else ""
        prev_display = f"  {prev_text}"
        prev_padding = max(0, 52 - 2 - get_display_width(prev_text))
        lines.append(colorize('│', Color.BRIGHT_CYAN) + colorize(prev_display + " " * prev_padding, Color.DIM) + colorize('│', Color.BRIGHT_CYAN))
        
        # 当前行（高亮）- 使用显示宽度截断
        curr_text = truncate_by_display_width(current_lyric, 44) if current_lyric else "♪ ~ ♪"
        curr_display = f"  ▶ {curr_text}"
        curr_padding = max(0, 52 - 4 - get_display_width(curr_text))
        lines.append(colorize('│', Color.BRIGHT_CYAN) + colorize(curr_display + " " * curr_padding, Color.BRIGHT_GREEN) + colorize('│', Color.BRIGHT_CYAN))
        
        # 下一行（暗淡）- 使用显示宽度截断
        next_text = truncate_by_display_width(next_lyric, 48) if next_lyric else ""
        next_display = f"  {next_text}"
        next_padding = max(0, 52 - 2 - get_display_width(next_text))
        lines.append(colorize('│', Color.BRIGHT_CYAN) + colorize(next_display + " " * next_padding, Color.DIM) + colorize('│', Color.BRIGHT_CYAN))
    
    # 底部边框
    lines.append(colorize('└' + '─' * 52 + '┘', Color.BRIGHT_CYAN))
    
    return '\n'.join(lines)


# 全局变量：上次显示的内容
_last_display_lines = 0
_initialized = False


def refresh_lyrics_display(
    song: str,
    artist: str,
    current_time: float,
    total_time: float,
    prev_lyric: str | None = None,
    current_lyric: str | None = None,
    next_lyric: str | None = None,
    is_instrumental: bool = False
):
    """
    刷新歌词显示（覆盖之前的显示）
    """
    global _last_display_lines, _initialized
    
    # 构建新的显示内容
    display = build_lyrics_display(
        song, artist, current_time, total_time,
        prev_lyric, current_lyric, next_lyric, is_instrumental
    )
    
    lines_count = display.count('\n') + 1
    
    # 移动光标向上
    if _last_display_lines > 0:
        # 使用Windows兼容的方式
        for _ in range(_last_display_lines):
            sys.stdout.write('\033[F')  # 光标上移一行
            sys.stdout.write('\033[2K')  # 清除整行
    
    sys.stdout.flush()
    
    # 打印新内容
    print(display)
    
    _last_display_lines = lines_count
    _initialized = True


def print_lyrics_loading(song: str, artist: str):
    """打印歌词加载中状态"""
    global _last_display_lines
    
    # 先清除之前的显示
    if _last_display_lines > 0:
        for _ in range(_last_display_lines):
            sys.stdout.write('\033[F')
            sys.stdout.write('\033[2K')
        sys.stdout.flush()
    
    lines = []
    lines.append(colorize('┌' + '─' * 52 + '┐', Color.BRIGHT_CYAN))
    
    song_display = f"♪ {song} - {artist}"
    padding = max(0, (52 - len(song_display)) // 2)
    lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * padding + colorize(song_display, Color.BRIGHT_WHITE) + " " * max(0, 52 - padding - len(song_display)) + colorize('│', Color.BRIGHT_CYAN))
    
    lines.append(colorize('├' + '─' * 52 + '┤', Color.BRIGHT_CYAN))
    
    loading_text = "⏳ 正在获取歌词..."
    padding = max(0, (52 - len(loading_text)) // 2)
    lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * padding + colorize(loading_text, Color.BRIGHT_YELLOW) + " " * max(0, 52 - padding - len(loading_text)) + colorize('│', Color.BRIGHT_CYAN))
    
    for _ in range(4):
        lines.append(colorize('│', Color.BRIGHT_CYAN) + " " * 52 + colorize('│', Color.BRIGHT_CYAN))
    
    lines.append(colorize('└' + '─' * 52 + '┘', Color.BRIGHT_CYAN))
    
    display = '\n'.join(lines)
    print(display)
    _last_display_lines = len(lines)


def print_lyric_fetch_status(success: bool, message: str = ""):
    """打印歌词获取状态"""
    if success:
        print_status('success', f"歌词获取成功 {message}")
    else:
        print_status('warning', f"未找到歌词，显示为纯音乐")


# 启用ANSI支持
enable_ansi()
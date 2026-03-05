"""
歌词获取模块
通过酷狗API获取歌词（无需账号）
支持本地缓存
支持异步预加载
"""
import urllib.request
import urllib.parse
import json
import re
import base64
import os
import hashlib
import logging
import threading
from collections import OrderedDict
from dataclasses import dataclass
from typing import Optional, Callable
from concurrent.futures import ThreadPoolExecutor, Future

logger = logging.getLogger(__name__)

# 全局线程池用于异步加载
_executor: Optional[ThreadPoolExecutor] = None

# 缓存统计（用于性能监控）
_cache_stats = {
    'memory_hits': 0,
    'disk_hits': 0,
    'misses': 0,
    'requests': 0
}


def _get_executor() -> ThreadPoolExecutor:
    """获取全局线程池"""
    global _executor
    if _executor is None:
        # 根据CPU核心数动态调整线程数，最少2个，最多4个
        import os
        max_workers = min(4, max(2, (os.cpu_count() or 2)))
        _executor = ThreadPoolExecutor(max_workers=max_workers, thread_name_prefix="lyric_loader")
    return _executor


def get_cache_stats() -> dict:
    """获取缓存统计信息"""
    global _cache_stats
    total = _cache_stats['requests']
    if total == 0:
        return _cache_stats.copy()
    return {
        **_cache_stats,
        'memory_hit_rate': _cache_stats['memory_hits'] / total,
        'disk_hit_rate': _cache_stats['disk_hits'] / total,
        'miss_rate': _cache_stats['misses'] / total,
    }


class LRUCache:
    """LRU 缓存实现，限制内存使用"""
    
    def __init__(self, max_size: int = 100):
        """
        初始化 LRU 缓存
        
        Args:
            max_size: 最大缓存条目数
        """
        self._cache: OrderedDict = OrderedDict()
        self._max_size = max_size
    
    def get(self, key: str) -> Optional[object]:
        """获取缓存，命中时移动到末尾（最近使用）"""
        if key in self._cache:
            # 移动到末尾表示最近使用
            self._cache.move_to_end(key)
            return self._cache[key]
        return None
    
    def set(self, key: str, value: object):
        """设置缓存"""
        if key in self._cache:
            # 已存在，更新并移动到末尾
            self._cache.move_to_end(key)
            self._cache[key] = value
        else:
            # 新条目
            if len(self._cache) >= self._max_size:
                # 删除最旧的（开头的）
                self._cache.popitem(last=False)
            self._cache[key] = value
    
    def __contains__(self, key: str) -> bool:
        return key in self._cache
    
    def __getitem__(self, key: str) -> object:
        return self.get(key)
    
    def __setitem__(self, key: str, value: object):
        self.set(key, value)
    
    def clear(self):
        """清空缓存"""
        self._cache.clear()
    
    @property
    def size(self) -> int:
        return len(self._cache)


@dataclass
class LyricWord:
    """逐字歌词单元"""
    text: str      # 文字
    start_time: float  # 开始时间（秒）
    duration: float    # 持续时间（秒）
    
    @property
    def end_time(self) -> float:
        return self.start_time + self.duration


@dataclass
class LyricLine:
    """单行歌词"""
    time: float  # 时间（秒）
    text: str    # 歌词文本
    words: list = None  # 逐字歌词列表（可选）
    duration: float = 0  # 该行持续时间（秒）
    translation: str = ""  # 翻译文本（可选）
    
    def get_word_at_time(self, current_time: float) -> tuple[int, str, str]:
        """
        获取当前时间对应的文字索引和已唱/未唱文本
        
        Returns:
            tuple: (当前字索引, 已唱文本, 未唱文本)
        """
        if not self.words:
            # 没有逐字信息，根据进度估算
            if self.duration <= 0:
                return -1, self.text, ""
            progress = min(1.0, max(0, (current_time - self.time) / self.duration))
            char_count = len(self.text)
            sung_count = int(progress * char_count)
            return sung_count, self.text[:sung_count], self.text[sung_count:]
        
        # 有逐字信息
        for i, word in enumerate(self.words):
            if current_time < word.end_time:
                if current_time < word.start_time:
                    # 还没唱到这个字
                    sung = ''.join(w.text for w in self.words[:i])
                    unsung = ''.join(w.text for w in self.words[i:])
                    return i, sung, unsung
                else:
                    # 正在唱这个字
                    sung = ''.join(w.text for w in self.words[:i])
                    unsung = ''.join(w.text for w in self.words[i:])
                    return i, sung, unsung
        
        # 这行已经唱完
        return len(self.words), self.text, ""


@dataclass
class SongInfo:
    """歌曲完整信息"""
    duration: float  # 歌曲时长（秒）
    lyrics: str      # 歌词文本
    is_instrumental: bool  # 是否纯音乐
    lyric_lines: list = None  # 解析后的歌词行（包含逐字信息）
    translated_lines: dict = None  # 翻译后的歌词行 {行索引: 翻译文本}
    translation_pending: bool = False  # 是否正在翻译中
    tags: list = None  # 歌曲标签 ['hot', 'new', 'favorite', 'vip', 'original', 'cover', 'live', 'remix']
    album_name: str = ""  # 专辑名称


class LyricCache:
    """本地歌词缓存"""
    
    def __init__(self, cache_dir: str = "lyrics_cache"):
        self.cache_dir = cache_dir
        self._ensure_cache_dir()
        self._path_cache: dict[str, str] = {}  # 路径缓存
    
    def _ensure_cache_dir(self):
        """确保缓存目录存在"""
        if not os.path.exists(self.cache_dir):
            os.makedirs(self.cache_dir)
    
    def _get_cache_path(self, song: str, artist: str) -> str:
        """获取缓存文件路径（带路径缓存）"""
        key = f"{song}_{artist}"
        if key in self._path_cache:
            return self._path_cache[key]
        
        hash_key = hashlib.md5(key.encode('utf-8')).hexdigest()
        path = os.path.join(self.cache_dir, f"{hash_key}.json")
        
        # 缓存路径（限制大小）
        if len(self._path_cache) < 100:
            self._path_cache[key] = path
        
        return path
    
    def get(self, song: str, artist: str) -> Optional[dict]:
        """读取缓存"""
        global _cache_stats
        _cache_stats['requests'] += 1
        
        path = self._get_cache_path(song, artist)
        if os.path.exists(path):
            try:
                with open(path, 'r', encoding='utf-8') as f:
                    result = json.load(f)
                _cache_stats['disk_hits'] += 1
                return result
            except Exception as e:
                logger.debug(f"读取缓存失败: {path}, 错误: {e}")
                _cache_stats['misses'] += 1
                return None
        _cache_stats['misses'] += 1
        return None
    
    def set(self, song: str, artist: str, data: dict):
        """写入缓存"""
        path = self._get_cache_path(song, artist)
        try:
            with open(path, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
        except Exception as e:
            logger.debug(f"写入缓存失败: {path}, 错误: {e}")
    
    def cleanup(self, max_size_mb: int = 50, max_age_days: int = 30) -> dict:
        """
        清理缓存
        
        Args:
            max_size_mb: 最大缓存大小（MB）
            max_age_days: 缓存最大天数
        
        Returns:
            清理统计 {'deleted': int, 'freed_mb': float}
        """
        if not os.path.exists(self.cache_dir):
            return {'deleted': 0, 'freed_mb': 0}
        
        import time
        now = time.time()
        max_age_seconds = max_age_days * 24 * 60 * 60
        deleted = 0
        freed_bytes = 0
        
        # 收集所有缓存文件及其信息
        files = []
        total_size = 0
        
        for filename in os.listdir(self.cache_dir):
            if not filename.endswith('.json'):
                continue
            
            filepath = os.path.join(self.cache_dir, filename)
            try:
                stat = os.stat(filepath)
                files.append({
                    'path': filepath,
                    'mtime': stat.st_mtime,
                    'size': stat.st_size
                })
                total_size += stat.st_size
            except:
                pass
        
        # 按修改时间排序（旧的在前）
        files.sort(key=lambda x: x['mtime'])
        
        # 先删除过期文件
        for f in files:
            if now - f['mtime'] > max_age_seconds:
                try:
                    os.remove(f['path'])
                    deleted += 1
                    freed_bytes += f['size']
                except:
                    pass
        
        # 如果还是太大，删除最旧的文件
        max_size_bytes = max_size_mb * 1024 * 1024
        remaining_size = total_size - freed_bytes
        
        for f in files:
            if remaining_size <= max_size_bytes:
                break
            if now - f['mtime'] <= max_age_seconds:  # 不删除未过期的
                try:
                    os.remove(f['path'])
                    deleted += 1
                    freed_bytes += f['size']
                    remaining_size -= f['size']
                except:
                    pass
        
        # 清空路径缓存
        self._path_cache.clear()
        
        return {
            'deleted': deleted,
            'freed_mb': round(freed_bytes / 1024 / 1024, 2)
        }


class LyricFetcher:
    """歌词获取器 - 使用酷狗API"""
    
    SEARCH_URL = "https://mobileservice.kugou.com/api/v3/search/song"
    LYRIC_SEARCH_URL = "https://lyrics.kugou.com/search"
    LYRIC_DOWNLOAD_URL = "https://lyrics.kugou.com/download"
    
    def __init__(self, timeout: int = 10, cache_dir: str = "lyrics_cache", max_memory_cache: int = 50):
        """
        初始化歌词获取器
        
        Args:
            timeout: 请求超时时间（秒）
            cache_dir: 磁盘缓存目录
            max_memory_cache: 内存缓存最大条目数
        """
        self.timeout = timeout
        self._memory_cache = LRUCache(max_memory_cache)  # LRU 内存缓存
        self._disk_cache = LyricCache(cache_dir)  # 磁盘缓存
        self._pending_loads: dict[str, Future] = {}  # 正在加载的任务
        self._load_callbacks: dict[str, list[Callable]] = {}  # 加载完成回调
        self._translator = None  # 翻译器引用（延迟导入）
    
    def preload_song_info(self, song: str, artist: str = "", prefer_chinese: bool = False,
                          callback: Callable[[str, str, 'SongInfo'], None] = None) -> Optional[Future]:
        """
        异步预加载歌曲信息（歌词+时长）
        
        Args:
            song: 歌曲名
            artist: 歌手名
            prefer_chinese: 是否优先中文
            callback: 加载完成回调，参数为 (song, artist, SongInfo)
        
        Returns:
            Future: 异步任务，可用来取消或等待
        """
        global _cache_stats
        cache_key = f"{song}_{artist}_cn{prefer_chinese}"
        
        # 检查是否已有内存缓存
        cached = self._memory_cache.get(cache_key)
        if cached is not None:
            _cache_stats['memory_hits'] += 1
            _cache_stats['requests'] += 1
            if callback:
                callback(song, artist, cached)
            return None
        
        # 检查磁盘缓存
        disk_data = self._disk_cache.get(song, artist)
        if disk_data:
            result = SongInfo(
                duration=disk_data.get('duration', 0),
                lyrics=disk_data.get('lyrics', ''),
                is_instrumental=disk_data.get('is_instrumental', True)
            )
            self._memory_cache[cache_key] = result
            if callback:
                callback(song, artist, result)
            return None
        
        # 检查是否正在加载
        if cache_key in self._pending_loads:
            if callback:
                if cache_key not in self._load_callbacks:
                    self._load_callbacks[cache_key] = []
                self._load_callbacks[cache_key].append(callback)
            return self._pending_loads[cache_key]
        
        # 启动异步加载
        def load_and_callback():
            try:
                result = self.get_song_info(song, artist, prefer_chinese)
                # 调用所有等待的回调
                if cache_key in self._load_callbacks:
                    for cb in self._load_callbacks[cache_key]:
                        try:
                            cb(song, artist, result)
                        except Exception:
                            pass
                    del self._load_callbacks[cache_key]
                return result
            finally:
                self._pending_loads.pop(cache_key, None)
        
        if callback:
            if cache_key not in self._load_callbacks:
                self._load_callbacks[cache_key] = []
            self._load_callbacks[cache_key].append(callback)
        
        future = _get_executor().submit(load_and_callback)
        self._pending_loads[cache_key] = future
        return future
    
    def is_loading(self, song: str, artist: str = "", prefer_chinese: bool = False) -> bool:
        """检查歌曲是否正在加载中"""
        cache_key = f"{song}_{artist}_cn{prefer_chinese}"
        return cache_key in self._pending_loads
    
    def _make_request(self, url: str, headers: dict = None) -> Optional[dict]:
        """发送HTTP请求"""
        try:
            default_headers = {
                'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
                'Referer': 'https://www.kugou.com/'
            }
            if headers:
                default_headers.update(headers)
            
            req = urllib.request.Request(url, headers=default_headers)
            with urllib.request.urlopen(req, timeout=self.timeout) as response:
                data = response.read().decode('utf-8')
                return json.loads(data)
        except Exception as e:
            logger.debug(f"HTTP请求失败: {url}, 错误: {e}")
            return None
    
    def _search_song(self, song: str, artist: str = "") -> Optional[dict]:
        """搜索歌曲获取信息"""
        query = f"{song} {artist}".strip()
        url = f"{self.SEARCH_URL}?format=json&keyword={urllib.parse.quote(query)}&page=1&pagesize=5"
        
        headers = {
            'User-Agent': 'Android9.0-AndroidPhone-10050-120-0-searchsongwidget-wifi',
            'kg-tid': '10050'
        }
        
        result = self._make_request(url, headers)
        if result and result.get('data', {}).get('info'):
            return result['data']['info'][0]
        return None
    
    def _normalize_song_name(self, name: str) -> str:
        """标准化歌曲名，去除括号内容和特殊字符"""
        if not name:
            return ""
        # 去除括号内容（Feat、Ver、Mix、Remix等）
        name = re.sub(r'\s*[\(\［【].*?[\)\］】]', '', name)
        # 去除空格和特殊字符
        name = re.sub(r'\s+', ' ', name).strip()
        return name.lower()

    def _search_lyric(self, song: str, artist: str, song_hash: str = "", prefer_chinese: bool = False, expected_duration: float = 0) -> Optional[dict]:
        """搜索歌词获取id和accesskey，使用智能匹配算法"""
        query = f"{song} {artist}".strip()
        url = f"{self.LYRIC_SEARCH_URL}?ver=1&man=yes&client=pc&keyword={urllib.parse.quote(query)}&hash={song_hash}"
        
        result = self._make_request(url)
        if result and result.get('candidates'):
            candidates = result['candidates']
            
            # 标准化搜索词
            normalized_song = self._normalize_song_name(song)
            normalized_artist = self._normalize_song_name(artist)
            
            # 智能匹配评分
            matched = []
            for c in candidates:
                c_song = c.get('song', '')
                c_artist = c.get('singer', '')
                c_duration = c.get('duration', 0)
                score = 0
                
                # 1. 歌曲名匹配（使用标准化名称）
                c_normalized = self._normalize_song_name(c_song)
                if normalized_song and normalized_song == c_normalized:
                    score += 50  # 完全匹配，高分
                elif normalized_song and normalized_song in c_normalized:
                    score += 30  # 包含匹配
                elif song.lower() in c_song.lower():
                    score += 15  # 简单包含匹配
                
                # 2. 歌手名匹配
                if normalized_artist and normalized_artist in self._normalize_song_name(c_artist):
                    score += 20
                
                # 3. 时长验证（如果有预期时长）
                if expected_duration > 0 and c_duration > 0:
                    # 时长差异在5秒以内，加分
                    time_diff = abs(expected_duration - c_duration)
                    if time_diff <= 5:
                        score += 40  # 时长匹配，高分
                    elif time_diff <= 15:
                        score += 20  # 时长接近
                    elif time_diff > 60:
                        score -= 30  # 时长差异太大，扣分
                
                # 4. 版本匹配（避免选择Live/Remix等特殊版本）
                # 如果原歌曲名不包含这些关键词，而候选包含，扣分
                version_keywords = ['live', 'remix', 'mix', '混音', '现场', '演唱会', '翻唱', 'cover']
                song_lower = song.lower()
                c_lower = c_song.lower()
                for kw in version_keywords:
                    if kw not in song_lower and kw in c_lower:
                        score -= 15
                
                # 5. 中文优先
                if prefer_chinese and self._has_chinese(c_song):
                    score += 10
                
                # 6. 有翻译标记加分
                if prefer_chinese and ('翻译' in c_song or 'trans' in c_song.lower()):
                    score += 5
                
                matched.append((score, c))
            
            # 按分数排序，返回最高分的（且分数>0）
            matched.sort(key=lambda x: x[0], reverse=True)
            
            # 返回最高分的候选，或者第一个候选（如果都没有分数）
            for score_item, candidate in matched:
                if score_item > 0:
                    logger.debug(f"歌词匹配: {song} - {artist} -> {candidate.get('song', '')} (分数: {score_item})")
                    return candidate
            
            # 如果所有候选分数都为0或负数，返回第一个
            return candidates[0] if candidates else None
        
        return None
    
    def _has_chinese(self, text: str) -> bool:
        """检查文本是否包含中文"""
        return any('\u4e00' <= ch <= '\u9fff' for ch in text)
    
    def _extract_tags(self, song_data: dict) -> list:
        """
        从歌曲数据中提取标签
        
        酷狗API返回的标签字段:
        - privilege: 权限标识 (0=免费, 1=VIP)
        - is_hot: 是否热门
        - is_new: 是否新歌
        - feetype: 收费类型 (0=免费, 1=VIP, 2=付费)
        - original_id: 原曲ID (如果有则为翻唱)
        - 歌曲名中的关键词: Live, Remix, cover等
        
        Returns:
            list: 标签列表 ['hot', 'new', 'favorite', 'vip', 'original', 'cover', 'live', 'remix']
        """
        tags = []
        
        if not song_data:
            return tags
        
        # 热门标签
        if song_data.get('is_hot') or song_data.get('hot'):
            tags.append('hot')
        
        # 新歌标签
        if song_data.get('is_new') or song_data.get('new_song'):
            tags.append('new')
        
        # VIP标签
        privilege = song_data.get('privilege', 0)
        feetype = song_data.get('feetype', 0)
        if privilege > 0 or feetype > 0:
            tags.append('vip')
        
        # 从歌曲名中检测标签
        song_name = song_data.get('songname', '') or song_data.get('song', '')
        filename = song_data.get('filename', '')  # 有时包含更多信息
        
        full_name = f"{song_name} {filename}".lower()
        
        # 现场版
        if 'live' in full_name or '现场' in full_name or '演唱会' in full_name:
            tags.append('live')
        
        # 混音版
        if 'remix' in full_name or 'mix' in full_name or '混音' in full_name or 'remastered' in full_name:
            if 'live' not in tags:  # 避免重复
                tags.append('remix')
        
        # 翻唱版
        if 'cover' in full_name or '翻唱' in full_name:
            tags.append('cover')
        
        # 原创标识（没有翻唱标记且没有原曲ID）
        if 'cover' not in tags and not song_data.get('original_id'):
            # 可以考虑标记为原创，但太普遍了，暂不添加
            pass
        
        # 收藏标签需要用户数据，API可能不返回
        # 如果有收藏相关字段
        if song_data.get('is_favorite') or song_data.get('collected'):
            tags.append('favorite')
        
        return tags
    
    def _download_lyric(self, lyric_id: str, accesskey: str) -> Optional[str]:
        """下载歌词"""
        url = f"{self.LYRIC_DOWNLOAD_URL}?ver=1&client=pc&id={lyric_id}&accesskey={accesskey}&fmt=lrc&charset=utf8"
        
        result = self._make_request(url)
        if result and result.get('content'):
            try:
                return base64.b64decode(result['content']).decode('utf-8')
            except Exception as e:
                logger.debug(f"歌词解码失败: lyric_id={lyric_id}, 错误: {e}")
                return None
        return None
    
    def _download_krc(self, lyric_id: str, accesskey: str) -> Optional[bytes]:
        """下载KRC逐字歌词（二进制格式）"""
        url = f"{self.LYRIC_DOWNLOAD_URL}?ver=1&client=pc&id={lyric_id}&accesskey={accesskey}&fmt=krc&charset=utf8"
        
        result = self._make_request(url)
        if result and result.get('content'):
            try:
                return base64.b64decode(result['content'])
            except Exception as e:
                logger.debug(f"KRC歌词解码失败: lyric_id={lyric_id}, 错误: {e}")
                return None
        return None
    
    def _decrypt_krc(self, krc_data: bytes) -> Optional[bytes]:
        """
        解密KRC格式歌词
        KRC使用XOR加密，密钥是固定的
        """
        # KRC文件头 "krc1" 或 "krc2"
        if len(krc_data) < 4:
            return None
        
        header = krc_data[:4]
        if header not in [b'krc1', b'krc2']:
            return None
        
        # KRC加密密钥
        key = [64, 71, 97, 119, 94, 50, 116, 71, 81, 54, 49, 45, 206, 210, 110, 105]
        
        # XOR解密
        decrypted = bytearray()
        encrypted_data = krc_data[4:]  # 跳过头部
        
        for i, byte in enumerate(encrypted_data):
            decrypted.append(byte ^ key[i % len(key)])
        
        # 解压缩 (zlib)
        import zlib
        try:
            decompressed = zlib.decompress(bytes(decrypted))
            return decompressed
        except Exception as e:
            logger.debug(f"KRC解压失败: {e}")
            return None
    
    def get_song_info(self, song: str, artist: str = "", prefer_chinese: bool = False) -> SongInfo:
        """
        获取歌曲完整信息（歌词+时长）
        优先使用缓存，优先获取逐字歌词
        
        Args:
            song: 歌曲名
            artist: 歌手名
            prefer_chinese: 是否优先获取有中文翻译的版本
        
        Returns:
            SongInfo: 包含时长、歌词、是否纯音乐、逐字歌词行
        """
        cache_key = f"{song}_{artist}_cn{prefer_chinese}"
        
        # 1. 检查内存缓存
        if cache_key in self._memory_cache:
            return self._memory_cache[cache_key]
        
        # 2. 检查磁盘缓存
        disk_data = self._disk_cache.get(song, artist)
        if disk_data:
            lyric_lines = None
            
            # 优先使用保存的逐字信息
            if disk_data.get('word_timing'):
                lyric_lines = []
                for line_data in disk_data['word_timing']:
                    words = None
                    if line_data.get('words'):
                        words = [
                            LyricWord(
                                text=w['text'],
                                start_time=w['start_time'],
                                duration=w['duration']
                            )
                            for w in line_data['words']
                        ]
                    lyric_lines.append(LyricLine(
                        time=line_data['time'],
                        text=line_data['text'],
                        words=words,
                        translation=line_data.get('translation', '')  # 加载翻译
                    ))
            elif disk_data.get('lyrics'):
                # 回退：解析普通LRC
                lyric_lines = parse_lrc(disk_data.get('lyrics', ''))
            
            # 加载翻译结果
            translated_lines = disk_data.get('translated_lines', {})
            
            result = SongInfo(
                duration=disk_data.get('duration', 0),
                lyrics=disk_data.get('lyrics', ''),
                is_instrumental=disk_data.get('is_instrumental', True),
                lyric_lines=lyric_lines,
                translated_lines=translated_lines if translated_lines else None,
                tags=disk_data.get('tags', []),
                album_name=disk_data.get('album_name', '')
            )
            self._memory_cache[cache_key] = result
            return result
        
        # 3. 从API获取
        default_result = SongInfo(duration=0, lyrics="", is_instrumental=True, lyric_lines=None, tags=[])
        
        try:
            # 搜索歌曲获取时长
            song_data = self._search_song(song, artist)
            if not song_data:
                return default_result
            
            duration = song_data.get('duration', 0)
            song_hash = song_data.get('hash', '')
            
            # 提取歌曲标签
            tags = self._extract_tags(song_data)
            album_name = song_data.get('album_name', '')
            
            # 搜索歌词（优先中文版本），传入已知的时长进行验证
            lyric_info = self._search_lyric(song, artist, song_hash, prefer_chinese, expected_duration=duration)
            if not lyric_info:
                result = SongInfo(duration=duration, lyrics="", is_instrumental=True, lyric_lines=None, tags=tags, album_name=album_name)
                self._memory_cache[cache_key] = result
                return result
            
            lyric_id = lyric_info.get('id')
            accesskey = lyric_info.get('accesskey')
            
            # 优先尝试下载KRC逐字歌词
            lyric_lines = None
            lrc = None
            
            krc_data = self._download_krc(str(lyric_id), accesskey)
            if krc_data:
                decrypted = self._decrypt_krc(krc_data)
                if decrypted:
                    lyric_lines = parse_krc(decrypted)
                    # 生成普通LRC格式用于缓存
                    if lyric_lines:
                        lrc_lines = []
                        for line in lyric_lines:
                            mins = int(line.time // 60)
                            secs = int(line.time % 60)
                            ms = int((line.time % 1) * 100)
                            lrc_lines.append(f"[{mins:02d}:{secs:02d}.{ms:02d}]{line.text}")
                        lrc = '\n'.join(lrc_lines)
            
            # 如果KRC失败，下载普通LRC
            if not lrc:
                lrc = self._download_lyric(str(lyric_id), accesskey)
                if lrc:
                    lyric_lines = parse_lrc(lrc)
            
            if lrc:
                is_inst = self._check_instrumental(lrc, song, artist)
                result = SongInfo(
                    duration=duration,
                    lyrics=lrc,
                    is_instrumental=is_inst,
                    lyric_lines=lyric_lines,
                    tags=tags,
                    album_name=album_name
                )
            else:
                result = SongInfo(duration=duration, lyrics="", is_instrumental=True, lyric_lines=None, tags=tags, album_name=album_name)
            
            # 缓存结果（包含逐字信息和标签）
            cache_data = {
                'song': song,
                'artist': artist,
                'duration': result.duration,
                'lyrics': result.lyrics,
                'is_instrumental': result.is_instrumental,
                'has_word_timing': lyric_lines is not None and len(lyric_lines) > 0 and lyric_lines[0].words is not None,
                'tags': tags,
                'album_name': album_name
            }
            # 如果有逐字信息，额外保存
            if lyric_lines and lyric_lines[0].words:
                cache_data['word_timing'] = [
                    {
                        'time': line.time,
                        'text': line.text,
                        'translation': line.translation,  # 保存翻译
                        'words': [
                            {'text': w.text, 'start_time': w.start_time, 'duration': w.duration}
                            for w in line.words
                        ] if line.words else None
                    }
                    for line in lyric_lines
                ]
            # 保存翻译结果
            if result.translated_lines:
                cache_data['translated_lines'] = result.translated_lines
            
            self._disk_cache.set(song, artist, cache_data)
            
            return result
            
        except Exception as e:
            logger.warning(f"获取歌曲信息失败: {song} - {artist}, 错误: {e}")
            return default_result
    
    def get_song_info_by_hash(self, song_hash: str) -> Optional[SongInfo]:
        """
        通过歌曲 hash 获取歌词信息（更精确）
        
        酷狗音乐的 hash 是歌曲的唯一标识，可以直接查询歌词。
        
        Args:
            song_hash: 歌曲的 hash 值（来自 MoeKoeMusic）
        
        Returns:
            SongInfo 或 None
        """
        if not song_hash:
            return None
        
        # 检查缓存
        cache_key = f"hash_{song_hash}"
        if cache_key in self._memory_cache:
            return self._memory_cache[cache_key]
        
        default_result = SongInfo(duration=0, lyrics="", is_instrumental=True, lyric_lines=None, tags=[])
        
        try:
            # 直接通过 hash 搜索歌词
            lyric_info = self._search_lyric_by_hash(song_hash)
            if not lyric_info:
                self._memory_cache[cache_key] = default_result
                return default_result
            
            lyric_id = lyric_info.get('id')
            accesskey = lyric_info.get('accesskey')
            duration = lyric_info.get('duration', 0)
            
            # 优先尝试下载 KRC 逐字歌词
            lyric_lines = None
            
            krc_data = self._download_krc(str(lyric_id), accesskey)
            if krc_data:
                decrypted = self._decrypt_krc(krc_data)
                if decrypted:
                    lyric_lines = parse_krc(decrypted)
            
            result = SongInfo(
                duration=duration,
                lyrics="",
                is_instrumental=False,
                lyric_lines=lyric_lines,
                tags=[]
            )
            
            self._memory_cache[cache_key] = result
            return result
            
        except Exception as e:
            logger.warning(f"通过 hash 获取歌词失败: {song_hash}, 错误: {e}")
            return default_result
    
    def preload_by_hash(self, song_hash: str, callback: Callable[[str, str, 'SongInfo'], None] = None) -> Optional[Future]:
        """
        通过 hash 异步预加载歌词
        
        Args:
            song_hash: 歌曲hash
            callback: 加载完成回调，参数为 (song, artist, SongInfo)
        
        Returns:
            Future: 异步任务
        """
        cache_key = f"hash_{song_hash}"
        
        # 检查内存缓存
        cached = self._memory_cache.get(cache_key)
        if cached is not None:
            _cache_stats['memory_hits'] += 1
            _cache_stats['requests'] += 1
            if callback:
                callback("", "", cached)
            return None
        
        # 检查是否正在加载
        if cache_key in self._pending_loads:
            if callback:
                if cache_key not in self._load_callbacks:
                    self._load_callbacks[cache_key] = []
                self._load_callbacks[cache_key].append(callback)
            return self._pending_loads[cache_key]
        
        # 启动异步加载
        def load_and_callback():
            try:
                result = self.get_song_info_by_hash(song_hash)
                # 调用所有等待的回调
                if cache_key in self._load_callbacks:
                    for cb in self._load_callbacks[cache_key]:
                        try:
                            cb("", "", result)
                        except Exception:
                            pass
                    del self._load_callbacks[cache_key]
                return result
            finally:
                self._pending_loads.pop(cache_key, None)
        
        if callback:
            if cache_key not in self._load_callbacks:
                self._load_callbacks[cache_key] = []
            self._load_callbacks[cache_key].append(callback)
        
        future = _get_executor().submit(load_and_callback)
        self._pending_loads[cache_key] = future
        return future
    
    def _search_lyric_by_hash(self, song_hash: str) -> Optional[dict]:
        """通过 hash 直接搜索歌词"""
        url = f"{self.LYRIC_SEARCH_URL}?ver=1&man=yes&client=pc&keyword=&hash={song_hash}"
        
        result = self._make_request(url)
        if result and result.get('candidates'):
            return result['candidates'][0]
        return None
    
    def translate_lyrics(self, song_info: SongInfo, callback: Callable[[SongInfo], None] = None) -> Optional[Future]:
        """
        异步翻译歌词
        
        Args:
            song_info: 歌曲信息对象
            callback: 翻译完成回调
        
        Returns:
            Future对象或None
        """
        if not song_info.lyric_lines:
            return None
        
        # 检查翻译器是否可用
        if self._translator is None:
            try:
                from translator import get_translator
                self._translator = get_translator()
            except ImportError:
                logger.debug("翻译模块未加载")
                return None
        
        if not self._translator or not self._translator.is_available():
            return None
        
        def do_translate():
            try:
                song_info.translation_pending = True
                translations = {}
                
                for i, line in enumerate(song_info.lyric_lines):
                    # 跳过空行和元数据
                    if not line.text or not line.text.strip():
                        continue
                    
                    # 跳过中文歌词
                    if self._is_chinese(line.text):
                        continue
                    
                    translation = self._translator.translate_text(line.text)
                    if translation:
                        translations[i] = translation
                        line.translation = translation
                
                song_info.translated_lines = translations
                song_info.translation_pending = False
                
                if callback:
                    callback(song_info)
                
                return song_info
            except Exception as e:
                logger.warning(f"翻译歌词失败: {e}")
                song_info.translation_pending = False
                return None
        
        return _get_executor().submit(do_translate)
    
    def _is_chinese(self, text: str) -> bool:
        """检查文本是否主要是中文"""
        if not text:
            return False
        chinese_count = sum(1 for ch in text if '\u4e00' <= ch <= '\u9fff')
        return chinese_count > len(text) * 0.3
    
    def get_lyrics(self, song: str, artist: str = "") -> tuple[Optional[str], bool]:
        """获取歌词（兼容旧接口）"""
        info = self.get_song_info(song, artist)
        return info.lyrics if info.lyrics else None, info.is_instrumental
    
    def _check_instrumental(self, lrc: str, song: str, artist: str) -> bool:
        """检查是否是纯音乐"""
        lower_song = song.lower()
        
        for keyword in ['instrumental', 'pure music', 'bgm', 'ost', '伴奏', '纯音乐']:
            if keyword in lower_song:
                return True
        
        lines = lrc.strip().split('\n')
        lyric_content = []
        for line in lines:
            match = re.search(r'\[(\d+:\d+[\.\d+]*)\](.*)', line)
            if match:
                text = match.group(2).strip()
                if text and not text.startswith('['):
                    lyric_content.append(text)
        
        combined_text = ' '.join(lyric_content).lower()
        for keyword in ['纯音乐', 'instrumental', '纯音乐请欣赏']:
            if keyword in combined_text:
                return True
        
        return len(lyric_content) < 3


def parse_krc(krc_content: bytes) -> list[LyricLine]:
    """
    解析KRC逐字歌词格式
    
    KRC格式说明:
    - 每行以 [开始时间,持续时间] 开头
    - 每个字用 <开始偏移,持续时间,标志> 格式，后面紧跟实际文字
    - 时间单位是毫秒
    - 例如: [0,2250]<0,160,0>晴<160,160,0>天
    """
    if not krc_content:
        return []
    
    lines = []
    
    try:
        text = krc_content.decode('utf-8', errors='ignore')
    except Exception:
        return []
    
    # 匹配行级时间标签和内容
    line_pattern = re.compile(r'\[(\d+),(\d+)\](.+)')
    # 匹配逐字标签 <开始偏移,持续时间,标志>文字
    # 注意：文字在标签后面，不在标签里面
    word_pattern = re.compile(r'<(\d+),(\d+),(\d*)>')
    
    for line in text.split('\n'):
        line_match = line_pattern.match(line)
        if not line_match:
            continue
        
        start_time = int(line_match.group(1)) / 1000.0  # 转换为秒
        duration = int(line_match.group(2)) / 1000.0
        content = line_match.group(3)
        
        # 解析逐字标签
        words = []
        text_parts = []
        
        # 找到所有标签位置
        last_end = 0
        for match in word_pattern.finditer(content):
            # 标签前的文字（如果有）
            if match.start() > last_end:
                between_text = content[last_end:match.start()]
                if between_text and not between_text.isspace():
                    # 这可能是上一个标签的文字（如果正则没匹配到）
                    pass
            
            word_start_offset = int(match.group(1)) / 1000.0
            word_duration = int(match.group(2)) / 1000.0
            # word_flag = match.group(3)  # 标志，暂不使用
            
            # 找到这个标签后面的文字（直到下一个标签或行尾）
            tag_end = match.end()
            # 查找下一个标签的位置
            next_match = word_pattern.search(content, tag_end)
            if next_match:
                word_text = content[tag_end:next_match.start()]
            else:
                word_text = content[tag_end:]
            
            # 保留空格（英语歌词需要空格分隔单词）
            # 但跳过纯空格且没有时长的"单词"
            if word_text:
                # 如果是纯空格且有持续时间，也保留（英语歌词的空格）
                if word_text.isspace():
                    if word_duration > 0:
                        words.append(LyricWord(
                            text=word_text,
                            start_time=start_time + word_start_offset,
                            duration=word_duration
                        ))
                        text_parts.append(word_text)
                else:
                    # 非纯空格，正常添加
                    words.append(LyricWord(
                        text=word_text,
                        start_time=start_time + word_start_offset,
                        duration=word_duration
                    ))
                    text_parts.append(word_text)
            
            last_end = tag_end
        
        # 如果没有逐字标签，尝试提取纯文本
        if not words:
            full_text = re.sub(word_pattern, '', content).strip()
            if full_text:
                lines.append(LyricLine(
                    time=start_time,
                    text=full_text,
                    words=None,
                    duration=duration
                ))
        else:
            full_text = ''.join(text_parts)
            if full_text:
                lines.append(LyricLine(
                    time=start_time,
                    text=full_text,
                    words=words,
                    duration=duration
                ))
    
    lines.sort(key=lambda x: x.time)
    return lines


def parse_lrc(lrc_text: str) -> list[LyricLine]:
    """解析LRC格式歌词"""
    if not lrc_text:
        return []
    
    lines = []
    
    for line in lrc_text.split('\n'):
        matches = re.findall(r'\[(\d+):(\d+)[\.\:](\d+)\]', line)
        
        if matches:
            for match in matches:
                minutes = int(match[0])
                seconds = int(match[1])
                milliseconds = int(match[2])
                
                total_time = minutes * 60 + seconds + milliseconds / 100
                text = re.sub(r'\[\d+:\d+[\.\:]\d+\]', '', line).strip()
                
                if text and not text.startswith('['):
                    lines.append(LyricLine(time=total_time, text=text))
    
    lines.sort(key=lambda x: x.time)
    return lines


def get_current_lyric(lines: list[LyricLine], current_time: float) -> tuple[Optional[LyricLine], Optional[LyricLine], Optional[LyricLine], int]:
    """
    获取当前时间对应的歌词（优化版，返回索引）
    
    Returns:
        tuple: (上一句, 当前句, 下一句, 当前索引)
    """
    if not lines:
        return None, None, None, -1
    
    prev_line = None
    current_line = None
    next_line = None
    current_idx = -1
    
    for i, line in enumerate(lines):
        if line.time <= current_time:
            prev_line = current_line
            current_line = line
            current_idx = i
            next_line = lines[i + 1] if i + 1 < len(lines) else None
        else:
            if current_line is None:
                next_line = line
            break
    
    return prev_line, current_line, next_line, current_idx


def build_progress_bar(current: float, total: float, width: int = 10) -> str:
    """
    构建精致进度条（支持半格精度）
    
    使用字符：
    - ▓ 已填满
    - ▒ 半填充
    - ▱ 未填充
    """
    if total <= 0:
        return "▱" * width
    
    progress = min(max(current / total, 0), 1.0)
    
    # 使用双倍精度计算（支持半格）
    total_units = width * 2
    filled_units = int(progress * total_units)
    
    full_blocks = filled_units // 2  # 完整块数
    half_block = filled_units % 2    # 是否有半块
    
    bar = "▓" * full_blocks
    if half_block:
        bar += "▒"
    bar += "▱" * (width - full_blocks - half_block)
    
    return bar


def format_time(seconds: float) -> str:
    """格式化时间为 mm:ss"""
    if seconds <= 0:
        return "0:00"
    minutes = int(seconds // 60)
    secs = int(seconds % 60)
    return f"{minutes}:{secs:02d}"
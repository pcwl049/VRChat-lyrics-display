"""
歌词翻译模块
支持多种翻译服务：Google、DeepL、百度等
"""

import logging
import re
import time
from typing import Optional, List, Dict, Callable
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor, Future
from functools import lru_cache

logger = logging.getLogger(__name__)

# 翻译服务类型
TRANSLATE_SERVICE_GOOGLE = "google"
TRANSLATE_SERVICE_DEEPL = "deepl"
TRANSLATE_SERVICE_BAIDU = "baidu"


@dataclass
class TranslationConfig:
    """翻译配置"""
    enabled: bool = False
    service: str = TRANSLATE_SERVICE_GOOGLE
    source_lang: str = "auto"  # 自动检测
    target_lang: str = "zh-CN"  # 翻译目标语言
    cache_enabled: bool = True
    max_retries: int = 3
    retry_delay: float = 1.0  # 重试延迟（秒）
    # DeepL/百度需要API密钥
    api_key: str = ""
    api_secret: str = ""


class TranslationCache:
    """翻译缓存"""
    
    def __init__(self, max_size: int = 500):
        self._cache: Dict[str, str] = {}
        self._max_size = max_size
    
    def _make_key(self, text: str, source: str, target: str) -> str:
        """生成缓存键"""
        # 简单哈希，避免长键
        return f"{hash(text)}_{source}_{target}"
    
    def get(self, text: str, source: str, target: str) -> Optional[str]:
        """获取缓存"""
        key = self._make_key(text, source, target)
        return self._cache.get(key)
    
    def set(self, text: str, source: str, target: str, translation: str):
        """设置缓存"""
        if len(self._cache) >= self._max_size:
            # 简单清理：删除一半
            keys = list(self._cache.keys())
            for k in keys[:len(keys)//2]:
                del self._cache[k]
        
        key = self._make_key(text, source, target)
        self._cache[key] = translation


class LyricTranslator:
    """歌词翻译器"""
    
    def __init__(self, config: TranslationConfig = None):
        self.config = config or TranslationConfig()
        self._cache = TranslationCache() if self.config.cache_enabled else None
        self._executor = ThreadPoolExecutor(max_workers=2)
        self._translator = None
        self._init_translator()
    
    def _init_translator(self):
        """初始化翻译器"""
        try:
            if self.config.service == TRANSLATE_SERVICE_GOOGLE:
                from deep_translator import GoogleTranslator
                self._translator = GoogleTranslator(
                    source=self.config.source_lang,
                    target=self.config.target_lang
                )
                logger.debug(f"Google翻译器初始化成功: {self.config.source_lang} -> {self.config.target_lang}")
            
            elif self.config.service == TRANSLATE_SERVICE_DEEPL:
                from deep_translator import DeeplTranslator
                if not self.config.api_key:
                    logger.warning("DeepL需要API密钥，回退到Google翻译")
                    return self._init_google_fallback()
                self._translator = DeeplTranslator(
                    api_key=self.config.api_key,
                    source=self.config.source_lang,
                    target=self.config.target_lang,
                    use_free_api=True
                )
            
            elif self.config.service == TRANSLATE_SERVICE_BAIDU:
                from deep_translator import BaiduTranslator
                if not self.config.api_key or not self.config.api_secret:
                    logger.warning("百度翻译需要API密钥和密钥，回退到Google翻译")
                    return self._init_google_fallback()
                self._translator = BaiduTranslator(
                    appid=self.config.api_key,
                    appsecret=self.config.api_secret,
                    source=self.config.source_lang,
                    target=self.config.target_lang
                )
            
        except ImportError as e:
            logger.error(f"翻译库导入失败: {e}，请安装 deep-translator")
            self._translator = None
        except Exception as e:
            logger.error(f"翻译器初始化失败: {e}")
            self._translator = None
    
    def _init_google_fallback(self):
        """回退到Google翻译"""
        try:
            from deep_translator import GoogleTranslator
            self._translator = GoogleTranslator(
                source=self.config.source_lang,
                target=self.config.target_lang
            )
            self.config.service = TRANSLATE_SERVICE_GOOGLE
        except Exception as e:
            logger.error(f"Google翻译回退失败: {e}")
            self._translator = None
    
    def is_available(self) -> bool:
        """检查翻译器是否可用"""
        return self._translator is not None and self.config.enabled
    
    def translate_text(self, text: str) -> Optional[str]:
        """
        翻译单行文本
        
        Args:
            text: 要翻译的文本
        
        Returns:
            翻译结果，失败返回None
        """
        if not self.is_available():
            return None
        
        if not text or not text.strip():
            return ""
        
        text = text.strip()
        
        # 检查缓存
        if self._cache:
            cached = self._cache.get(text, self.config.source_lang, self.config.target_lang)
            if cached:
                return cached
        
        # 检查是否已经是中文
        if self._is_chinese(text):
            return None  # 不翻译中文
        
        # 重试翻译
        for attempt in range(self.config.max_retries):
            try:
                result = self._translator.translate(text)
                if result and result != text:
                    # 缓存结果
                    if self._cache:
                        self._cache.set(text, self.config.source_lang, self.config.target_lang, result)
                    return result
            except Exception as e:
                logger.debug(f"翻译失败 (尝试 {attempt + 1}/{self.config.max_retries}): {e}")
                if attempt < self.config.max_retries - 1:
                    time.sleep(self.config.retry_delay)
                else:
                    logger.warning(f"翻译最终失败: {text[:50]}...")
        
        return None
    
    def translate_lyrics(self, lyrics: List[str], 
                         callback: Callable[[int, str], None] = None) -> Dict[int, str]:
        """
        批量翻译歌词
        
        Args:
            lyrics: 歌词行列表
            callback: 翻译完成回调，参数为 (行索引, 翻译结果)
        
        Returns:
            字典：{行索引: 翻译结果}
        """
        if not self.is_available():
            return {}
        
        results = {}
        
        for i, line in enumerate(lyrics):
            # 跳过空行和元数据行
            if not line or not line.strip():
                continue
            
            # 跳过时间标签行
            if re.match(r'^\[.*\]$', line.strip()) and not re.search(r'\[.*\].+', line):
                continue
            
            # 提取歌词内容（去除时间标签）
            content = re.sub(r'\[\d+:\d+\.\d+\]', '', line).strip()
            if not content:
                continue
            
            # 翻译
            translation = self.translate_text(content)
            if translation:
                results[i] = translation
                if callback:
                    callback(i, translation)
        
        return results
    
    def translate_lyrics_async(self, lyrics: List[str],
                               callback: Callable[[int, str], None] = None) -> Future:
        """
        异步批量翻译歌词
        
        Args:
            lyrics: 歌词行列表
            callback: 翻译完成回调
        
        Returns:
            Future对象
        """
        return self._executor.submit(self.translate_lyrics, lyrics, callback)
    
    def _is_chinese(self, text: str) -> bool:
        """检查文本是否主要是中文"""
        if not text:
            return False
        chinese_count = sum(1 for ch in text if '\u4e00' <= ch <= '\u9fff')
        return chinese_count > len(text) * 0.3  # 30%以上是中文则认为是中文


# 全局翻译器实例
_translator: Optional[LyricTranslator] = None


def init_translator(config: TranslationConfig = None) -> LyricTranslator:
    """初始化全局翻译器"""
    global _translator
    _translator = LyricTranslator(config)
    return _translator


def get_translator() -> Optional[LyricTranslator]:
    """获取全局翻译器"""
    return _translator


def translate_lyric_line(text: str) -> Optional[str]:
    """翻译单行歌词（便捷函数）"""
    if _translator and _translator.is_available():
        return _translator.translate_text(text)
    return None

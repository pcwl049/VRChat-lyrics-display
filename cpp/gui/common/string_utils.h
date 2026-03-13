#pragma once

#include <string>
#include <utility>

// ============================================================================
// 字符串处理工具函数
// ============================================================================

// 计算字符串的 UTF-8 字节数
size_t Utf8ByteLength(const std::wstring& s);

// 截断字符串到指定 UTF-8 字节数
std::wstring TruncateToBytes(const std::wstring& s, size_t maxBytes);

// 极简模式歌曲名截断：中文最多3个字，外语最多6个字符
std::wstring TruncateMinimalTitle(const std::wstring& title);

// 极简模式滚动歌名：获取当前滚动位置的歌名部分
std::wstring GetScrollingMinimalTitle(const std::wstring& title, int scrollOffset);

// 判断歌名是否需要滚动（超过限制）
bool NeedsMinimalScroll(const std::wstring& title);

// 智能截断歌名（处理括号）
// 返回: pair<mainTitle, bracketContent>
std::pair<std::wstring, std::wstring> SmartTruncateTitle(const std::wstring& title, size_t maxMainBytes, size_t maxBracketBytes);

// 提取第一个歌手名（处理多歌手情况）
std::wstring GetFirstArtist(const std::wstring& artist);

// 简单截断字符串
std::wstring TruncateStr(const std::wstring& s, size_t maxLen);

// 格式化时间（秒 -> "m:ss"）
std::wstring FormatTime(double seconds);

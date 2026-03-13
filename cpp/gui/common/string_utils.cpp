#include "string_utils.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>

// 计算字符串的 UTF-8 字节数
size_t Utf8ByteLength(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    return (len > 0) ? (len - 1) : 0;  // len包含null终止符
}

// 截断字符串到指定 UTF-8 字节数
std::wstring TruncateToBytes(const std::wstring& s, size_t maxBytes) {
    size_t currentBytes = Utf8ByteLength(s);
    if (currentBytes <= maxBytes) return s;
    
    // 二分查找截断位置
    size_t left = 0, right = s.length();
    while (left < right) {
        size_t mid = (left + right + 1) / 2;
        size_t bytes = Utf8ByteLength(s.substr(0, mid));
        if (bytes <= maxBytes - 2) {  // 留出".."的空间
            left = mid;
        } else {
            right = mid - 1;
        }
    }
    return s.substr(0, left) + L"..";
}

// 极简模式歌曲名截断：中文最多3个字，外语最多6个字符
std::wstring TruncateMinimalTitle(const std::wstring& title) {
    if (title.empty()) return L"";
    
    int chineseCount = 0;
    int otherCount = 0;
    std::wstring result;
    
    for (wchar_t c : title) {
        // 判断是否为中文字符（CJK范围）
        bool isChinese = (c >= 0x4E00 && c <= 0x9FFF) || 
                         (c >= 0x3400 && c <= 0x4DBF) ||
                         (c >= 0x20000 && c <= 0x2A6DF) ||
                         (c >= 0x2A700 && c <= 0x2B73F);
        
        if (isChinese) {
            if (chineseCount >= 3) continue;  // 中文已满3个
            chineseCount++;
        } else if (c != L' ' && c != L'\t') {
            if (otherCount >= 6) continue;  // 非中文已满6个
            otherCount++;
        }
        
        result += c;
    }
    
    return result;
}

// 判断歌名是否需要滚动（超过限制）
bool NeedsMinimalScroll(const std::wstring& title) {
    if (title.empty()) return false;
    
    int chineseCount = 0;
    int otherCount = 0;
    
    for (wchar_t c : title) {
        bool isChinese = (c >= 0x4E00 && c <= 0x9FFF) || 
                         (c >= 0x3400 && c <= 0x4DBF) ||
                         (c >= 0x20000 && c <= 0x2A6DF) ||
                         (c >= 0x2A700 && c <= 0x2B73F);
        
        if (isChinese) {
            chineseCount++;
        } else if (c != L' ' && c != L'\t') {
            otherCount++;
        }
    }
    
    // 如果中文超过3个或非中文超过6个，需要滚动
    return (chineseCount > 3 || otherCount > 6);
}

// 极简模式滚动歌名：获取当前滚动位置的歌名部分
std::wstring GetScrollingMinimalTitle(const std::wstring& title, int scrollOffset) {
    if (title.empty()) return L"";
    
    // 分类字符：中文和非中文
    struct CharInfo {
        wchar_t c;
        bool isChinese;
    };
    std::vector<CharInfo> chars;
    
    for (wchar_t c : title) {
        bool isChinese = (c >= 0x4E00 && c <= 0x9FFF) || 
                         (c >= 0x3400 && c <= 0x4DBF) ||
                         (c >= 0x20000 && c <= 0x2A6DF) ||
                         (c >= 0x2A700 && c <= 0x2B73F);
        chars.push_back({c, isChinese});
    }
    
    // 确定窗口大小（中文3个，非中文6个）
    // 滚动窗口：显示限制大小的字符
    int maxChinese = 3;
    int maxOther = 6;
    
    // 计算滚动窗口的字符范围
    int startIdx = scrollOffset;
    if (startIdx < 0) startIdx = 0;
    if (startIdx >= (int)chars.size()) startIdx = 0;
    
    int chineseCount = 0;
    int otherCount = 0;
    std::wstring result;
    
    // 从滚动位置开始收集字符，直到达到限制
    for (int i = startIdx; i < (int)chars.size() && (int)result.length() < (int)title.length(); i++) {
        const CharInfo& ci = chars[i];
        
        if (ci.isChinese) {
            if (chineseCount >= maxChinese) break;
            chineseCount++;
        } else if (ci.c != L' ' && ci.c != L'\t') {
            if (otherCount >= maxOther) break;
            otherCount++;
        }
        
        result += ci.c;
    }
    
    // 如果窗口没填满，从头补充（循环滚动）
    if ((chineseCount < maxChinese || otherCount < maxOther) && startIdx > 0) {
        for (int i = 0; i < startIdx && (chineseCount < maxChinese || otherCount < maxOther); i++) {
            const CharInfo& ci = chars[i];
            
            if (ci.isChinese) {
                if (chineseCount >= maxChinese) continue;
                chineseCount++;
            } else if (ci.c != L' ' && ci.c != L'\t') {
                if (otherCount >= maxOther) continue;
                otherCount++;
            }
            
            result += ci.c;
        }
    }
    
    return result.empty() ? TruncateMinimalTitle(title) : result;
}

// 智能截断歌名（处理括号）
std::pair<std::wstring, std::wstring> SmartTruncateTitle(const std::wstring& title, size_t maxMainBytes, size_t maxBracketBytes) {
    std::wstring mainTitle = title;
    std::wstring bracketContent;
    
    // 提取括号内容
    size_t bracketStart = title.find_first_of(L"([({}");
    size_t bracketEnd = title.find_last_of(L")]});");
    
    if (bracketStart != std::wstring::npos && bracketEnd != std::wstring::npos && bracketEnd > bracketStart) {
        mainTitle = title.substr(0, bracketStart);
        // 去除末尾空格
        while (!mainTitle.empty() && (mainTitle.back() == L' ' || mainTitle.back() == L'\t')) {
            mainTitle.pop_back();
        }
        bracketContent = title.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
    }
    
    size_t mainBytes = Utf8ByteLength(mainTitle);
    size_t bracketBytes = Utf8ByteLength(bracketContent);
    
    // 如果主标题很短但括号内容很长，去除括号
    if (!bracketContent.empty() && mainBytes < 12 && bracketBytes > maxBracketBytes) {
        bracketContent.clear();
    }
    // 如果有足够空间，保留括号内容
    else if (!bracketContent.empty() && bracketBytes <= maxBracketBytes) {
        // 保留括号内容
    }
    else {
        bracketContent.clear();
    }
    
    // 截断主标题
    if (mainBytes > maxMainBytes) {
        mainTitle = TruncateToBytes(mainTitle, maxMainBytes);
    }
    
    // 截断括号内容
    if (!bracketContent.empty() && Utf8ByteLength(bracketContent) > maxBracketBytes) {
        bracketContent = TruncateToBytes(bracketContent, maxBracketBytes);
    }
    
    return {mainTitle, bracketContent};
}

// 提取第一个歌手名（处理多歌手情况）
std::wstring GetFirstArtist(const std::wstring& artist) {
    if (artist.empty()) return L"";
    
    // 常见分隔符: , / & 、 , ;
    size_t pos = artist.find_first_of(L",/&、,;");
    if (pos != std::wstring::npos) {
        std::wstring first = artist.substr(0, pos);
        // 去除末尾空格
        while (!first.empty() && (first.back() == L' ' || first.back() == L'\t')) {
            first.pop_back();
        }
        return first;
    }
    return artist;
}

// 简单截断字符串
std::wstring TruncateStr(const std::wstring& s, size_t maxLen) {
    if (s.length() <= maxLen) return s;
    if (maxLen <= 2) return s.substr(0, maxLen);
    return s.substr(0, maxLen - 2) + L"..";
}

// 格式化时间（秒 -> "m:ss"）
std::wstring FormatTime(double seconds) {
    int m = (int)seconds / 60;
    int s = (int)seconds % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%d:%02d", m, s);
    return buf;
}

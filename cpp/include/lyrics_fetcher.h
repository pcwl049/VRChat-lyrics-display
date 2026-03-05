#pragma once

#include "types.h"
#include <string>
#include <functional>

namespace moekoe {

/**
 * 酷狗歌词获取器
 */
class LyricsFetcher {
public:
    LyricsFetcher();
    ~LyricsFetcher();
    
    // 获取歌词 (通过歌曲名和歌手)
    std::vector<LyricLine> fetchLyrics(const std::wstring& song_name, 
                                        const std::wstring& artist);
    
    // 获取歌词 (通过 hash)
    std::vector<LyricLine> fetchLyricsByHash(const std::string& hash);
    
    // 设置缓存目录
    void setCacheDir(const std::string& dir);
    
private:
    // 搜索歌曲
    std::string searchSong(const std::wstring& song_name, const std::wstring& artist);
    
    // 获取歌词
    std::vector<LyricLine> downloadLyrics(const std::string& hash);
    
    // 解析 KRC 歌词
    std::vector<LyricLine> parseKRC(const std::vector<uint8_t>& data);
    
    // 解析 LRC 歌词
    std::vector<LyricLine> parseLRC(const std::string& content);
    
    std::string cache_dir_;
};

} // namespace moekoe

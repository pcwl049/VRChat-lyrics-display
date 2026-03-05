#include "lyrics_fetcher.h"
#include <winhttp.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <zlib.h>

#pragma comment(lib, "winhttp.lib")

namespace moekoe {

LyricsFetcher::LyricsFetcher() = default;
LyricsFetcher::~LyricsFetcher() = default;

void LyricsFetcher::setCacheDir(const std::string& dir) {
    cache_dir_ = dir;
}

std::vector<LyricLine> LyricsFetcher::fetchLyrics(const std::wstring& song_name,
                                                    const std::wstring& artist) {
    // 先搜索歌曲获取 hash
    std::string hash = searchSong(song_name, artist);
    if (hash.empty()) {
        return {};
    }
    
    return fetchLyricsByHash(hash);
}

std::vector<LyricLine> LyricsFetcher::fetchLyricsByHash(const std::string& hash) {
    return downloadLyrics(hash);
}

// HTTP GET 请求辅助函数
static std::string httpGet(const std::wstring& host, const std::wstring& path) {
    std::string result;
    
    HINTERNET session = WinHttpOpen(L"MoeKoeVRChat/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    
    HINTERNET connect = WinHttpConnect(session, host.c_str(), 
                                        INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return result;
    }
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }
    
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(request, NULL)) {
            DWORD size = 0;
            DWORD downloaded = 0;
            do {
                size = 0;
                if (!WinHttpQueryDataAvailable(request, &size)) break;
                if (size == 0) break;
                
                std::vector<char> buffer(size + 1);
                if (!WinHttpReadData(request, buffer.data(), size, &downloaded)) break;
                
                result.append(buffer.data(), downloaded);
            } while (size > 0);
        }
    }
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    return result;
}

std::string LyricsFetcher::searchSong(const std::wstring& song_name,
                                       const std::wstring& artist) {
    // 构建搜索 URL
    std::wostringstream path;
    path << L"/api/v1/search/music?keyword="
         << song_name << L" " << artist
         << L"&page=1&pagesize=1";
    
    std::string response = httpGet(L"mobileservice.kugou.com", path.str());
    
    // 解析 JSON 获取第一个结果的 hash
    // 简化处理，实际应使用 nlohmann/json
    size_t hash_pos = response.find("\"hash\":\"");
    if (hash_pos != std::string::npos) {
        size_t start = hash_pos + 8;
        size_t end = response.find("\"", start);
        if (end != std::string::npos) {
            return response.substr(start, end - start);
        }
    }
    
    return "";
}

std::vector<LyricLine> LyricsFetcher::downloadLyrics(const std::string& hash) {
    // 获取歌词
    std::wostringstream path;
    path << L"/yy/index.php?r=play/getdata&hash=" 
         << std::wstring(hash.begin(), hash.end());
    
    std::string response = httpGet(L"www.kugou.com", path.str());
    
    // 解析歌词
    // 简化处理，实际需要完整解析
    std::vector<LyricLine> lines;
    
    // TODO: 完整的歌词解析逻辑
    
    return lines;
}

std::vector<LyricLine> LyricsFetcher::parseKRC(const std::vector<uint8_t>& data) {
    std::vector<LyricLine> lines;
    
    if (data.size() < 4) return lines;
    
    // KRC 格式：前4字节是 header，后面是 zlib 压缩的数据
    // 解压需要 zlib
    
    return lines;
}

std::vector<LyricLine> LyricsFetcher::parseLRC(const std::string& content) {
    std::vector<LyricLine> lines;
    
    // LRC 格式：[mm:ss.xx]歌词文本
    std::regex lrc_pattern(R"(\[(\d+):(\d+)(?:\.(\d+))?\](.*))");
    std::sregex_iterator it(content.begin(), content.end(), lrc_pattern);
    std::sregex_iterator end;
    
    for (; it != end; ++it) {
        std::smatch match = *it;
        if (match.size() >= 4) {
            LyricLine line;
            int min = std::stoi(match[1].str());
            int sec = std::stoi(match[2].str());
            int ms = match[3].matched ? std::stoi(match[3].str()) : 0;
            line.time = min * 60 + sec + ms / 1000.0;
            
            // UTF-8 转宽字符
            std::string text = match[4].str();
            int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            line.text.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, 
                               &line.text[0], size);
            
            lines.push_back(line);
        }
    }
    
    // 按时间排序
    std::sort(lines.begin(), lines.end(), 
              [](const LyricLine& a, const LyricLine& b) {
                  return a.time < b.time;
              });
    
    return lines;
}

} // namespace moekoe

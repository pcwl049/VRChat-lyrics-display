#include "lyrics_search.h"
#include "../common/logger.h"
#include "../ui/draw_helpers.h"
#include <winhttp.h>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")

// Search lyrics from QQ Music API
// API: 搜索 -> https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=keyword&format=json
//      歌词 -> https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=xxx
std::vector<moekoe::LyricLine> SearchLyricsForQQMusic(const std::wstring& title, const std::wstring& artist) {
    std::vector<moekoe::LyricLine> lyrics;
    if (title.empty()) return lyrics;
    
    std::string dbgMsg = "Searching lyrics for: " + WstringToUtf8(title) + " - " + WstringToUtf8(artist);
    LOG_INFO("QQ Music", "%s", dbgMsg.c_str());
    
    // Build search query
    std::string query = WstringToUtf8(title);
    if (!artist.empty()) {
        query += " ";
        query += WstringToUtf8(artist);
    }
    
    // URL encode
    std::string encodedQuery;
    for (char c : query) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedQuery += c;
        } else {
            char buf[4];
            sprintf_s(buf, "%%%02X", (unsigned char)c);
            encodedQuery += buf;
        }
    }
    
    // === Step 1: Search QQ Music API ===
    // API: https://c.y.qq.com/soso/fcgi-bin/client_search_cp?w=keyword&format=json
    std::wstring whost = L"c.y.qq.com";
    std::wstring wpath = L"/soso/fcgi-bin/client_search_cp?w=" + Utf8ToWstring(encodedQuery) + L"&format=json&p=1&n=5";
    
    HINTERNET hSession = WinHttpOpen(L"VRCLyricsDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return lyrics;
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return lyrics; }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, L"https://y.qq.com", WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return lyrics; }
    
    std::wstring headers = L"Referer: https://y.qq.com\r\nCookie: guid=1234567890\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string searchResp;
    DWORD dwSize = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            searchResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log search response (first 500 chars)
    std::string respDbg = "QQ Search response: " + searchResp.substr(0, 500);
    LOG_INFO("QQ Music", "%s", respDbg.c_str());
    
    // Extract songmid from search result
    // Format: "song":{"list":[{"songmid":"xxx",...}]}
    size_t listPos = searchResp.find("\"list\":[");
    if (listPos == std::string::npos) {
        LOG_INFO("QQ Music", "No song list found");
        return lyrics;
    }
    
    // Find first songmid
    size_t songmidPos = searchResp.find("\"songmid\":\"", listPos);
    if (songmidPos == std::string::npos) {
        LOG_INFO("QQ Music", "No songmid found");
        return lyrics;
    }
    
    size_t midStart = songmidPos + 11;
    size_t midEnd = searchResp.find("\"", midStart);
    if (midEnd == std::string::npos) return lyrics;
    
    std::string songmid = searchResp.substr(midStart, midEnd - midStart);
    LOG_INFO("QQ Music", "Found songmid: %s", songmid.c_str());
    
    // === Step 2: Fetch Lyrics ===
    // API: https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=xxx
    wpath = L"/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" + Utf8ToWstring(songmid) + L"&format=json";
    
    hSession = WinHttpOpen(L"VRCLyricsDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return lyrics;
    
    hConnect = WinHttpConnect(hSession, L"c.y.qq.com", INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return lyrics; }
    
    hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(), NULL, L"https://y.qq.com", WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return lyrics; }
    
    headers = L"Referer: https://y.qq.com\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string lyricsResp;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            lyricsResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log lyrics response
    std::string lrcDbg = "QQ Lyrics response: " + lyricsResp.substr(0, 300);
    LOG_INFO("QQ Music", "%s", lrcDbg.c_str());
    
    // Extract lyric - format: {"lyric":"BASE64_ENCODED_LRC"}
    size_t lyricPos = lyricsResp.find("\"lyric\":\"");
    if (lyricPos == std::string::npos) {
        LOG_INFO("QQ Music", "No lyric field found");
        return lyrics;
    }
    
    size_t lrcStart = lyricPos + 9;
    size_t lrcEnd = lyricsResp.find("\"", lrcStart);
    if (lrcEnd == std::string::npos) return lyrics;
    
    std::string lrcBase64 = lyricsResp.substr(lrcStart, lrcEnd - lrcStart);
    LOG_INFO("QQ Music", "Base64 length: %zu", lrcBase64.length());
    
    // Base64 decode
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string unescaped;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;
    
    int val = 0, valb = -8;
    for (unsigned char c : lrcBase64) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            unescaped.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    // Debug: log decoded content (first 200 chars)
    std::string decodedDbg = "Decoded lyric: " + unescaped.substr(0, 200);
    LOG_INFO("QQ Music", "%s", decodedDbg.c_str());
    
    // Parse LRC format
    size_t pos = 0;
    while ((pos = unescaped.find('[', pos)) != std::string::npos) {
        size_t endBracket = unescaped.find(']', pos);
        if (endBracket == std::string::npos) { pos++; continue; }
        
        std::string timeStr = unescaped.substr(pos + 1, endBracket - pos - 1);
        
        // Skip metadata tags like [ti:], [ar:], etc.
        if (timeStr.find(':') != std::string::npos && !isdigit((unsigned char)timeStr[0])) {
            pos = endBracket + 1;
            continue;
        }
        
        // Parse time: mm:ss.xx or mm:ss:xxx
        int min = 0, sec = 0, ms = 0;
        if (sscanf_s(timeStr.c_str(), "%d:%d.%d", &min, &sec, &ms) >= 2 ||
            sscanf_s(timeStr.c_str(), "%d:%d:%d", &min, &sec, &ms) >= 2) {
            double time = min * 60.0 + sec + ms / 1000.0;
            
            // Find lyric text
            size_t nextBracket = unescaped.find('[', endBracket);
            std::string text;
            if (nextBracket != std::string::npos) {
                text = unescaped.substr(endBracket + 1, nextBracket - endBracket - 1);
            } else {
                text = unescaped.substr(endBracket + 1);
            }
            
            // Trim
            size_t start = text.find_first_not_of(" \t\r\n");
            size_t end = text.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos && start <= end) {
                text = text.substr(start, end - start + 1);
            } else {
                text = "";
            }
            
            if (!text.empty()) {
                moekoe::LyricLine line;
                line.startTime = (int)(time * 1000);  // Convert to ms
                line.text = Utf8ToWstring(text);
                lyrics.push_back(line);
            }
        }
        
        pos = endBracket + 1;
    }
    
    // Sort by time
    std::sort(lyrics.begin(), lyrics.end(), [](const moekoe::LyricLine& a, const moekoe::LyricLine& b) {
        return a.startTime < b.startTime;
    });
    
    LOG_INFO("QQ Music", "Parsed %zu lyric lines", lyrics.size());
    
    return lyrics;
}

// Search lyrics from Qishui Music (汽水音乐) API
// API: 搜索 -> https://api.qishui.com/luna/pc/search/track?q=keyword
//      歌词 -> https://music.douyin.com/qishui/share/track?track_id=xxx
std::vector<moekoe::LyricLine> SearchLyricsForQishuiMusic(const std::wstring& title, const std::wstring& artist) {
    std::vector<moekoe::LyricLine> lyrics;
    if (title.empty()) return lyrics;
    
    std::string dbgMsg = "[Qishui] Searching lyrics for: " + WstringToUtf8(title) + " - " + WstringToUtf8(artist);
    LOG_INFO("Qishui Music", "%s", dbgMsg.c_str());
    
    // Build search query
    std::string query = WstringToUtf8(title);
    if (!artist.empty()) {
        query += " ";
        query += WstringToUtf8(artist);
    }
    
    // URL encode
    std::string encodedQuery;
    for (char c : query) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encodedQuery += c;
        } else {
            char buf[4];
            sprintf_s(buf, "%%%02X", (unsigned char)c);
            encodedQuery += buf;
        }
    }
    
    // === Step 1: Search Qishui Music API ===
    // Build search URL with required parameters
    std::string searchPath = "/luna/pc/search/track?";
    searchPath += "aid=386088&app_name=luna_pc&region=cn&geo_region=cn&os_region=cn";
    searchPath += "&device_id=1088932190113307&iid=2332504177791808";
    searchPath += "&version_name=3.0.0&version_code=30000000&channel=official";
    searchPath += "&ac=wifi&device_platform=windows&device_type=Windows";
    searchPath += "&q=" + encodedQuery;
    searchPath += "&cursor=0&search_method=input&search_scene=";
    
    LOG_INFO("Qishui Music", "Search path: %s", searchPath.c_str());
    
    // 使用简单的 WinHTTP 配置
    HINTERNET hSession = WinHttpOpen(L"VRCLyricsDisplay", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) {
        LOG_INFO("Qishui Music", "WinHttpOpen failed: %d", GetLastError());
        return lyrics;
    }
    
    // 设置超时
    DWORD timeout = 15000;  // 15秒
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    
    // 忽略 SSL 证书错误（开发用）
    DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    
    // 使用 HTTPS
    HINTERNET hConnect = WinHttpConnect(hSession, L"api.qishui.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        LOG_INFO("Qishui Music", "WinHttpConnect failed: %d", GetLastError());
        WinHttpCloseHandle(hSession);
        return lyrics;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", Utf8ToWstring(searchPath).c_str(), 
                                             NULL, NULL, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        LOG_INFO("Qishui Music", "WinHttpOpenRequest failed: %d", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return lyrics;
    }
    
    // 也设置请求级别的 SSL 忽略
    WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    
    // 添加必要的 headers
    std::wstring headers = L"Accept: application/json, text/plain, */*\r\n";
    headers += L"Accept-Language: zh-CN,zh;q=0.9,en;q=0.8\r\n";
    headers += L"Referer: https://www.douyin.com/\r\n";
    headers += L"Origin: https://www.douyin.com\r\n";
    
    BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent) {
        DWORD err = GetLastError();
        LOG_INFO("Qishui Music", "WinHttpSendRequest failed: %d", err);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return lyrics;
    }
    
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    BOOL received = WinHttpReceiveResponse(hRequest, NULL);
    
    if (received) {
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                           WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        LOG_INFO("Qishui Music", "Search HTTP status: %d", statusCode);
    } else {
        DWORD err = GetLastError();
        LOG_INFO("Qishui Music", "WinHttpReceiveResponse failed: %d", err);
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return lyrics;
    }
    
    std::string searchResp;
    DWORD dwSize = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            searchResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log search response (first 500 chars)
    if (searchResp.empty()) {
        LOG_INFO("Qishui Music", "Search response is empty");
        return lyrics;
    }
    std::string respDbg = "[Qishui] Search response: " + searchResp.substr(0, (std::min)((size_t)500, searchResp.length()));
    LOG_INFO("Qishui Music", "%s", respDbg.c_str());
    
    // Extract track_id from search result
    // Format: "result_groups":[{"data":[{"entity":{"track":{"id":123456,...}}}]}]
    size_t dataPos = searchResp.find("\"data\":[");
    if (dataPos == std::string::npos) {
        LOG_INFO("Qishui Music", "No data found in search response");
        return lyrics;
    }
    
    // Find first track id
    size_t trackIdPos = searchResp.find("\"id\":", dataPos);
    if (trackIdPos == std::string::npos) {
        LOG_INFO("Qishui Music", "No track id found");
        return lyrics;
    }
    
    // Parse track_id - can be number or string format
    // Number format: "id":123456
    // String format: "id":"123456"
    size_t idStart = trackIdPos + 5;  // skip "id":
    // Skip whitespace
    while (idStart < searchResp.length() && (searchResp[idStart] == ' ' || searchResp[idStart] == '\t')) {
        idStart++;
    }
    
    // Check if it's a quoted string
    bool isQuoted = false;
    if (idStart < searchResp.length() && searchResp[idStart] == '"') {
        isQuoted = true;
        idStart++;  // skip opening quote
    }
    
    // Find end of ID (number digits)
    size_t idEnd = idStart;
    while (idEnd < searchResp.length() && isdigit((unsigned char)searchResp[idEnd])) {
        idEnd++;
    }
    
    if (idEnd <= idStart) {
        LOG_INFO("Qishui Music", "Failed to parse track id");
        return lyrics;
    }
    
    std::string trackId = searchResp.substr(idStart, idEnd - idStart);
    LOG_INFO("Qishui Music", "Found track_id: %s", trackId.c_str());
    
    // === Step 2: Fetch Lyrics ===
    // API: https://music.douyin.com/qishui/share/track?track_id=xxx
    std::wstring lyricPath = L"/qishui/share/track?track_id=" + Utf8ToWstring(trackId);
    
    hSession = WinHttpOpen(L"VRCLyricsDisplay/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return lyrics;
    
    hConnect = WinHttpConnect(hSession, L"music.douyin.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return lyrics; }
    
    hRequest = WinHttpOpenRequest(hConnect, L"GET", lyricPath.c_str(), NULL, 
                                   L"https://music.douyin.com", WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return lyrics; }
    
    headers = L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string lyricsResp;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            lyricsResp += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Debug: log lyrics response (first 1000 chars and search for key patterns)
    std::string lrcDbg = "[Qishui] Lyrics response length: " + std::to_string(lyricsResp.length());
    LOG_INFO("Qishui Music", "%s", lrcDbg.c_str());
    
    // Look for _ROUTER_DATA which contains the actual lyrics
    size_t routerPos = lyricsResp.find("_ROUTER_DATA");
    if (routerPos != std::string::npos) {
        LOG_INFO("Qishui Music", "Found _ROUTER_DATA at position %zu", routerPos);
    }
    
    // Parse lyrics from HTML/JSON response
    // Format: "sentences":[{"startMs":0,"endMs":5000,"words":[{"text":"xxx","startMs":0}]}]
    // Also look for _ROUTER_DATA = {...}
    size_t sentencesPos = lyricsResp.find("\"sentences\":[");
    if (sentencesPos == std::string::npos) {
        // Try alternative format: audioWithLyricsOption
        sentencesPos = lyricsResp.find("\"audioWithLyricsOption\"");
        if (sentencesPos != std::string::npos) {
            LOG_INFO("Qishui Music", "Found audioWithLyricsOption, searching for sentences");
            sentencesPos = lyricsResp.find("\"sentences\":[", sentencesPos);
        }
    }
    
    if (sentencesPos == std::string::npos) {
        LOG_INFO("Qishui Music", "No sentences found in lyrics response");
        // Log some content around potential lyrics markers
        size_t lyricPos = lyricsResp.find("\"lyric");
        if (lyricPos != std::string::npos) {
            std::string sample = lyricsResp.substr(lyricPos, (std::min)((size_t)300, lyricsResp.length() - lyricPos));
            LOG_INFO("Qishui Music", "Found 'lyric' marker: %s", sample.c_str());
        }
        return lyrics;
    }
    
    // Log sentences content for debugging
    std::string sentencesSample = lyricsResp.substr(sentencesPos, (std::min)((size_t)500, lyricsResp.length() - sentencesPos));
    LOG_INFO("Qishui Music", "Sentences sample: %s", sentencesSample.c_str());
    
    // Parse each sentence using bracket counting for proper JSON parsing
    size_t pos = sentencesPos + 13;  // skip "sentences":[
    
    while (pos < lyricsResp.length()) {
        // Skip whitespace and commas
        while (pos < lyricsResp.length() && (lyricsResp[pos] == ' ' || lyricsResp[pos] == '\t' || lyricsResp[pos] == ',' || lyricsResp[pos] == '\n' || lyricsResp[pos] == '\r')) {
            pos++;
        }
        
        // Check for end of array
        if (pos >= lyricsResp.length() || lyricsResp[pos] == ']') break;
        
        // Find start of sentence object
        if (lyricsResp[pos] != '{') {
            pos++;
            continue;
        }
        
        // Find end of sentence object using bracket counting
        size_t objStart = pos;
        int depth = 0;
        bool inString = false;
        while (pos < lyricsResp.length()) {
            char c = lyricsResp[pos];
            if (c == '"' && (pos == 0 || lyricsResp[pos-1] != '\\')) {
                inString = !inString;
            } else if (!inString) {
                if (c == '{') depth++;
                else if (c == '}') {
                    depth--;
                    if (depth == 0) {
                        pos++;  // include the closing }
                        break;
                    }
                }
            }
            pos++;
        }
        
        std::string sentenceObj = lyricsResp.substr(objStart, pos - objStart);
        
        // Extract startMs
        int startMs = -1;
        size_t startMsPos = sentenceObj.find("\"startMs\":");
        if (startMsPos != std::string::npos) {
            size_t msStart = startMsPos + 10;
            // Skip whitespace
            while (msStart < sentenceObj.length() && (sentenceObj[msStart] == ' ' || sentenceObj[msStart] == '\t')) {
                msStart++;
            }
            startMs = atoi(sentenceObj.c_str() + msStart);
        }
        
        // Extract sentence-level text (not from words array)
        // Find "text":" that appears BEFORE "words":[
        std::string sentenceText;
        size_t wordsArrayPos = sentenceObj.find("\"words\":[");
        
        size_t textSearchPos = 0;
        while (textSearchPos < sentenceObj.length()) {
            size_t textPos = sentenceObj.find("\"text\":\"", textSearchPos);
            if (textPos == std::string::npos) break;
            
            // Check if this text is inside words array
            if (wordsArrayPos != std::string::npos && textPos > wordsArrayPos) {
                // This text is inside words array, skip it
                textSearchPos = textPos + 8;
                continue;
            }
            
            // This is the sentence-level text
            size_t textStart = textPos + 8;
            size_t textEnd = sentenceObj.find("\"", textStart);
            if (textEnd != std::string::npos) {
                sentenceText = sentenceObj.substr(textStart, textEnd - textStart);
                break;
            }
            break;
        }
        
        // Skip metadata lines (作曲, 作词, etc.)
        if (!sentenceText.empty() && startMs >= 0) {
            // Skip if it looks like metadata
            bool isMetadata = (sentenceText.find("作曲") != std::string::npos ||
                               sentenceText.find("作词") != std::string::npos ||
                               sentenceText.find("编曲") != std::string::npos ||
                               sentenceText.find("浣滄洸") != std::string::npos ||  // UTF-8 作曲
                               sentenceText.find("浣滆瘝") != std::string::npos ||  // UTF-8 作词
                               sentenceText.find("缂栨洸") != std::string::npos);   // UTF-8 编曲
            
            if (!isMetadata) {
                moekoe::LyricLine line;
                line.startTime = startMs;
                line.text = Utf8ToWstring(sentenceText);
                lyrics.push_back(line);
            }
        }
    }
    
    // Sort by time
    std::sort(lyrics.begin(), lyrics.end(), [](const moekoe::LyricLine& a, const moekoe::LyricLine& b) {
        return a.startTime < b.startTime;
    });
    
    LOG_INFO("Qishui Music", "Parsed %zu lyric lines", lyrics.size());
    
    // Log first few lyrics for debugging
    for (size_t i = 0; i < (std::min)((size_t)5, lyrics.size()); i++) {
        LOG_INFO("Qishui Music", "Lyric[%zu]: time=%d ms, text='%s'", 
                 i, lyrics[i].startTime, WstringToUtf8(lyrics[i].text).c_str());
    }

    return lyrics;
}


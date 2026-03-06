// netease_ws.h - Netease Cloud Music CDP Client
// Based on NcmVRChatSyncV2 reference implementation
#pragma once

#include "moekoe_ws.h"
#include <winhttp.h>
#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace moekoe {

class NeteaseWS {
public:
    using Callback = std::function<void(const SongInfo&)>;
    
    NeteaseWS(int port = 9222);
    ~NeteaseWS();
    
    void setCallback(Callback cb) { callback_ = cb; }
    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }
    const SongInfo& getSongInfo() const { return songInfo_; }
    
private:
    void run();
    
    // WebSocket communication
    std::string receiveMessage();
    std::string sendMessage(const std::string& msg);
    
    // CDP communication
    bool attachToOrpheusPage();
    std::string evaluateScript(const std::string& script);
    std::string parseEvaluateResult(const std::string& result, int expectedId);
    
    void parseState(const std::string& json);
    std::wstring Utf8ToWstring(const std::string& utf8);
    std::string parseResultValue(const std::string& result, int expectedId);
    
    // Lyrics fetching
    void fetchLyrics(const std::string& playId);
    std::string httpGet(const std::string& url);
    void parseLrcLyrics(const std::string& lrc, const std::string& tLrc = "");
    
    int port_;
    int msgId_ = 1;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;
    Callback callback_;
    SongInfo songInfo_;
    
    HINTERNET hSession_ = nullptr;
    HINTERNET hConnect_ = nullptr;
    HINTERNET hRequest_ = nullptr;
    HINTERNET hWebSocket_ = nullptr;
    std::string sessionId_;
    std::string currentPlayId_;  // Current song ID for lyrics API
    std::string lastFetchedPlayId_;  // To avoid refetching same song
};

} // namespace moekoe
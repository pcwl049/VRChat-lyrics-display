#pragma once

#include "types.h"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

namespace moekoe {

/**
 * MoeKoeMusic WebSocket 客户端
 */
class MoeKoeClient {
public:
    using SongChangeCallback = std::function<void(const SongInfo&)>;
    using StateChangeCallback = std::function<void(PlaybackState)>;
    
    MoeKoeClient(const std::string& host = "127.0.0.1", int port = 6520);
    ~MoeKoeClient();
    
    // 连接/断开
    bool connect();
    void disconnect();
    
    // 状态
    bool isConnected() const;
    const SongInfo& getSongInfo() const;
    PlaybackState getPlaybackState() const;
    const std::vector<LyricLine>& getLyrics() const;
    
    // 回调
    void setSongChangeCallback(SongChangeCallback callback);
    void setStateChangeCallback(StateChangeCallback callback);
    
private:
    void receiveLoop();
    void handleMessage(const std::string& message);
    void parseSongInfo(const std::string& json);
    void parseLyrics(const std::string& json);
    void parsePlaybackState(const std::string& json);
    
    std::string host_;
    int port_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;
    std::mutex mutex_;
    
    SongInfo song_info_;
    PlaybackState playback_state_ = PlaybackState::Unknown;
    std::vector<LyricLine> lyrics_;
    
    SongChangeCallback song_callback_;
    StateChangeCallback state_callback_;
    
    // Windows WebSocket handle
    void* websocket_ = nullptr;
};

} // namespace moekoe

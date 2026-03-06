#pragma once

#include "music_source.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

namespace moekoe {

/**
 * MoeKoeMusic WebSocket 客户端
 */
class MoeKoeClient : public MusicSource {
public:
    MoeKoeClient(const std::string& host = "127.0.0.1", int port = 6520);
    ~MoeKoeClient() override;
    
    // MusicSource 接口实现
    MusicPlatform getPlatform() const override { return MusicPlatform::MoeKoeMusic; }
    std::wstring getPlatformName() const override { return L"MoeKoeMusic"; }
    
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    
    const SongInfo& getSongInfo() const override;
    PlaybackState getPlaybackState() const override;
    const std::vector<LyricLine>& getLyrics() const override;
    
    // 保留旧接口兼容性
    using SongChangeCallback = MusicSource::SongChangeCallback;
    using StateChangeCallback = MusicSource::StateChangeCallback;
    
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
    
    // Windows WebSocket handle
    void* websocket_ = nullptr;
};

} // namespace moekoe

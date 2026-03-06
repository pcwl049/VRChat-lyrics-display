#pragma once

#include "music_source.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>

namespace moekoe {

/**
 * 网易云音乐客户端
 * 使用 Chrome DevTools Protocol (CDP) 连接网易云客户端
 * 需要网易云客户端以 --remote-debugging-port=9222 启动
 */
class NeteaseClient : public MusicSource {
public:
    NeteaseClient(int port = 9222);
    ~NeteaseClient() override;
    
    // MusicSource 接口实现
    MusicPlatform getPlatform() const override { return MusicPlatform::NeteaseCloudMusic; }
    std::wstring getPlatformName() const override { return L"\x7F51\x6613\x4E91\x97F3\x4E50"; }
    
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    
    const SongInfo& getSongInfo() const override;
    PlaybackState getPlaybackState() const override;
    const std::vector<LyricLine>& getLyrics() const override;
    
private:
    // CDP 通信
    bool connectToDevTools();
    bool attachToOrpheusPage();
    bool injectMonitorScript();
    void receiveLoop();
    void pollLoop();
    
    // CDP 命令
    int sendCommand(const std::string& method, const std::string& params = "{}");
    std::string evaluateScript(const std::string& script);
    
    // 数据获取
    bool fetchPlayState();
    bool fetchSongInfo();
    bool fetchLyrics(const std::string& songId);
    
    // HTTP 辅助
    std::string httpGet(const std::string& path);
    
    int port_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread receive_thread_;
    std::thread poll_thread_;
    std::mutex mutex_;
    
    // WebSocket
    void* websocket_ = nullptr;
    std::string session_id_;
    int msg_id_ = 1;
    std::map<int, std::string> pending_responses_;
    std::condition_variable response_cv_;
    
    // 状态
    SongInfo song_info_;
    PlaybackState playback_state_ = PlaybackState::Unknown;
    std::vector<LyricLine> lyrics_;
    
    // 当前播放信息
    std::string current_song_id_;
    double current_time_ = 0;
    bool is_playing_ = false;
};

} // namespace moekoe

#pragma once

#include "types.h"
#include <functional>
#include <memory>
#include <string>

namespace moekoe {

/**
 * 音乐平台类型
 */
enum class MusicPlatform {
    MoeKoeMusic,
    NeteaseCloudMusic,
    QQMusic,
    Unknown
};

/**
 * 音乐源抽象基类
 * 所有音乐平台客户端都需要继承此接口
 */
class MusicSource {
public:
    using SongChangeCallback = std::function<void(const SongInfo&)>;
    using StateChangeCallback = std::function<void(PlaybackState)>;
    
    virtual ~MusicSource() = default;
    
    // 平台类型
    virtual MusicPlatform getPlatform() const = 0;
    virtual std::wstring getPlatformName() const = 0;
    
    // 连接/断开
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    
    // 获取当前状态
    virtual const SongInfo& getSongInfo() const = 0;
    virtual PlaybackState getPlaybackState() const = 0;
    virtual const std::vector<LyricLine>& getLyrics() const = 0;
    
    // 回调设置
    void setSongChangeCallback(SongChangeCallback callback) { song_callback_ = std::move(callback); }
    void setStateChangeCallback(StateChangeCallback callback) { state_callback_ = std::move(callback); }
    
protected:
    SongChangeCallback song_callback_;
    StateChangeCallback state_callback_;
    
    // 通知回调
    void notifySongChange(const SongInfo& info) {
        if (song_callback_) song_callback_(info);
    }
    
    void notifyStateChange(PlaybackState state) {
        if (state_callback_) state_callback_(state);
    }
};

/**
 * 创建音乐源工厂函数
 */
std::unique_ptr<MusicSource> createMusicSource(MusicPlatform platform);

} // namespace moekoe

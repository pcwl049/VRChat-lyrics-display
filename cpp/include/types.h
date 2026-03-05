#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace moekoe {

/**
 * 播放状态枚举
 */
enum class PlaybackState {
    Unknown = 0,
    Stopped = 1,
    Playing = 2,
    Paused = 3
};

/**
 * 歌曲信息
 */
struct SongInfo {
    std::wstring title;         // 歌曲名
    std::wstring artist;        // 歌手
    std::wstring album;         // 专辑
    std::string hash;           // 歌曲hash (用于酷狗API)
    double duration = 0.0;      // 总时长 (秒)
};

/**
 * 歌词行
 */
struct LyricLine {
    double time = 0.0;          // 时间戳 (秒)
    std::wstring text;          // 歌词文本
    std::wstring translation;   // 翻译
    std::vector<std::pair<double, char32_t>> chars; // 逐字数据
};

/**
 * 媒体信息 (合并数据)
 */
struct MediaInfo {
    // 来自 WebSocket (MoeKoeMusic)
    SongInfo song;
    
    // 来自 GSMTC (精确进度)
    double current_time = 0.0;  // 当前播放时间
    double total_time = 0.0;    // 总时长
    PlaybackState state = PlaybackState::Unknown;
    
    // 歌词
    std::vector<LyricLine> lyrics;
    int current_lyric_index = -1;
    
    // 状态
    bool ws_connected = false;
    bool gsmtc_connected = false;
    
    bool isConnected() const {
        return ws_connected || gsmtc_connected;
    }
    
    bool isPlaying() const {
        return state == PlaybackState::Playing;
    }
    
    bool isPaused() const {
        return state == PlaybackState::Paused;
    }
    
    double progressPercent() const {
        double total = total_time > 0 ? total_time : song.duration;
        if (total > 0) {
            return std::min(1.0, current_time / total);
        }
        return 0.0;
    }
};

/**
 * 配置
 */
struct Config {
    // OSC 设置
    std::string osc_ip = "127.0.0.1";
    int osc_port = 9000;
    
    // MoeKoeMusic 设置
    std::string moekoe_host = "127.0.0.1";
    int moekoe_port = 6520;
    
    // 显示设置
    std::string display_mode = "full";
    bool show_lyrics = true;
    double lyric_advance = 0.0;  // 歌词提前量 (秒)
    
    // 发送间隔
    double min_send_interval = 3.0;
    double resend_interval = 12.0;
    
    // 文件路径
    std::string config_path = "config.json";
    std::string calibration_path = "calibration.json";
    std::string cache_dir = "lyrics_cache";
};

} // namespace moekoe

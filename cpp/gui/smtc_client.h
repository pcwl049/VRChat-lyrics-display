// smtc_client.h - System Media Transport Controls Client
// Supports QQ Music, Spotify, Apple Music, and other SMTC-enabled players

#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <windows.h>

namespace smtc {

// Media information from SMTC
struct MediaInfo {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring appId;          // Source app ID (e.g., "QQMusic.exe")
    std::wstring appName;        // Display name (e.g., "QQ音乐")
    double duration = 0;         // Total duration in seconds
    double position = 0;         // Current position in seconds
    double playbackRate = 1.0;   // Playback speed
    bool isPlaying = false;
    bool hasData = false;
    int trackNumber = 0;
};

// Playback status enum matching WinRT
enum class PlaybackStatus {
    Unknown = 0,
    Closed = 1,
    Opened = 2,
    Paused = 3,
    Playing = 4,
    Stopped = 5
};

// SMTC Client class
class SMTCClient {
public:
    using Callback = std::function<void(const MediaInfo&)>;
    
    SMTCClient();
    ~SMTCClient();
    
    // Set callback for media updates
    void setCallback(Callback cb) { callback_ = cb; }
    
    // Start monitoring (runs in background thread)
    bool start();
    
    // Stop monitoring
    void stop();
    
    // Check if monitoring is active
    bool isRunning() const { return running_; }
    
    // Get current media info (synchronous)
    MediaInfo getCurrentMedia();
    
    // Filter by app ID (optional, empty = all apps)
    void setAppFilter(const std::wstring& appId) { appFilter_ = appId; }
    
    // Check if WinRT is available (Windows 10+)
    static bool isAvailable();

private:
    void run();
    void processMediaUpdate();
    
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread thread_;
    Callback callback_;
    MediaInfo currentMedia_;
    std::wstring appFilter_;  // Empty = accept all, or filter by app
    CRITICAL_SECTION cs_;
    
    // WinRT state (void* to avoid WinRT headers in header file)
    void* manager_ = nullptr;  // GlobalSystemMediaTransportControlsSessionManager
    void* session_ = nullptr;  // GlobalSystemMediaTransportControlsSession
};

// Helper function to get app display name from app ID
std::wstring GetAppDisplayName(const std::wstring& appId);

// Known music app IDs
namespace Apps {
    const std::wstring QQMusic = L"QQMusic.exe";
    const std::wstring QishuiMusic = L"SodaMusic.exe";  // 汽水音乐 (进程名为 SodaMusic)
    const std::wstring Spotify = L"Spotify.exe";
    const std::wstring AppleMusic = L"AppleMusic.exe";
    const std::wstring NeteaseUWP = L"Netease.CloudMusic.UWP";
    const std::wstring Kugou = L"Kugou.exe";
    const std::wstring Groove = L"Microsoft.ZuneMusic";
}

} // namespace smtc

// smtc_client.cpp - System Media Transport Controls Client Implementation
// Uses C++/WinRT to access Windows.Media.Control APIs

#include "smtc_client.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.Streams.h>
#include <chrono>
#include <ctime>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::ApplicationModel;

namespace smtc {

// Convert WinRT TimeSpan to seconds
static double TimeSpanToSeconds(TimeSpan ts) {
    return std::chrono::duration<double>(ts).count();
}

// Get display name from app user model ID
std::wstring GetAppDisplayName(const std::wstring& appId) {
    // Extract filename from path or app ID
    size_t pos = appId.find_last_of(L"\\");
    if (pos != std::wstring::npos) {
        std::wstring name = appId.substr(pos + 1);
        // Remove .exe extension if present
        size_t extPos = name.find_last_of(L".");
        if (extPos != std::wstring::npos) {
            name = name.substr(0, extPos);
        }
        return name;
    }
    return appId;
}

// Check if WinRT SMTC is available
bool SMTCClient::isAvailable() {
    try {
        init_apartment(apartment_type::single_threaded);
        // Try to request the manager - this will fail on older Windows
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        return manager != nullptr;
    } catch (...) {
        return false;
    }
}

SMTCClient::SMTCClient() {
    InitializeCriticalSection(&cs_);
}

SMTCClient::~SMTCClient() {
    stop();
    DeleteCriticalSection(&cs_);
}

bool SMTCClient::start() {
    if (running_) return true;
    
    try {
        // Initialize WinRT apartment
        init_apartment(apartment_type::single_threaded);
        initialized_ = true;
    } catch (...) {
        return false;
    }
    
    running_ = true;
    thread_ = std::thread(&SMTCClient::run, this);
    return true;
}

void SMTCClient::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SMTCClient::run() {
    try {
        // Request session manager
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        manager_ = reinterpret_cast<void*>(winrt::get_abi(manager));
        
        // Subscribe to current session changed event
        auto token = manager.CurrentSessionChanged([this](auto&&, auto&&) {
            processMediaUpdate();
        });
        
        // Subscribe to media properties changed for all sessions
        auto sessions = manager.GetSessions();
        for (auto session : sessions) {
            session.MediaPropertiesChanged([this](auto&&, auto&&) {
                processMediaUpdate();
            });
            session.PlaybackInfoChanged([this](auto&&, auto&&) {
                processMediaUpdate();
            });
            session.TimelinePropertiesChanged([this](auto&&, auto&&) {
                processMediaUpdate();
            });
        }
        
        // Initial fetch
        processMediaUpdate();
        
        // Keep running and process updates
        while (running_) {
            // Sleep and check periodically
            Sleep(1500);  // 1.5s interval matching OSC rate
            
            // Refresh media info
            processMediaUpdate();
        }
        
    } catch (const winrt::hresult_error& e) {
        // WinRT error
        char msg[256];
        sprintf_s(msg, "SMTC Error: 0x%08X", e.code().value);
        OutputDebugStringA(msg);
    } catch (...) {
        // Other error
        OutputDebugStringA("SMTC: Unknown error");
    }
}

void SMTCClient::processMediaUpdate() {
    try {
        auto manager = *reinterpret_cast<GlobalSystemMediaTransportControlsSessionManager*>(&manager_);
        if (!manager) {
            // Re-acquire manager
            manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            manager_ = reinterpret_cast<void*>(winrt::get_abi(manager));
        }
        
        // Known music player app IDs (partial match)
        static const std::vector<std::wstring> musicPlayerIds = {
            L"QQMusic",           // QQ Music
            L"Spotify",           // Spotify
            L"CloudMusic",        // Netease Cloud Music
            L"Kugou",             // Kugou
            L"KuGou",             // Kugou (alt)
            L"AppleMusic",        // Apple Music
            L"ZuneMusic",         // Groove Music
            L"Foobar2000",        // Foobar2000
            L"Music.UI",          // Windows Media Player
        };
        
        auto sessions = manager.GetSessions();
        GlobalSystemMediaTransportControlsSession current = nullptr;
        
        // Find a music player session (prefer QQ Music if filter is set)
        for (auto session : sessions) {
            auto appId = session.SourceAppUserModelId();
            std::wstring appIdStr = appId.c_str();
            
            // Check if this is a music player
            bool isMusicPlayer = false;
            for (const auto& musicId : musicPlayerIds) {
                if (appIdStr.find(musicId) != std::wstring::npos) {
                    isMusicPlayer = true;
                    break;
                }
            }
            
            // If app filter is set, only accept matching apps
            if (!appFilter_.empty()) {
                if (appIdStr.find(appFilter_) != std::wstring::npos && isMusicPlayer) {
                    current = session;
                    break;  // Found the filtered app
                }
            } else if (isMusicPlayer) {
                // No filter, take first music player
                if (!current) {
                    current = session;
                }
                // Prefer QQ Music
                if (appIdStr.find(L"QQMusic") != std::wstring::npos) {
                    current = session;
                    break;
                }
            }
        }
        
        if (!current) {
            MediaInfo info;
            info.hasData = false;
            
            EnterCriticalSection(&cs_);
            currentMedia_ = info;
            LeaveCriticalSection(&cs_);
            
            if (callback_) callback_(info);
            return;
        }
        
        auto appId = current.SourceAppUserModelId();
        std::wstring appIdStr = appId.c_str();
        
        MediaInfo info;
        info.appId = appIdStr;
        info.appName = GetAppDisplayName(info.appId);
        info.hasData = true;
        
        // Get media properties
        auto mediaProps = current.TryGetMediaPropertiesAsync().get();
        if (mediaProps) {
            info.title = mediaProps.Title().c_str();
            info.artist = mediaProps.Artist().c_str();
            info.album = mediaProps.AlbumTitle().c_str();
            info.trackNumber = mediaProps.TrackNumber();
        }
        
        // Get playback info
        auto playbackInfo = current.GetPlaybackInfo();
        if (playbackInfo) {
            auto status = playbackInfo.PlaybackStatus();
            info.isPlaying = (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);
            
            auto rate = playbackInfo.PlaybackRate();
            if (rate) {
                info.playbackRate = rate.Value();
            }
        }
        
        // Get timeline properties
        auto timeline = current.GetTimelineProperties();
        if (timeline) {
            info.position = TimeSpanToSeconds(timeline.Position());
            info.duration = TimeSpanToSeconds(timeline.EndTime() - timeline.StartTime());
        }
        
        // Update current media
        EnterCriticalSection(&cs_);
        currentMedia_ = info;
        LeaveCriticalSection(&cs_);
        
        // Call callback
        if (callback_) callback_(info);
        
    } catch (...) {
        // Error getting media info
    }
}

MediaInfo SMTCClient::getCurrentMedia() {
    EnterCriticalSection(&cs_);
    MediaInfo info = currentMedia_;
    LeaveCriticalSection(&cs_);
    return info;
}

} // namespace smtc

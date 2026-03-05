#include "gsmtc_client.h"
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.h>

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Foundation;

namespace moekoe {

// 将 100 纳秒单位转换为秒
inline double TimeSpanToSeconds(const TimeSpan& span) {
    return std::chrono::duration<double>(span).count();
}

GSMTCCient::GSMTCCient() = default;

GSMTCCient::~GSMTCCient() {
    stop();
}

bool GSMTCCient::start(const std::wstring& target_app_id) {
    if (running_) return true;
    
    target_app_id_ = target_app_id;
    running_ = true;
    thread_ = std::thread(&GSMTCCient::monitorLoop, this);
    
    return true;
}

void GSMTCCient::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    connected_ = false;
}

double GSMTCCient::getCurrentTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_time_;
}

double GSMTCCient::getTotalTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_time_;
}

PlaybackState GSMTCCient::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool GSMTCCient::isConnected() const {
    return connected_;
}

void GSMTCCient::setProgressCallback(ProgressCallback callback) {
    progress_callback_ = std::move(callback);
}

void GSMTCCient::setStateCallback(StateCallback callback) {
    state_callback_ = std::move(callback);
}

void GSMTCCient::monitorLoop() {
    // 初始化 WinRT
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    
    GlobalSystemMediaTransportControlsSessionManager session_manager{nullptr};
    
    while (running_) {
        try {
            // 获取会话管理器
            if (!session_manager) {
                auto async = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
                session_manager = async.get();
            }
            
            if (session_manager) {
                auto session = session_manager.GetCurrentSession();
                
                if (session) {
                    // 检查是否是目标应用
                    auto app_id = session.SourceAppUserModelId();
                    if (!target_app_id_.empty() && app_id != target_app_id_) {
                        connected_ = false;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    
                    // 获取播放状态
                    auto playback_info = session.GetPlaybackInfo();
                    PlaybackState new_state = PlaybackState::Unknown;
                    switch (playback_info.PlaybackStatus()) {
                        case 3: new_state = PlaybackState::Stopped; break;
                        case 4: new_state = PlaybackState::Playing; break;
                        case 5: new_state = PlaybackState::Paused; break;
                        default: break;
                    }
                    
                    // 获取进度
                    auto timeline = session.GetTimelineProperties();
                    double new_current = TimeSpanToSeconds(timeline.Position());
                    double new_total = TimeSpanToSeconds(timeline.EndTime());
                    
                    // 更新状态
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        current_time_ = new_current;
                        total_time_ = new_total;
                        state_ = new_state;
                    }
                    connected_ = true;
                    
                    // 回调
                    if (progress_callback_) {
                        progress_callback_(new_current, new_total);
                    }
                    
                    if (state_ != new_state && state_callback_) {
                        state_callback_(new_state);
                    }
                } else {
                    connected_ = false;
                }
            }
        } catch (const winrt::hresult_error& e) {
            // 忽略错误，继续轮询
            connected_ = false;
        } catch (...) {
            connected_ = false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    winrt::uninit_apartment();
}

} // namespace moekoe

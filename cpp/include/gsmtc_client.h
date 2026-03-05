#pragma once

#include "types.h"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace moekoe {

/**
 * Windows GSMTC 客户端
 * 
 * 通过 Windows 系统 API 获取精确播放进度。
 * 比 WebSocket 方式更精确、延迟更低。
 */
class GSMTCCient {
public:
    using ProgressCallback = std::function<void(double current, double total)>;
    using StateCallback = std::function<void(PlaybackState state)>;
    
    GSMTCCient();
    ~GSMTCCient();
    
    // 启动/停止
    bool start(const std::wstring& target_app_id = L"cn.MoeKoe.Music");
    void stop();
    
    // 获取当前进度
    double getCurrentTime() const;
    double getTotalTime() const;
    PlaybackState getState() const;
    bool isConnected() const;
    
    // 回调
    void setProgressCallback(ProgressCallback callback);
    void setStateCallback(StateCallback callback);
    
private:
    void monitorLoop();
    
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    
    double current_time_ = 0.0;
    double total_time_ = 0.0;
    PlaybackState state_ = PlaybackState::Unknown;
    bool connected_ = false;
    
    std::wstring target_app_id_;
    ProgressCallback progress_callback_;
    StateCallback state_callback_;
};

} // namespace moekoe

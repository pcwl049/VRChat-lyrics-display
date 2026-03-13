#pragma once

// Windows 头文件包含顺序很重要
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>
#include "../moekoe_ws.h"

// ============================================================================
// OSCManager - 统一 OSC 消息发送管理
// ============================================================================
class OSCManager {
public:
    static OSCManager& instance() {
        static OSCManager mgr;
        return mgr;
    }
    
    // 连接管理
    void connect(const std::string& ip, int port);
    void disconnect();
    bool isConnected() const { return m_sender != nullptr; }
    
    // 状态控制
    void pause(int seconds = 30);
    void resume();
    bool isPaused() const;
    int getRemainingPauseTime();
    float getPauseProgress();
    
    // Overlay 控制
    void setOverlayClosing(bool closing) { m_overlayClosing = closing; }
    bool isOverlayClosing() const { return m_overlayClosing; }
    
    // 消息发送
    bool sendMessage(const std::wstring& msg);
    bool sendMessageForce(const std::wstring& msg);
    bool sendSystemMessage(const std::wstring& msg, bool clearQueue = true);
    void sendGoodbye();
    
    // 配置
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setSystemResumeTime(DWORD time) { m_systemResumeTime = time; }
    
    // 兼容性访问
    moekoe::OSCSender* getSender() { return m_sender; }
    const std::string& getIp() const { return m_ip; }
    int getPort() const { return m_port; }
    
    // 清除上次发送的消息
    void clearLastMessage() { m_lastMessage.clear(); }
    
private:
    OSCManager() = default;
    ~OSCManager() { disconnect(); }
    
    bool canSend();
    void doSend(const std::wstring& msg);
    
    moekoe::OSCSender* m_sender = nullptr;
    std::string m_ip;
    int m_port = 9000;
    bool m_enabled = true;
    bool m_paused = false;
    bool m_overlayClosing = false;
    DWORD m_pauseEndTime = 0;
    DWORD m_lastSendTime = 0;
    DWORD m_systemResumeTime = 0;
    std::wstring m_lastMessage;
};

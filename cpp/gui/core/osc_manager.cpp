// osc_manager.cpp - OSC 消息管理实现
// Windows 头文件包含顺序很重要
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "osc_manager.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include "../common/logger.h"
#include "../common/config.h"

// OSC 最小发送间隔
const DWORD OSC_MIN_INTERVAL = 2000;

// 系统消息队列（内部使用）
static std::queue<std::wstring> s_systemMsgQueue;
static std::mutex s_systemMsgMutex;
static std::atomic<bool> s_systemMsgRunning{false};

void OSCManager::connect(const std::string& ip, int port) {
    disconnect();
    m_sender = new moekoe::OSCSender(ip, port);
    m_ip = ip;
    m_port = port;
    char msg[128];
    sprintf_s(msg, "Connected to %s:%d", ip.c_str(), port);
    LOG_INFO("OSCManager", "%s", msg);
}

void OSCManager::disconnect() {
    if (m_sender) {
        delete m_sender;
        m_sender = nullptr;
        LOG_INFO("OSCManager", "Disconnected");
    }
}

void OSCManager::pause(int seconds) {
    m_paused = true;
    m_pauseEndTime = GetTickCount() + seconds * 1000;
    m_overlayClosing = false;
    // 同步全局变量
    extern bool g_oscPaused;
    extern DWORD g_oscPauseEndTime;
    g_oscPaused = true;
    g_oscPauseEndTime = m_pauseEndTime;
    char msg[64];
    sprintf_s(msg, "Paused for %d seconds", seconds);
    LOG_INFO("OSCManager", "%s", msg);
}

void OSCManager::resume() {
    m_paused = false;
    m_pauseEndTime = 0;
    // 同步全局变量
    extern bool g_oscPaused;
    extern DWORD g_oscPauseEndTime;
    g_oscPaused = false;
    g_oscPauseEndTime = 0;
    LOG_INFO("OSCManager", "Resumed");
}

bool OSCManager::isPaused() const {
    extern bool g_oscPaused;
    extern DWORD g_oscPauseEndTime;
    if (g_oscPaused && g_oscPauseEndTime > 0) {
        return GetTickCount() < g_oscPauseEndTime;
    }
    return g_oscPaused;
}

int OSCManager::getRemainingPauseTime() {
    extern bool g_oscPaused;
    extern DWORD g_oscPauseEndTime;
    if (!g_oscPaused || g_oscPauseEndTime == 0) return 0;
    DWORD remaining = g_oscPauseEndTime - GetTickCount();
    return remaining > 0 ? (int)(remaining / 1000) : 0;
}

float OSCManager::getPauseProgress() {
    extern bool g_oscPaused;
    extern DWORD g_oscPauseEndTime;
    if (!g_oscPaused || g_oscPauseEndTime == 0) return 0.0f;
    DWORD now = GetTickCount();
    if (now >= g_oscPauseEndTime) return 0.0f;
    return (float)(g_oscPauseEndTime - now) / (30.0f * 1000.0f);
}

bool OSCManager::canSend() {
    if (!m_sender || !m_enabled) return false;
    if (isPaused()) return false;
    return true;
}

void OSCManager::doSend(const std::wstring& msg) {
    if (m_sender) {
        m_sender->sendChatbox(msg);
    }
}

bool OSCManager::sendMessage(const std::wstring& msg) {
    if (!canSend()) {
        return false;
    }
    
    DWORD now = GetTickCount();
    
    // 系统恢复后的额外等待
    if (m_systemResumeTime > 0) {
        DWORD timeSinceResume = now - m_systemResumeTime;
        if (timeSinceResume < 3000) {
            if (timeSinceResume < 100) {
                m_lastSendTime = now;
            }
            return false;
        } else {
            m_systemResumeTime = 0;
        }
    }
    
    // 速率限制
    DWORD timeSinceLastSend = now - m_lastSendTime;
    if (timeSinceLastSend < OSC_MIN_INTERVAL) {
        return false;
    }
    
    // 去重
    if (msg == m_lastMessage) {
        return false;
    }
    
    doSend(msg);
    m_lastMessage = msg;
    m_lastSendTime = now;
    LOG_INFO("OSCManager", "Message sent successfully");
    return true;
}

bool OSCManager::sendMessageForce(const std::wstring& msg) {
    if (!canSend()) return false;
    
    DWORD now = GetTickCount();
    if (now - m_lastSendTime < OSC_MIN_INTERVAL) {
        return false;
    }
    
    doSend(msg);
    m_lastMessage = msg;
    m_lastSendTime = now;
    return true;
}

bool OSCManager::sendSystemMessage(const std::wstring& message, bool clearQueue) {
    if (!m_sender || !m_enabled) {
        return false;
    }
    
    // 将消息加入队列
    {
        std::lock_guard<std::mutex> lock(s_systemMsgMutex);
        if (clearQueue) {
            while (!s_systemMsgQueue.empty()) {
                s_systemMsgQueue.pop();
            }
        }
        s_systemMsgQueue.push(message);
    }
    
    // 如果线程未运行，启动新线程处理队列
    if (!s_systemMsgRunning.exchange(true)) {
        std::thread([this]() {
            while (true) {
                std::wstring msg;
                {
                    std::lock_guard<std::mutex> lock(s_systemMsgMutex);
                    if (s_systemMsgQueue.empty()) {
                        s_systemMsgRunning = false;
                        break;
                    }
                    msg = s_systemMsgQueue.front();
                    s_systemMsgQueue.pop();
                }
                
                // 等待足够时间，确保不触发限流
                DWORD now = GetTickCount();
                DWORD timeSinceLastSend = now - m_lastSendTime;
                if (timeSinceLastSend < OSC_MIN_INTERVAL) {
                    Sleep(OSC_MIN_INTERVAL - timeSinceLastSend);
                }
                
                // 发送消息
                if (m_sender && m_enabled) {
                    m_sender->sendChatbox(msg);
                    m_lastSendTime = GetTickCount();
                    LOG_INFO("OSCManager", "Sent system message");
                }
            }
        }).detach();
    }
    
    return true;
}

void OSCManager::sendGoodbye() {
    if (!m_sender || !m_enabled) return;
    
    DWORD now = GetTickCount();
    DWORD timeSinceLastSend = now - m_lastSendTime;
    if (timeSinceLastSend < OSC_MIN_INTERVAL) {
        Sleep(OSC_MIN_INTERVAL - timeSinceLastSend);
    }
    // 根据极简模式发送不同的退出消息
    extern bool g_minimalMode;
    if (g_minimalMode) {
        m_sender->sendChatbox(L"·");
    } else {
        m_sender->sendChatbox(L"VRCLyricsDisplay\n\x6B22\x8FCE\x4E0B\x6B21\x4F7F\x7528\x54E6~");
    }
    m_lastSendTime = GetTickCount();
    LOG_INFO("OSCManager", "Goodbye message sent");
}

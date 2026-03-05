#pragma once

#include "types.h"
#include <string>
#include <cstdint>

namespace moekoe {

/**
 * VRChat OSC 发送器
 */
class OSCSender {
public:
    OSCSender(const std::string& ip = "127.0.0.1", int port = 9000);
    ~OSCSender();
    
    // 发送消息到 VRChat 聊天框
    bool sendChatMessage(const std::wstring& message);
    bool sendChatMessage(const std::string& message);
    
    // 清除聊天框
    bool clearChatbox();
    
    // 设置目标
    void setTarget(const std::string& ip, int port);
    
private:
    bool sendOSC(const std::string& address, const std::string& message);
    
    std::string ip_;
    int port_;
    int socket_ = -1;
};

} // namespace moekoe

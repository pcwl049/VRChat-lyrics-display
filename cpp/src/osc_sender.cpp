#include "osc_sender.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstdint>

#pragma comment(lib, "ws2_32.lib")

namespace moekoe {

// OSC 编码辅助函数
static void padToFourBytes(std::vector<char>& buffer) {
    while (buffer.size() % 4 != 0) {
        buffer.push_back(0);
    }
}

static void writeString(std::vector<char>& buffer, const std::string& str) {
    for (char c : str) {
        buffer.push_back(c);
    }
    buffer.push_back(0);
    padToFourBytes(buffer);
}

static void writeInt(std::vector<char>& buffer, int32_t value) {
    buffer.push_back((value >> 24) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back(value & 0xFF);
}

static void writeFloat(std::vector<char>& buffer, float value) {
    uint32_t int_val;
    memcpy(&int_val, &value, sizeof(float));
    writeInt(buffer, static_cast<int32_t>(int_val));
}

OSCSender::OSCSender(const std::string& ip, int port)
    : ip_(ip), port_(port) {
    // 初始化 Winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
    
    // 创建 UDP socket
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

OSCSender::~OSCSender() {
    if (socket_ != -1) {
        closesocket(socket_);
    }
    WSACleanup();
}

void OSCSender::setTarget(const std::string& ip, int port) {
    ip_ = ip;
    port_ = port;
}

bool OSCSender::sendChatMessage(const std::wstring& message) {
    // 宽字符转 UTF-8
    if (message.empty()) return false;
    
    int size = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, 
                                    nullptr, 0, nullptr, nullptr);
    std::string utf8_message(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1,
                        &utf8_message[0], size, nullptr, nullptr);
    
    return sendChatMessage(utf8_message);
}

bool OSCSender::sendChatMessage(const std::string& message) {
    return sendOSC("/chatbox/input", message);
}

bool OSCSender::clearChatbox() {
    return sendOSC("/chatbox/input", "");
}

bool OSCSender::sendOSC(const std::string& address, const std::string& message) {
    if (socket_ == -1) return false;
    
    // 构建 OSC 消息
    std::vector<char> buffer;
    
    // 地址
    writeString(buffer, address);
    
    // 类型标签
    writeString(buffer, ",sT");  // string + true (立即显示)
    
    // 消息内容
    writeString(buffer, message);
    
    // 发送
    sockaddr_in dest;
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port_);
    inet_pton(AF_INET, ip_.c_str(), &dest.sin_addr);
    
    int result = sendto(socket_, buffer.data(), static_cast<int>(buffer.size()),
                        0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    
    return result != SOCKET_ERROR;
}

} // namespace moekoe

// moekoe_ws.h - MoeKoeMusic WebSocket Client

#pragma once
#define _WIN32_IE 0x0600
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>  // For NOTIFYICONDATAW
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>

namespace moekoe {

struct LyricLine {
    int startTime = 0;    // ms
    int duration = 0;     // ms
    std::wstring text;
};

struct SongInfo {
    std::wstring title = L"Waiting...";
    std::wstring artist = L"";
    double duration = 0;
    double currentTime = 0;
    bool isPlaying = false;
    bool hasData = false;
    std::vector<LyricLine> lyrics;
};

// Simple OSC sender for VRChat chatbox
class OSCSender {
public:
    OSCSender(const std::string& ip = "127.0.0.1", int port = 9000);
    ~OSCSender();
    
    bool sendChatbox(const std::string& message);
    bool sendChatbox(const std::wstring& message);
    
private:
    std::vector<uint8_t> buildOSCMessage(const std::string& address, const std::string& message);
    void padTo4(std::vector<uint8_t>& data);
    
    std::string ip_;
    int port_;
    SOCKET sock_ = INVALID_SOCKET;
};

class MoeKoeWS {
public:
    using Callback = std::function<void(const SongInfo&)>;
    
    MoeKoeWS(const std::string& host = "127.0.0.1", int port = 6520);
    ~MoeKoeWS();
    
    void setCallback(Callback cb) { callback_ = cb; }
    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }
    const SongInfo& getSongInfo() const { return songInfo_; }
    
private:
    void run();
    void parseMessage(const std::string& msg);
    std::string decodeBase64(const std::string& encoded);
    std::wstring utf8ToWstring(const std::string& utf8);
    
    std::string host_;
    int port_;
    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread thread_;
    Callback callback_;
    SongInfo songInfo_;
};

} // namespace moekoe

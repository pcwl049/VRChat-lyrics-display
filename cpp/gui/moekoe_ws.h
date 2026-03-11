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
#include <mutex>

namespace moekoe {

struct LyricLine {
    int startTime = 0;    // ms
    int duration = 0;     // ms
    std::wstring text;
    std::wstring translation;  // Translated lyric (optional)
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

// OSC Receiver for VRChat parameters (pause control via VRChat gestures)
// Users can bind VRChat gestures to send OSC parameters
class OSCReceiver {
public:
    using PauseCallback = std::function<void()>;
    
    OSCReceiver(int port = 9001);
    ~OSCReceiver();
    
    void setPauseCallback(PauseCallback cb) { pauseCallback_ = cb; }
    bool start();
    void stop();
    bool isRunning() const { return running_; }
    int getPort() const { return port_; }
    
private:
    void run();
    bool parseOSCMessage(const uint8_t* data, size_t len);
    std::string readString(const uint8_t* data, size_t len, size_t& pos);
    
    int port_;
    SOCKET sock_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread thread_;
    PauseCallback pauseCallback_;
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
    const SongInfo getSongInfo() const;  // 返回副本，线程安全
    
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
    mutable std::mutex songInfoMutex_;  // 保护 songInfo_ 的互斥锁
    SongInfo songInfo_;
};

} // namespace moekoe

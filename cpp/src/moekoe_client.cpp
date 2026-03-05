#include "moekoe_client.h"
#include <winhttp.h>
#include <nlohmann/json.hpp>

#pragma comment(lib, "winhttp.lib")

namespace moekoe {

MoeKoeClient::MoeKoeClient(const std::string& host, int port)
    : host_(host), port_(port) {
}

MoeKoeClient::~MoeKoeClient() {
    disconnect();
}

bool MoeKoeClient::connect() {
    if (running_) return true;
    
    // 初始化 WebSocket 连接
    HINTERNET session = WinHttpOpen(L"MoeKoeVRChat/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    // 转换 host 为宽字符
    int size = MultiByteToWideChar(CP_UTF8, 0, host_.c_str(), -1, nullptr, 0);
    std::wstring whost(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, host_.c_str(), -1, &whost[0], size);
    
    HINTERNET connect = WinHttpConnect(session, whost.c_str(), port_, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    // WebSocket 握手
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // 升级到 WebSocket
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          NULL, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // 发送请求
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // 完成握手
    DWORD status = 0;
    DWORD size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, NULL);
    
    if (status != 101) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    // 获取 WebSocket 句柄
    websocket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    
    if (!websocket_) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    running_ = true;
    connected_ = true;
    thread_ = std::thread(&MoeKoeClient::receiveLoop, this);
    
    return true;
}

void MoeKoeClient::disconnect() {
    running_ = false;
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    if (websocket_) {
        WinHttpWebSocketClose(websocket_, 1000, NULL, 0);
        WinHttpCloseHandle(websocket_);
        websocket_ = nullptr;
    }
    
    connected_ = false;
}

bool MoeKoeClient::isConnected() const {
    return connected_;
}

const SongInfo& MoeKoeClient::getSongInfo() const {
    return song_info_;
}

PlaybackState MoeKoeClient::getPlaybackState() const {
    return playback_state_;
}

const std::vector<LyricLine>& MoeKoeClient::getLyrics() const {
    return lyrics_;
}

void MoeKoeClient::setSongChangeCallback(SongChangeCallback callback) {
    song_callback_ = std::move(callback);
}

void MoeKoeClient::setStateChangeCallback(StateChangeCallback callback) {
    state_callback_ = std::move(callback);
}

void MoeKoeClient::receiveLoop() {
    std::vector<char> buffer(4096);
    
    while (running_) {
        DWORD read = ;
        DWORD buffer_type = ;
        
        HRESULT result = WinHttpWebSocketReceive(websocket_, 
                                                  buffer.data(), 
                                                  static_cast<DWORD>(buffer.size()),
                                                  &read, 
                                                  &buffer_type);
        
        if (FAILED(result) || buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            connected_ = false;
            break;
        }
        
        if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            std::string message(buffer.data(), read);
            handleMessage(message);
        }
    }
}

void MoeKoeClient::handleMessage(const std::string& message) {
    try {
        auto json = nlohmann::json::parse(message);
        
        if (json.contains("type")) {
            std::string type = json["type"];
            
            if (type == "songChange" || type == "song") {
                parseSongInfo(json["data"].dump());
            } else if (type == "lyrics") {
                parseLyrics(json["data"].dump());
            } else if (type == "playerState") {
                parsePlaybackState(json["data"].dump());
            }
        }
    } catch (const std::exception& e) {
        // 解析错误，忽略
    }
}

void MoeKoeClient::parseSongInfo(const std::string& json_str) {
    try {
        auto json = nlohmann::json::parse(json_str);
        
        SongInfo new_info;
        
        if (json.contains("name")) {
            std::string name = json["name"];
            int size = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
            new_info.title.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, &new_info.title[0], size);
        }
        
        if (json.contains("author")) {
            std::string author = json["author"];
            int size = MultiByteToWideChar(CP_UTF8, 0, author.c_str(), -1, nullptr, 0);
            new_info.artist.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, author.c_str(), -1, &new_info.artist[0], size);
        }
        
        if (json.contains("hash")) {
            new_info.hash = json["hash"];
        }
        
        if (json.contains("duration")) {
            new_info.duration = json["duration"];
        }
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            song_info_ = new_info;
        }
        
        if (song_callback_) {
            song_callback_(new_info);
        }
        
    } catch (const std::exception& e) {
        // 解析错误
    }
}

void MoeKoeClient::parseLyrics(const std::string& json_str) {
    // TODO: 解析歌词
}

void MoeKoeClient::parsePlaybackState(const std::string& json_str) {
    try {
        auto json = nlohmann::json::parse(json_str);
        
        PlaybackState new_state = PlaybackState::Unknown;
        if (json.contains("isPlaying")) {
            new_state = json["isPlaying"] ? PlaybackState::Playing : PlaybackState::Paused;
        }
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            playback_state_ = new_state;
        }
        
        if (state_callback_) {
            state_callback_(new_state);
        }
        
    } catch (const std::exception& e) {
        // 解析错误
    }
}

} // namespace moekoe

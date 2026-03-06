#include "netease_client.h"
#include <winhttp.h>
#include <nlohmann/json.hpp>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace moekoe {

// 注入到网易云页面的监控脚本
static const char* INJECT_SCRIPT = R"(
(function() {
    if (window.__NCM_MONITOR_INSTALLED__) return true;

    window.__NCM_PLAY_STATE__ = {
        playId: null,
        current: null,
        state: null,
        isPlaying: false,
        duration: 0,
        title: '',
        artist: ''
    };

    try {
        const webpackRequire = window.webpackJsonp.push([[],{9999:function(e,t,r){e.exports=r}},[[9999]]]);
        const moduleCache = webpackRequire.c;

        let adapter = null;
        let reduxStore = null;

        for (const id in moduleCache) {
            if (adapter && reduxStore) break;
            const mod = moduleCache[id]?.exports;
            if (!mod) continue;
            if (!adapter && mod.Bridge?._Adapter?.registerCallMap) {
                adapter = mod.Bridge._Adapter;
            }
            if (!reduxStore && mod.a?.app?._store?.getState) {
                reduxStore = mod.a.app._store;
            }
        }

        if (!adapter?.registerCallMap) return false;

        const appendCallback = (key, callback) => {
            const existing = adapter.registerCallMap[key];
            if (existing) {
                const callbacks = Array.isArray(existing) ? existing : [existing];
                adapter.registerCallMap[key] = [...callbacks, callback];
                return true;
            }
            return false;
        };

        appendCallback('audioplayer.onPlayProgress', (playId, current, cacheProgress) => {
            window.__NCM_PLAY_STATE__.playId = String(playId);
            window.__NCM_PLAY_STATE__.current = parseFloat(current);
        });

        appendCallback('audioplayer.onPlayState', (playId, _, state) => {
            window.__NCM_PLAY_STATE__.playId = String(playId);
            window.__NCM_PLAY_STATE__.state = parseInt(state);
            window.__NCM_PLAY_STATE__.isPlaying = parseInt(state) === 1;
        });

        if (reduxStore) {
            window.__REDUX_STORE__ = reduxStore;
        }

        window.__NCM_MONITOR_INSTALLED__ = true;
        return true;
    } catch(e) {
        return false;
    }
})();
)";

NeteaseClient::NeteaseClient(int port) : port_(port) {
}

NeteaseClient::~NeteaseClient() {
    disconnect();
}

bool NeteaseClient::connect() {
    if (running_) return true;
    
    if (!connectToDevTools()) {
        return false;
    }
    
    if (!attachToOrpheusPage()) {
        disconnect();
        return false;
    }
    
    // 等待页面加载
    Sleep(1000);
    
    if (!injectMonitorScript()) {
        disconnect();
        return false;
    }
    
    running_ = true;
    connected_ = true;
    
    receive_thread_ = std::thread(&NeteaseClient::receiveLoop, this);
    poll_thread_ = std::thread(&NeteaseClient::pollLoop, this);
    
    return true;
}

void NeteaseClient::disconnect() {
    running_ = false;
    
    response_cv_.notify_all();
    
    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
    
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
    
    if (websocket_) {
        WinHttpWebSocketClose((HINTERNET)websocket_, 1000, NULL, 0);
        WinHttpCloseHandle((HINTERNET)websocket_);
        websocket_ = nullptr;
    }
    
    connected_ = false;
}

bool NeteaseClient::isConnected() const {
    return connected_;
}

const SongInfo& NeteaseClient::getSongInfo() const {
    return song_info_;
}

PlaybackState NeteaseClient::getPlaybackState() const {
    return playback_state_;
}

const std::vector<LyricLine>& NeteaseClient::getLyrics() const {
    return lyrics_;
}

std::string NeteaseClient::httpGet(const std::string& path) {
    HINTERNET session = WinHttpOpen(L"MoeKoeVRChat/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return "";
    
    std::wstring whost = L"127.0.0.1";
    HINTERNET connect = WinHttpConnect(session, whost.c_str(), port_, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return "";
    }
    
    int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], size);
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", wpath.c_str(),
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return "";
    }
    
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return "";
    }
    
    if (!WinHttpReceiveResponse(request, NULL)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return "";
    }
    
    std::string response;
    DWORD read = 0;
    char buffer[4096];
    
    do {
        DWORD downloaded = 0;
        if (!WinHttpReadData(request, buffer, sizeof(buffer) - 1, &downloaded)) {
            break;
        }
        buffer[downloaded] = 0;
        response += buffer;
    } while (downloaded > 0);
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    return response;
}

bool NeteaseClient::connectToDevTools() {
    // 获取调试页面列表
    std::string json = httpGet("/json");
    if (json.empty()) return false;
    
    try {
        auto pages = nlohmann::json::parse(json);
        if (!pages.is_array() || pages.empty()) return false;
        
        // 找到 orpheus 页面
        std::string ws_url;
        for (const auto& page : pages) {
            if (page.contains("url") && page["url"].get<std::string>().find("orpheus") != std::string::npos) {
                ws_url = page["webSocketDebuggerUrl"].get<std::string>();
                break;
            }
        }
        
        if (ws_url.empty() && !pages.empty()) {
            ws_url = pages[0]["webSocketDebuggerUrl"].get<std::string>();
        }
        
        if (ws_url.empty()) return false;
        
        // 解析 WebSocket URL
        // ws://127.0.0.1:9222/devtools/page/xxx
        size_t pos = ws_url.find("ws://");
        if (pos == std::string::npos) return false;
        
        pos += 5; // skip "ws://"
        size_t colon = ws_url.find(':', pos);
        size_t slash = ws_url.find('/', colon);
        
        std::string host = ws_url.substr(pos, colon - pos);
        int port = std::stoi(ws_url.substr(colon + 1, slash - colon - 1));
        std::string path = ws_url.substr(slash);
        
        // 连接 WebSocket
        HINTERNET session = WinHttpOpen(L"MoeKoeVRChat/1.0",
                                         WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return false;
        
        int wsize = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
        std::wstring whost(wsize - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, &whost[0], wsize);
        
        HINTERNET connect = WinHttpConnect(session, whost.c_str(), port, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            return false;
        }
        
        wsize = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wpath(wsize - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wsize);
        
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", wpath.c_str(),
                                                NULL, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!request) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        
        if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        
        if (!WinHttpReceiveResponse(request, NULL)) {
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        
        websocket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
        WinHttpCloseHandle(request);
        
        if (!websocket_) {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return false;
        }
        
        return true;
        
    } catch (...) {
        return false;
    }
}

bool NeteaseClient::attachToOrpheusPage() {
    // 获取所有目标
    int id = sendCommand("Target.getTargets");
    
    // 等待响应
    std::unique_lock<std::mutex> lock(mutex_);
    response_cv_.wait_for(lock, std::chrono::seconds(5), [&]() {
        return pending_responses_.count(id) > 0 || !running_;
    });
    
    if (pending_responses_.count(id) == 0) return false;
    
    try {
        auto resp = nlohmann::json::parse(pending_responses_[id]);
        pending_responses_.erase(id);
        
        auto targets = resp["result"]["targetInfos"];
        for (const auto& target : targets) {
            if (target["type"] == "page" && target["url"].get<std::string>().find("orpheus") != std::string::npos) {
                std::string targetId = target["targetId"];
                
                // 附加到目标
                nlohmann::json params;
                params["targetId"] = targetId;
                params["flatten"] = false;
                
                int attachId = sendCommand("Target.attachToTarget", params.dump());
                
                response_cv_.wait_for(lock, std::chrono::seconds(5), [&]() {
                    return pending_responses_.count(attachId) > 0 || !running_;
                });
                
                if (pending_responses_.count(attachId) > 0) {
                    auto attachResp = nlohmann::json::parse(pending_responses_[attachId]);
                    pending_responses_.erase(attachId);
                    session_id_ = attachResp["result"]["sessionId"].get<std::string>();
                    return true;
                }
            }
        }
    } catch (...) {}
    
    return false;
}

bool NeteaseClient::injectMonitorScript() {
    std::string result = evaluateScript(INJECT_SCRIPT);
    return result == "true";
}

int NeteaseClient::sendCommand(const std::string& method, const std::string& params) {
    if (!websocket_) return -1;
    
    nlohmann::json msg;
    int id = msg_id_++;
    msg["id"] = id;
    msg["method"] = method;
    msg["params"] = nlohmann::json::parse(params);
    
    if (!session_id_.empty()) {
        // 发送到特定 session
        nlohmann::json wrapper;
        wrapper["id"] = msg_id_++;
        wrapper["method"] = "Target.sendMessageToTarget";
        wrapper["params"]["sessionId"] = session_id_;
        wrapper["params"]["message"] = msg.dump();
        msg = wrapper;
    }
    
    std::string data = msg.dump();
    
    WinHttpWebSocketSend((HINTERNET)websocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                         (void*)data.c_str(), (DWORD)data.length());
    
    return id;
}

std::string NeteaseClient::evaluateScript(const std::string& script) {
    nlohmann::json params;
    params["expression"] = script;
    params["returnByValue"] = true;
    
    int id = sendCommand("Runtime.evaluate", params.dump());
    
    std::unique_lock<std::mutex> lock(mutex_);
    response_cv_.wait_for(lock, std::chrono::seconds(5), [&]() {
        return pending_responses_.count(id) > 0 || !running_;
    });
    
    if (pending_responses_.count(id) == 0) return "";
    
    try {
        auto resp = nlohmann::json::parse(pending_responses_[id]);
        pending_responses_.erase(id);
        
        if (resp.contains("result") && resp["result"].contains("result")) {
            auto& value = resp["result"]["result"]["value"];
            if (value.is_string()) {
                return value.get<std::string>();
            } else if (value.is_boolean()) {
                return value.get<bool>() ? "true" : "false";
            }
        }
    } catch (...) {}
    
    return "";
}

void NeteaseClient::receiveLoop() {
    std::vector<char> buffer(65536);
    
    while (running_) {
        DWORD read = 0;
        DWORD buffer_type = 0;
        
        HRESULT result = WinHttpWebSocketReceive((HINTERNET)websocket_,
                                                  buffer.data(),
                                                  (DWORD)buffer.size(),
                                                  &read,
                                                  &buffer_type);
        
        if (FAILED(result) || buffer_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            connected_ = false;
            break;
        }
        
        if (buffer_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            std::string message(buffer.data(), read);
            
            try {
                auto json = nlohmann::json::parse(message);
                
                // 处理目标消息
                if (json["method"] == "Target.receivedMessageFromTarget") {
                    auto inner = nlohmann::json::parse(json["params"]["message"].get<std::string>());
                    if (inner.contains("id")) {
                        int id = inner["id"].get<int>();
                        std::lock_guard<std::mutex> lock(mutex_);
                        pending_responses_[id] = inner.dump();
                        response_cv_.notify_all();
                    }
                } else if (json.contains("id")) {
                    int id = json["id"].get<int>();
                    std::lock_guard<std::mutex> lock(mutex_);
                    pending_responses_[id] = message;
                    response_cv_.notify_all();
                }
            } catch (...) {}
        }
    }
}

void NeteaseClient::pollLoop() {
    while (running_) {
        if (!fetchPlayState()) {
            Sleep(1000);
            continue;
        }
        
        Sleep(500);
    }
}

bool NeteaseClient::fetchPlayState() {
    std::string result = evaluateScript(R"(
        (function() {
            const state = window.__NCM_PLAY_STATE__;
            if (!state) return null;
            
            const store = window.__REDUX_STORE__;
            if (store) {
                const playing = store.getState()?.playing;
                if (playing) {
                    return JSON.stringify({
                        playId: String(playing.playId || playing.resourceTrackId || state.playId || ''),
                        current: state.current || playing.restoreResource?.current || 0,
                        isPlaying: state.isPlaying !== undefined ? state.isPlaying : (playing.playingState === 1),
                        duration: playing.resourceDuration || 0,
                        title: playing.resourceName || '',
                        artist: (playing.resourceArtists || []).map(a => a.name || a).join(' / ')
                    });
                }
            }
            
            return JSON.stringify(state);
        })()
    )");
    
    if (result.empty() || result == "null") return false;
    
    try {
        auto json = nlohmann::json::parse(result);
        
        std::string playId = json.value("playId", "");
        if (playId.empty()) return false;
        
        bool wasNewSong = (playId != current_song_id_);
        
        current_song_id_ = playId;
        current_time_ = json.value("current", 0.0);
        is_playing_ = json.value("isPlaying", false);
        double duration = json.value("duration", 0.0);
        std::string title = json.value("title", "");
        std::string artist = json.value("artist", "");
        
        // 更新歌曲信息
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            int size = MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, nullptr, 0);
            song_info_.title.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, title.c_str(), -1, &song_info_.title[0], size);
            
            size = MultiByteToWideChar(CP_UTF8, 0, artist.c_str(), -1, nullptr, 0);
            song_info_.artist.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, artist.c_str(), -1, &song_info_.artist[0], size);
            
            song_info_.duration = duration;
            song_info_.currentTime = current_time_;
            song_info_.isPlaying = is_playing_;
            song_info_.hasData = true;
            
            playback_state_ = is_playing_ ? PlaybackState::Playing : PlaybackState::Paused;
        }
        
        // 新歌曲，获取歌词
        if (wasNewSong) {
            fetchLyrics(playId);
            notifySongChange(song_info_);
        }
        
        notifyStateChange(playback_state_);
        return true;
        
    } catch (...) {
        return false;
    }
}

bool NeteaseClient::fetchLyrics(const std::string& songId) {
    // 使用网易云API获取歌词
    std::string url = "https://music.163.com/api/song/lyric?id=" + songId + "&lv=1&tv=1";
    
    HINTERNET session = WinHttpOpen(L"MoeKoeVRChat/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    
    HINTERNET connect = WinHttpConnect(session, L"music.163.com", 80, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }
    
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", L"/api/song/lyric?id=" + 
                                            std::wstring(songId.begin(), songId.end()) + L"&lv=1&tv=1",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }
    
    std::string response;
    char buffer[4096];
    DWORD downloaded = 0;
    
    do {
        if (!WinHttpReadData(request, buffer, sizeof(buffer) - 1, &downloaded)) break;
        buffer[downloaded] = 0;
        response += buffer;
    } while (downloaded > 0);
    
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    
    // 解析歌词
    try {
        auto json = nlohmann::json::parse(response);
        std::string lrc = json.value("lrc", nlohmann::json::object()).value("lyric", "");
        
        if (lrc.empty()) return false;
        
        std::vector<LyricLine> new_lyrics;
        std::regex lrcregex("\\[(\\d{2}):(\\d{2})\\.(\\d{2,3})\\](.*)");
        std::smatch match;
        
        std::string::const_iterator searchStart(lrc.cbegin());
        while (std::regex_search(searchStart, lrc.cend(), match, lrcregex)) {
            LyricLine line;
            int min = std::stoi(match[1].str());
            int sec = std::stoi(match[2].str());
            int ms = std::stoi(match[3].str());
            line.startTime = (min * 60 + sec) * 1000 + (match[3].str().length() == 2 ? ms * 10 : ms);
            
            std::string text = match[4].str();
            int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
            line.text.resize(size - 1);
            MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &line.text[0], size);
            
            if (!line.text.empty()) {
                new_lyrics.push_back(line);
            }
            
            searchStart = match.suffix().first;
        }
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            lyrics_ = std::move(new_lyrics);
        }
        
        return true;
        
    } catch (...) {
        return false;
    }
}

} // namespace moekoe

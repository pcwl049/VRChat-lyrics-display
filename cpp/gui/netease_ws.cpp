// netease_ws.cpp - Netease Cloud Music CDP Client
// Based on NcmVRChatSyncV2 reference implementation

#include "netease_ws.h"
#include <sstream>
#include <cstdio>
#include <map>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace moekoe {

// Debug log to file - use temp directory
static void DebugLog(const char* msg) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\netease_debug.log");
    
    HANDLE hFile = CreateFileA(tempPath, 
        FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, NULL);
        WriteFile(hFile, "\r\n", 2, &written, NULL);
        CloseHandle(hFile);
    }
}

static void DebugLog(const wchar_t* msg) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\netease_debug.log");
    
    HANDLE hFile = CreateFileA(tempPath, 
        FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        int len = WideCharToMultiByte(CP_UTF8, 0, msg, -1, NULL, 0, NULL, NULL);
        char* buffer = new char[len];
        WideCharToMultiByte(CP_UTF8, 0, msg, -1, buffer, len, NULL, NULL);
        WriteFile(hFile, buffer, len - 1, &written, NULL);
        WriteFile(hFile, "\r\n", 2, &written, NULL);
        delete[] buffer;
        CloseHandle(hFile);
    }
}

// JavaScript to inject into Netease page - always re-register callbacks
static const char* INJECT_SCRIPT = R"(
(function() {
  // Always initialize/reinitialize state
  if (!window.__NCM_PLAY_STATE__) {
    window.__NCM_PLAY_STATE__ = {
      playId: null,
      current: null,
      cacheProgress: null,
      lastUpdate: null,
      state: null,
      isPlaying: false
    };
  }

  try {
    // Find adapter and redux store from webpack modules
    var webpackRequire = window.webpackJsonp && window.webpackJsonp.push([[],{9999:function(e,t,r){e.exports=r}},[[9999]]]);
    if (!webpackRequire) return null;
    var moduleCache = webpackRequire.c;

    var adapter = null;
    var reduxStore = null;

    for (var id in moduleCache) {
      if (adapter && reduxStore) break;
      var mod = moduleCache[id] && moduleCache[id].exports;
      if (!mod) continue;
      if (!adapter && mod.Bridge && mod.Bridge._Adapter && mod.Bridge._Adapter.registerCallMap) {
        adapter = mod.Bridge._Adapter;
      }
      if (!reduxStore && mod.a && mod.a.app && mod.a.app._store && mod.a.app._store.getState) {
        reduxStore = mod.a.app._store;
      }
    }

    if (!adapter || !adapter.registerCallMap) {
      return null;
    }

    var updateState = function(playId, updates) {
      window.__NCM_PLAY_STATE__.playId = String(playId);
      window.__NCM_PLAY_STATE__.lastUpdate = Date.now();
      Object.assign(window.__NCM_PLAY_STATE__, updates);
    };

    // Force register callbacks - always append new ones
    var existing1 = adapter.registerCallMap['audioplayer.onPlayProgress'];
    var callbacks1 = existing1 ? (Array.isArray(existing1) ? existing1 : [existing1]) : [];
    callbacks1.push(function(playId, current, cacheProgress) {
      updateState(playId, { current: parseFloat(current), cacheProgress: parseFloat(cacheProgress) });
    });
    adapter.registerCallMap['audioplayer.onPlayProgress'] = callbacks1;

    var existing2 = adapter.registerCallMap['audioplayer.onPlayState'];
    var callbacks2 = existing2 ? (Array.isArray(existing2) ? existing2 : [existing2]) : [];
    callbacks2.push(function(playId, _, state) {
      updateState(playId, { state: parseInt(state), isPlaying: parseInt(state) === 1 });
    });
    adapter.registerCallMap['audioplayer.onPlayState'] = callbacks2;

    // Store redux store reference
    if (reduxStore) {
      window.__REDUX_STORE__ = reduxStore;
    }

    window.__NCM_MONITOR_INSTALLED__ = true;
    return window.__NCM_PLAY_STATE__;
  } catch(e) { return null; }
})();
)";

NeteaseWS::NeteaseWS(int port) : port_(port) {
    songInfo_.title = L"Waiting...";
    songInfo_.artist = L"";
    songInfo_.isPlaying = false;
    songInfo_.hasData = false;
}

NeteaseWS::~NeteaseWS() {
    disconnect();
}

std::string NeteaseWS::receiveMessage() {
    if (!hWebSocket_) return "";
    
    std::string response;
    char buffer[8192];
    DWORD read = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;
    
    HRESULT hr;
    do {
        hr = WinHttpWebSocketReceive(hWebSocket_, buffer, sizeof(buffer) - 1, &read, &bufferType);
        if (FAILED(hr)) return "";
        buffer[read] = 0;
        response += buffer;
    } while (bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);
    
    return response;
}

bool NeteaseWS::connect() {
    DebugLog("[Netease] connect() called");
    if (running_) return true;
    
    songInfo_.title = L"\x6B63\x5728\x8FDE\x63A5...";
    songInfo_.artist = L"";
    songInfo_.hasData = false;
    if (callback_) callback_(songInfo_);
    
    // Get WebSocket debugger URL from /json
    HINTERNET hSession = WinHttpOpen(L"NeteaseWS/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        songInfo_.artist = L"WinHttp \x521D\x59CB\x5316\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", port_, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        songInfo_.artist = L"\x7F51\x6613\x4E91\x672A\x542F\x52A8\x6216\x672A\x5F00\x542F\x8C03\x8BD5\x7AEF\x53E3";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/json",
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    DWORD timeout = 5000;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    
    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        songInfo_.artist = L"\x8BF7\x7528\x8F6F\x4EF6\x542F\x52A8\x7F51\x6613\x4E91";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    std::string response;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            response += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    // Parse WebSocket URL
    std::string wsUrl;
    size_t urlPos = response.find("\"webSocketDebuggerUrl\"");
    if (urlPos != std::string::npos) {
        size_t urlStart = response.find("\"", urlPos + 23);
        if (urlStart != std::string::npos) {
            size_t urlEnd = response.find("\"", urlStart + 1);
            if (urlEnd != std::string::npos) {
                wsUrl = response.substr(urlStart + 1, urlEnd - urlStart - 1);
            }
        }
    }
    
    if (wsUrl.empty()) {
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        songInfo_.artist = L"\x65E0\x6CD5\x89E3\x6790 WebSocket \x5730\x5740";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    std::string wsPath = "/";
    size_t pathStart = wsUrl.find("://");
    if (pathStart != std::string::npos) {
        pathStart = wsUrl.find("/", pathStart + 3);
        if (pathStart != std::string::npos) {
            wsPath = wsUrl.substr(pathStart);
        }
    }
    
    // Connect WebSocket
    hSession_ = WinHttpOpen(L"NeteaseWS/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession_) {
        songInfo_.title = L"WebSocket \x521D\x59CB\x5316\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    hConnect_ = WinHttpConnect(hSession_, L"127.0.0.1", port_, 0);
    if (!hConnect_) {
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x8FDE\x63A5\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    std::wstring wPath(wsPath.begin(), wsPath.end());
    hRequest_ = WinHttpOpenRequest(hConnect_, L"GET", wPath.c_str(),
                                    NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest_) {
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x8BF7\x6C42\x521B\x5EFA\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    if (!WinHttpSetOption(hRequest_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) {
        WinHttpCloseHandle(hRequest_);
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x5347\x7EA7\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    if (!WinHttpSendRequest(hRequest_, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest_);
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x53D1\x9001\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    if (!WinHttpReceiveResponse(hRequest_, NULL)) {
        WinHttpCloseHandle(hRequest_);
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x54CD\x5E94\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    hWebSocket_ = WinHttpWebSocketCompleteUpgrade(hRequest_, 0);
    WinHttpCloseHandle(hRequest_);
    hRequest_ = nullptr;
    
    if (!hWebSocket_) {
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"WebSocket \x5347\x7EA7\x5B8C\x6210\x5931\x8D25";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    msgId_ = 1;
    
    // Attach to orpheus page
    songInfo_.title = L"\x6B63\x5728\x9644\x52A0\x9875\x9762...";
    songInfo_.artist = L"";
    if (callback_) callback_(songInfo_);
    
    if (!attachToOrpheusPage()) {
        WinHttpCloseHandle(hWebSocket_);
        hWebSocket_ = nullptr;
        WinHttpCloseHandle(hConnect_);
        WinHttpCloseHandle(hSession_);
        songInfo_.title = L"\x8FDE\x63A5\x5931\x8D25";
        songInfo_.artist = L"\x627E\x4E0D\x5230\x7F51\x6613\x4E91\x9875\x9762";
        if (callback_) callback_(songInfo_);
        return false;
    }
    
    DebugLog("[Netease] Connected successfully, starting monitor thread");
    running_ = true;
    connected_ = true;
    thread_ = std::thread(&NeteaseWS::run, this);
    
    return true;
}

void NeteaseWS::disconnect() {
    running_ = false;
    
    if (thread_.joinable()) {
        thread_.join();
    }
    
    if (hWebSocket_) {
        WinHttpWebSocketClose(hWebSocket_, 1000, NULL, 0);
        WinHttpCloseHandle(hWebSocket_);
        hWebSocket_ = nullptr;
    }
    
    if (hConnect_) {
        WinHttpCloseHandle(hConnect_);
        hConnect_ = nullptr;
    }
    
    if (hSession_) {
        WinHttpCloseHandle(hSession_);
        hSession_ = nullptr;
    }
    
    connected_ = false;
    sessionId_.clear();
}

void NeteaseWS::run() {
    DebugLog("[Netease] run() started - monitoring loop beginning");
    songInfo_.hasData = true;
    songInfo_.title = L"Connected";
    songInfo_.artist = L"Netease";
    if (callback_) callback_(songInfo_);
    
    // Wait for webpack and Redux store to be ready
    int initRetries = 0;
    while (initRetries < 60 && running_) {
        std::string storeCheck = evaluateScript("typeof window.__REDUX_STORE__");
        if (storeCheck == "object") {
            break;  // Store is ready
        }
        
        // Force re-inject by clearing old state first
        evaluateScript("window.__NCM_MONITOR_INSTALLED__=false;window.__NCM_PLAY_STATE__=null;");
        // Then inject the monitoring script
        evaluateScript(INJECT_SCRIPT);
        Sleep(500);
        initRetries++;
    }
    
    if (initRetries >= 60) {
        songInfo_.title = L"\x521D\x59CB\x5316\x5931\x8D25";
        songInfo_.artist = L"\x65E0\x6CD5\x83B7\x53D6 Redux Store";
        if (callback_) callback_(songInfo_);
        return;
    }
    
    int failCount = 0;
    int injectCounter = 0;  // Re-inject every 10 iterations
    while (running_ && connected_ && failCount < 10) {
        Sleep(500);
        
        // Re-inject script periodically to ensure callbacks are registered
        injectCounter++;
        if (injectCounter >= 10) {
            evaluateScript(INJECT_SCRIPT);
            injectCounter = 0;
        }
        
        // Get song info from Redux store
        std::string infoScript = "(function(){"
            "var ps=window.__NCM_PLAY_STATE__;"
            "var s=window.__REDUX_STORE__;if(!s)return null;"
            "var p=s.getState().playing;if(!p)return null;"
            // Debug: check if progress callback is working
            "var monitorInstalled=window.__NCM_MONITOR_INSTALLED__||false;"
            "var psCurrent=ps?ps.current:null;"
            "var psLastUpdate=ps?ps.lastUpdate:null;"
            // Get current time from multiple sources
            "var cur=0;"
            "if(ps&&ps.current&&ps.current>0){cur=ps.current;}"  // from callback, already in seconds
            "else if(p.restoreResource&&p.restoreResource.current){cur=p.restoreResource.current/1000;}"  // from store, in ms
            "else if(p.resourceTrackId&&p.resourceTrackId.startTime){cur=p.resourceTrackId.startTime/1000;}"  // another possible source
            "return JSON.stringify({"
            "playId:p.resourceTrackId||p.playId||'',"
            "title:p.resourceName||'',"
            "artist:(p.resourceArtists||[]).map(function(a){return a.name||a;}).join(' / '),"
            "duration:p.resourceDuration||0,"  // in seconds
            "isPlaying:ps&&ps.isPlaying!==undefined?ps.isPlaying:(p.playingState===1),"
            "current:cur,"
            "debugMonitor:monitorInstalled,"
            "debugPsCurrent:psCurrent,"
            "debugLastUpdate:psLastUpdate"
            "});})()";
        
        std::string info = evaluateScript(infoScript);
        
        // Debug output to file
        if (!info.empty() && info != "null") {
            std::string dbgMsg = "[Netease] Raw: " + info;
            DebugLog(dbgMsg.c_str());
        }
        
        if (info.empty() || info == "null") {
            failCount++;
            DebugLog("[Netease] info is empty or null");
            continue;
        }
        failCount = 0;
        
        parseState(info);
        
        // Fetch lyrics if playId changed
        if (!currentPlayId_.empty() && currentPlayId_ != lastFetchedPlayId_) {
            // Clear old lyrics when song changes
            songInfo_.lyrics.clear();
            fetchLyrics(currentPlayId_);
        }
        
        if (callback_) callback_(songInfo_);
    }
    
    if (failCount >= 10) {
        connected_ = false;
        songInfo_.title = L"\x8FDE\x63A5\x5DF2\x65AD\x5F00";
        songInfo_.artist = L"";
        songInfo_.hasData = false;
        if (callback_) callback_(songInfo_);
    }
}

bool NeteaseWS::attachToOrpheusPage() {
    // Send Target.getTargets
    std::string result = sendMessage("{\"id\":" + std::to_string(msgId_++) + 
                                      ",\"method\":\"Target.getTargets\",\"params\":{}}");
    
    if (result.empty()) return false;
    
    // Find targetId
    std::string targetId;
    size_t pos = 0;
    while ((pos = result.find("\"targetId\"", pos)) != std::string::npos) {
        size_t idStart = result.find("\"", pos + 11);
        if (idStart == std::string::npos) break;
        size_t idEnd = result.find("\"", idStart + 1);
        if (idEnd == std::string::npos) break;
        std::string tid = result.substr(idStart + 1, idEnd - idStart - 1);
        
        size_t blockStart = result.rfind("{", pos);
        size_t blockEnd = result.find("},", pos);
        if (blockEnd == std::string::npos) blockEnd = result.find("}", pos);
        std::string block = result.substr(blockStart, blockEnd - blockStart + 1);
        
        if (block.find("orpheus") != std::string::npos ||
            block.find("music.163") != std::string::npos) {
            targetId = tid;
            break;
        }
        
        if (block.find("\"type\":\"page\"") != std::string::npos) {
            if (targetId.empty()) targetId = tid;
        }
        
        pos = idEnd + 1;
    }
    
    if (targetId.empty()) return false;
    
    // Attach with flatten: false
    // Important: The response comes as Target.attachedToTarget EVENT, not direct response!
    std::string msg = "{\"id\":" + std::to_string(msgId_++) + 
                      ",\"method\":\"Target.attachToTarget\"" +
                      ",\"params\":{\"targetId\":\"" + targetId + "\",\"flatten\":false}}";
    
    HRESULT hr = WinHttpWebSocketSend(hWebSocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                       (PVOID)msg.c_str(), (DWORD)msg.length());
    if (FAILED(hr)) return false;
    
    // Wait for Target.attachedToTarget event (not direct response!)
    for (int retry = 0; retry < 10; retry++) {
        std::string response = receiveMessage();
        if (response.empty()) return false;
        
        // Check for attachedToTarget event
        if (response.find("\"method\":\"Target.attachedToTarget\"") != std::string::npos) {
            // Parse sessionId from this event
            size_t sessionPos = response.find("\"sessionId\"");
            if (sessionPos != std::string::npos) {
                size_t sessionStart = response.find("\"", sessionPos + 12);
                if (sessionStart != std::string::npos) {
                    size_t sessionEnd = response.find("\"", sessionStart + 1);
                    if (sessionEnd != std::string::npos) {
                        sessionId_ = response.substr(sessionStart + 1, sessionEnd - sessionStart - 1);
                        return !sessionId_.empty();
                    }
                }
            }
        }
    }
    
    return false;
}

std::string NeteaseWS::sendMessage(const std::string& msg) {
    if (!hWebSocket_) return "";
    
    HRESULT hr = WinHttpWebSocketSend(hWebSocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                       (PVOID)msg.c_str(), (DWORD)msg.length());
    if (FAILED(hr)) return "";
    
    return receiveMessage();
}

std::string NeteaseWS::evaluateScript(const std::string& script) {
    if (!hWebSocket_) return "";
    
    // Escape script for JSON
    std::string escaped;
    for (char c : script) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\r') continue;
        else escaped += c;
    }
    
    int innerId = msgId_++;
    
    // Build the Runtime.evaluate command
    std::string evalCmd = "{\"id\":" + std::to_string(innerId) + 
                          ",\"method\":\"Runtime.evaluate\"" +
                          ",\"params\":{\"expression\":\"" + escaped + "\",\"returnByValue\":true}}";
    
    if (sessionId_.empty()) {
        // Direct command (no session) - shouldn't happen for Netease
        std::string result = sendMessage(evalCmd);
        return parseResultValue(result, innerId);
    }
    
    // Send via Target.sendMessageToTarget
    // Escape the inner command for JSON
    std::string escapedInner;
    for (char c : evalCmd) {
        if (c == '"') escapedInner += "\\\"";
        else if (c == '\\') escapedInner += "\\\\";
        else if (c == '\n') escapedInner += "\\n";
        else escapedInner += c;
    }
    
    int outerId = msgId_++;
    std::string msg = "{\"id\":" + std::to_string(outerId) + 
                      ",\"method\":\"Target.sendMessageToTarget\"" +
                      ",\"params\":{\"sessionId\":\"" + sessionId_ + "\"" +
                      ",\"message\":\"" + escapedInner + "\"}}";
    
    // Send the message
    HRESULT hr = WinHttpWebSocketSend(hWebSocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                       (PVOID)msg.c_str(), (DWORD)msg.length());
    if (FAILED(hr)) return "";
    
    // Wait for response with timeout
    DWORD startTime = GetTickCount();
    while (GetTickCount() - startTime < 5000) {  // 5 second timeout
        std::string response = receiveMessage();
        if (response.empty()) return "";
        
        // Check for Target.receivedMessageFromTarget event
        if (response.find("\"method\":\"Target.receivedMessageFromTarget\"") != std::string::npos) {
            // Extract the message content
            size_t msgPos = response.find("\"message\":\"");
            if (msgPos != std::string::npos) {
                size_t msgStart = msgPos + 11;
                std::string unescaped;
                bool inEscape = false;
                
                for (size_t i = msgStart; i < response.size(); i++) {
                    if (inEscape) {
                        switch (response[i]) {
                            case '"': unescaped += '"'; break;
                            case '\\': unescaped += '\\'; break;
                            case 'n': unescaped += '\n'; break;
                            case 'r': unescaped += '\r'; break;
                            case 't': unescaped += '\t'; break;
                            default: unescaped += response[i]; break;
                        }
                        inEscape = false;
                    } else if (response[i] == '\\') {
                        inEscape = true;
                    } else if (response[i] == '"') {
                        break;  // End of message string
                    } else {
                        unescaped += response[i];
                    }
                }
                
                // Check if this response matches our innerId
                if (unescaped.find("\"id\":" + std::to_string(innerId)) != std::string::npos) {
                    return parseResultValue(unescaped, innerId);
                }
            }
        }
        
        // Check for error response to our outer message
        if (response.find("\"id\":" + std::to_string(outerId)) != std::string::npos) {
            if (response.find("\"error\"") != std::string::npos) {
                return "";  // Error occurred
            }
            // This was just a success response, continue waiting for the actual result
        }
    }
    
    return "";  // Timeout
}

std::string NeteaseWS::parseResultValue(const std::string& result, int expectedId) {
    if (result.empty()) return "";
    if (result.find("\"error\"") != std::string::npos) return "";
    
    // Find the innermost "value" field (after "result":{"result":{"value":...)
    // We need to find the last occurrence of "value" before the end
    
    // Look for pattern: "value":"..." or "value":{...} or "value":number/boolean
    size_t valuePos = result.rfind("\"value\":");
    if (valuePos == std::string::npos) return "";
    
    size_t colonPos = valuePos + 8;
    size_t start = result.find_first_not_of(" \t\n", colonPos);
    if (start == std::string::npos) return "";
    
    if (result[start] == '"') {
        // String value - extract until matching quote
        start++;
        std::string value;
        for (size_t i = start; i < result.size(); i++) {
            if (result[i] == '\\' && i + 1 < result.size()) {
                char next = result[i + 1];
                if (next == '"') { value += '"'; i++; }
                else if (next == '\\') { value += '\\'; i++; }
                else if (next == 'n') { value += '\n'; i++; }
                else if (next == 'r') { value += '\r'; i++; }
                else if (next == 't') { value += '\t'; i++; }
                else { value += result[i]; }
            } else if (result[i] == '"') {
                break;
            } else {
                value += result[i];
            }
        }
        return value;
    }
    
    // Non-string value
    size_t end = result.find_first_of(",} \t\n", start);
    if (end == std::string::npos) end = result.size();
    
    return result.substr(start, end - start);
}

void NeteaseWS::parseState(const std::string& json) {
    auto extractString = [&json](const char* key) -> std::string {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        
        size_t colonPos = json.find(":", pos + search.length());
        if (colonPos == std::string::npos) return "";
        
        size_t start = json.find("\"", colonPos + 1);
        if (start == std::string::npos) {
            // Maybe it's a number or null
            size_t numStart = json.find_first_not_of(" \t\n", colonPos + 1);
            if (numStart != std::string::npos && json[numStart] != 'n') {
                size_t numEnd = json.find_first_of(",} \t\n", numStart);
                return json.substr(numStart, numEnd - numStart);
            }
            return "";
        }
        
        size_t end = start + 1;
        while (end < json.size()) {
            if (json[end] == '\\' && end + 1 < json.size()) {
                end += 2;
            } else if (json[end] == '"') {
                break;
            } else {
                end++;
            }
        }
        
        std::string value = json.substr(start + 1, end - start - 1);
        // Unescape JSON string
        std::string unescaped;
        for (size_t i = 0; i < value.size(); i++) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                char next = value[i + 1];
                if (next == '"') { unescaped += '"'; i++; }
                else if (next == '\\') { unescaped += '\\'; i++; }
                else if (next == 'n') { unescaped += '\n'; i++; }
                else if (next == 'r') { unescaped += '\r'; i++; }
                else if (next == 't') { unescaped += '\t'; i++; }
                else if (next == 'u' && i + 5 < value.size()) {
                    // \uXXXX - hex starts at i+2, not i+1
                    std::string hex = value.substr(i + 2, 4);
                    try {
                        int cp = std::stoi(hex, nullptr, 16);
                        if (cp < 0x80) { unescaped += (char)cp; }
                        else if (cp < 0x800) {
                            unescaped += (char)(0xC0 | (cp >> 6));
                            unescaped += (char)(0x80 | (cp & 0x3F));
                        } else {
                            unescaped += (char)(0xE0 | (cp >> 12));
                            unescaped += (char)(0x80 | ((cp >> 6) & 0x3F));
                            unescaped += (char)(0x80 | (cp & 0x3F));
                        }
                        i += 5;  // Skip \uXXXX (5 more chars)
                    } catch (...) { unescaped += value[i]; }
                }
                else unescaped += value[i];
            } else {
                unescaped += value[i];
            }
        }
        return unescaped;
    };
    
    auto extractNumber = [&json](const char* key) -> double {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return 0;
        
        size_t colonPos = json.find(":", pos + search.length());
        if (colonPos == std::string::npos) return 0;
        
        size_t start = json.find_first_of("0123456789.-", colonPos);
        if (start == std::string::npos) return 0;
        
        size_t end = start + 1;
        while (end < json.size() && (json[end] == '.' || isdigit(json[end]))) {
            end++;
        }
        
        try {
            return std::stod(json.substr(start, end - start));
        } catch (...) {
            return 0;
        }
    };
    
    auto extractBool = [&json](const char* key) -> bool {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return false;
        
        size_t colonPos = json.find(":", pos + search.length());
        if (colonPos == std::string::npos) return false;
        
        size_t start = json.find_first_not_of(" \t\n", colonPos + 1);
        if (start == std::string::npos) return false;
        
        return json[start] == 't';
    };
    
    // Parse from json parameter
    if (!json.empty() && json != "null") {
        songInfo_.hasData = true;
        
        std::string titleUtf8 = extractString("title");
        std::string artistUtf8 = extractString("artist");
        
        songInfo_.title = Utf8ToWstring(titleUtf8);
        songInfo_.artist = Utf8ToWstring(artistUtf8);
        currentPlayId_ = extractString("playId");
        // Note: Netease duration is already in seconds, not milliseconds!
        songInfo_.duration = extractNumber("duration");
        songInfo_.isPlaying = extractBool("isPlaying");
        songInfo_.currentTime = extractNumber("current");  // in seconds
        
        // Debug output with wide string
        wchar_t wdbg[512];
        swprintf_s(wdbg, L"[Netease] Parsed: title=%s, duration=%.1f, current=%.1f", 
                   songInfo_.title.c_str(), songInfo_.duration, songInfo_.currentTime);
        DebugLog(wdbg);
    }
}

std::wstring NeteaseWS::Utf8ToWstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &result[0], len);
    return result;
}

// HTTP GET request for lyrics API
std::string NeteaseWS::httpGet(const std::string& url) {
    // Parse URL
    std::string host, path;
    size_t hostStart = url.find("://");
    if (hostStart != std::string::npos) {
        hostStart += 3;
        size_t pathStart = url.find("/", hostStart);
        if (pathStart != std::string::npos) {
            host = url.substr(hostStart, pathStart - hostStart);
            path = url.substr(pathStart);
        } else {
            host = url.substr(hostStart);
            path = "/";
        }
    }
    
    if (host.empty()) return "";
    
    // Convert host to wide string
    std::wstring whost(host.begin(), host.end());
    
    HINTERNET hSession = WinHttpOpen(L"NeteaseLyrics/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, whost.c_str(), INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    std::wstring wpath(path.begin(), path.end());
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // Add headers for Netease API
    std::wstring headers = L"Cookie: appver=2.0.2\r\n";
    WinHttpSendRequest(hRequest, headers.c_str(), (DWORD)headers.length(),
                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    WinHttpReceiveResponse(hRequest, NULL);
    
    std::string response;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        char* buffer = new char[dwSize + 1];
        ZeroMemory(buffer, dwSize + 1);
        if (WinHttpReadData(hRequest, buffer, dwSize, &dwDownloaded)) {
            response += buffer;
        }
        delete[] buffer;
    } while (dwSize > 0);
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}

// Parse LRC format lyrics
void NeteaseWS::parseLrcLyrics(const std::string& lrc, const std::string& tLrc) {
    if (lrc.empty()) return;
    
    songInfo_.lyrics.clear();
    
    // Parse time and text from LRC format: [mm:ss.xx]text
    std::vector<std::pair<double, std::wstring>> lines;
    std::vector<std::pair<double, std::wstring>> tlines;
    
    // LRC metadata tags to skip
    auto isMetadataTag = [](const std::string& tag) -> bool {
        return tag == "ti" || tag == "ar" || tag == "al" || tag == "by" ||
               tag == "offset" || tag == "re" || tag == "ve" || tag == "length";
    };
    
    // Regex-like parsing for [mm:ss.xx] or [mm:ss.xxx]
    auto parseLrcLines = [&lines, &isMetadataTag](const std::string& text) {
        size_t pos = 0;
        while ((pos = text.find('[', pos)) != std::string::npos) {
            size_t endBracket = text.find(']', pos);
            if (endBracket == std::string::npos) { pos++; continue; }
            
            std::string timeStr = text.substr(pos + 1, endBracket - pos - 1);
            size_t colonPos = timeStr.find(':');
            if (colonPos == std::string::npos) { pos++; continue; }
            
            // Check if this is a metadata tag like [ti:歌名] or [ar:歌手]
            std::string prefix = timeStr.substr(0, colonPos);
            bool isMeta = false;
            for (char& c : prefix) c = tolower(c);
            if (isMetadataTag(prefix)) {
                isMeta = true;
            }
            // Also check if prefix contains non-digit characters (not a valid time)
            for (char c : prefix) {
                if (!isdigit(c) && c != '.') {
                    isMeta = true;
                    break;
                }
            }
            if (isMeta) {
                pos = endBracket + 1;
                continue;
            }
            
            try {
                double minutes = std::stod(timeStr.substr(0, colonPos));
                double seconds = std::stod(timeStr.substr(colonPos + 1));
                double timeMs = (minutes * 60 + seconds) * 1000;
                
                size_t textStart = endBracket + 1;
                size_t textEnd = text.find('\n', textStart);
                if (textEnd == std::string::npos) textEnd = text.length();
                std::string lyricText = text.substr(textStart, textEnd - textStart);
                
                // Trim
                while (!lyricText.empty() && (lyricText[0] == ' ' || lyricText[0] == '\r')) {
                    lyricText = lyricText.substr(1);
                }
                while (!lyricText.empty() && (lyricText.back() == ' ' || lyricText.back() == '\r')) {
                    lyricText.pop_back();
                }
                
                // Remove any remaining time tags from lyric text
                // Format: [mm:ss.xx] or [mm:ss:xx] or [mm:ss]
                size_t tagPos = 0;
                while ((tagPos = lyricText.find('[', tagPos)) != std::string::npos) {
                    size_t tagEnd = lyricText.find(']', tagPos);
                    if (tagEnd == std::string::npos) break;
                    
                    std::string tagContent = lyricText.substr(tagPos + 1, tagEnd - tagPos - 1);
                    size_t tagColonPos = tagContent.find(':');
                    
                    // Check if this looks like a time tag
                    bool isTimeTag = false;
                    if (tagColonPos != std::string::npos) {
                        isTimeTag = true;
                        // Check if content before and after colon are numeric
                        for (size_t i = 0; i < tagContent.size(); i++) {
                            char c = tagContent[i];
                            if (i == tagColonPos) continue;
                            if (!isdigit(c) && c != '.' && c != '-' && c != ' ') {
                                isTimeTag = false;
                                break;
                            }
                        }
                    }
                    
                    if (isTimeTag) {
                        // Remove the time tag
                        lyricText.erase(tagPos, tagEnd - tagPos + 1);
                        // Remove leading space after tag removal
                        while (tagPos < lyricText.size() && lyricText[tagPos] == ' ') {
                            lyricText.erase(tagPos, 1);
                        }
                        // Continue checking from same position
                    } else {
                        tagPos = tagEnd + 1;
                    }
                }
                
                if (!lyricText.empty()) {
                    std::wstring wtext;
                    int len = MultiByteToWideChar(CP_UTF8, 0, lyricText.c_str(), -1, NULL, 0);
                    if (len > 0) {
                        wtext.resize(len - 1);
                        MultiByteToWideChar(CP_UTF8, 0, lyricText.c_str(), -1, &wtext[0], len);
                    }
                    lines.push_back({timeMs, wtext});
                }
            } catch (...) {}
            pos = endBracket + 1;
        }
    };
    
    parseLrcLines(lrc);
    
    // Parse translation
    if (!tLrc.empty()) {
        auto parseTLrc = [&tlines, &isMetadataTag](const std::string& text) {
            size_t pos = 0;
            while ((pos = text.find('[', pos)) != std::string::npos) {
                size_t endBracket = text.find(']', pos);
                if (endBracket == std::string::npos) { pos++; continue; }
                
                std::string timeStr = text.substr(pos + 1, endBracket - pos - 1);
                size_t colonPos = timeStr.find(':');
                if (colonPos == std::string::npos) { pos++; continue; }
                
                // Check if this is a metadata tag (same logic as parseLrcLines)
                std::string prefix = timeStr.substr(0, colonPos);
                bool isMeta = false;
                for (char& c : prefix) c = tolower(c);
                if (isMetadataTag(prefix)) {
                    isMeta = true;
                }
                for (char c : prefix) {
                    if (!isdigit(c) && c != '.') {
                        isMeta = true;
                        break;
                    }
                }
                if (isMeta) {
                    pos = endBracket + 1;
                    continue;
                }
                
                try {
                    double minutes = std::stod(timeStr.substr(0, colonPos));
                    double seconds = std::stod(timeStr.substr(colonPos + 1));
                    double timeMs = (minutes * 60 + seconds) * 1000;
                    
                    size_t textStart = endBracket + 1;
                    size_t textEnd = text.find('\n', textStart);
                    if (textEnd == std::string::npos) textEnd = text.length();
                    std::string lyricText = text.substr(textStart, textEnd - textStart);
                    
                    while (!lyricText.empty() && (lyricText[0] == ' ' || lyricText[0] == '\r')) {
                        lyricText = lyricText.substr(1);
                    }
                    while (!lyricText.empty() && (lyricText.back() == ' ' || lyricText.back() == '\r')) {
                        lyricText.pop_back();
                    }
                    
                    // Remove any remaining time tags from translation text
                    size_t tagPos = 0;
                    while ((tagPos = lyricText.find('[', tagPos)) != std::string::npos) {
                        size_t tagEnd = lyricText.find(']', tagPos);
                        if (tagEnd == std::string::npos) break;
                        
                        std::string tagContent = lyricText.substr(tagPos + 1, tagEnd - tagPos - 1);
                        size_t tagColonPos = tagContent.find(':');
                        
                        bool isTimeTag = false;
                        if (tagColonPos != std::string::npos) {
                            isTimeTag = true;
                            for (size_t i = 0; i < tagContent.size(); i++) {
                                char c = tagContent[i];
                                if (i == tagColonPos) continue;
                                if (!isdigit(c) && c != '.' && c != '-' && c != ' ') {
                                    isTimeTag = false;
                                    break;
                                }
                            }
                        }
                        
                        if (isTimeTag) {
                            lyricText.erase(tagPos, tagEnd - tagPos + 1);
                            while (tagPos < lyricText.size() && lyricText[tagPos] == ' ') {
                                lyricText.erase(tagPos, 1);
                            }
                        } else {
                            tagPos = tagEnd + 1;
                        }
                    }
                    
                    if (!lyricText.empty()) {
                        std::wstring wtext;
                        int len = MultiByteToWideChar(CP_UTF8, 0, lyricText.c_str(), -1, NULL, 0);
                        if (len > 0) {
                            wtext.resize(len - 1);
                            MultiByteToWideChar(CP_UTF8, 0, lyricText.c_str(), -1, &wtext[0], len);
                        }
                        tlines.push_back({timeMs, wtext});
                    }
                } catch (...) {}
                pos = endBracket + 1;
            }
        };
        parseTLrc(tLrc);
    }
    
    // Build translation map - use long long as key for exact timestamp matching
    std::map<long long, std::wstring> tMap;
    for (const auto& tl : tlines) {
        tMap[static_cast<long long>(tl.first)] = tl.second;
    }
    
    // Create LyricLine objects
    for (const auto& line : lines) {
        // 跳过空歌词行
        if (line.second.empty()) continue;
        
        // 跳过只包含时间戳的行（去掉时间戳后为空）
        std::wstring trimmedText = line.second;
        while (!trimmedText.empty() && (trimmedText[0] == L' ' || trimmedText[0] == L'\t')) {
            trimmedText = trimmedText.substr(1);
        }
        if (trimmedText.empty()) continue;
        
        LyricLine ll;
        ll.startTime = static_cast<int>(line.first);
        ll.text = line.second;
        auto it = tMap.find(static_cast<long long>(line.first));
        if (it != tMap.end()) {
            ll.translation = it->second;
        }
        songInfo_.lyrics.push_back(ll);
    }
    
    std::wstring dbgMsg = L"[Netease] Lyrics parsed, lines count: " + std::to_wstring(songInfo_.lyrics.size());
    DebugLog(dbgMsg.c_str());
}

// Fetch lyrics from Netease API
void NeteaseWS::fetchLyrics(const std::string& playId) {
    if (playId.empty() || playId == lastFetchedPlayId_) return;
    
    DebugLog(L"[Netease] Fetching lyrics for playId...");
    lastFetchedPlayId_ = playId;
    
    std::string url = "http://music.163.com/api/song/lyric?id=" + playId + "&lv=1&tv=1";
    DebugLog(L"[Netease] Lyrics API URL constructed");
    
    std::string response = httpGet(url);
    
    if (response.empty()) {
        DebugLog(L"[Netease] Lyrics API returned empty");
        return;
    }
    
    DebugLog(L"[Netease] Lyrics API response received");
    
    // Log first 200 chars of response
    std::string preview = response.substr(0, (std::min)((size_t)200, response.length()));
    std::string logMsg = "[Netease] Response preview: " + preview;
    DebugLog(logMsg.c_str());
    
    // Extract lrc and tlyric from JSON
    auto extractJsonString = [&response](const char* key) -> std::string {
        std::string search = std::string("\"") + key + "\"";
        size_t pos = response.find(search);
        if (pos == std::string::npos) return "";
        
        size_t colonPos = response.find(":", pos + search.length());
        if (colonPos == std::string::npos) return "";
        
        size_t start = response.find("\"", colonPos + 1);
        if (start == std::string::npos) return "";
        
        size_t end = start + 1;
        while (end < response.size()) {
            if (response[end] == '\\' && end + 1 < response.size()) {
                end += 2;
            } else if (response[end] == '"') {
                break;
            } else {
                end++;
            }
        }
        
        std::string value = response.substr(start + 1, end - start - 1);
        
        // Unescape
        std::string unescaped;
        for (size_t i = 0; i < value.size(); i++) {
            if (value[i] == '\\' && i + 1 < value.size()) {
                char next = value[i + 1];
                if (next == '"') { unescaped += '"'; i++; }
                else if (next == '\\') { unescaped += '\\'; i++; }
                else if (next == 'n') { unescaped += '\n'; i++; }
                else if (next == 'r') { unescaped += '\r'; i++; }
                else if (next == 'u' && i + 5 < value.size()) {
                    std::string hex = value.substr(i + 2, 4);
                    try {
                        int cp = std::stoi(hex, nullptr, 16);
                        if (cp < 0x80) { unescaped += (char)cp; }
                        else if (cp < 0x800) {
                            unescaped += (char)(0xC0 | (cp >> 6));
                            unescaped += (char)(0x80 | (cp & 0x3F));
                        } else {
                            unescaped += (char)(0xE0 | (cp >> 12));
                            unescaped += (char)(0x80 | ((cp >> 6) & 0x3F));
                            unescaped += (char)(0x80 | (cp & 0x3F));
                        }
                        i += 5;
                    } catch (...) { unescaped += value[i]; }
                }
                else unescaped += value[i];
            } else {
                unescaped += value[i];
            }
        }
        return unescaped;
    };
    
    std::string lrc = extractJsonString("lyric");
    std::string tLrc = extractJsonString("tlyric");
    
    parseLrcLyrics(lrc, tLrc);
}

} // namespace moekoe





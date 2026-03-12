// moekoe_ws.cpp - MoeKoeMusic WebSocket Client
#define _CRT_SECURE_NO_WARNINGS

#include "moekoe_ws.h"
#include <sstream>
#include <random>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

// 外部变量：主程序的OSC发送时间戳（用于限流）- 全局命名空间
extern DWORD g_lastOscSendTime;

namespace moekoe {

// Helper function to get temp log file path
static std::string GetDebugLogPath(const char* filename) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\");
    strcat_s(tempPath, filename);
    return std::string(tempPath);
}

// Check and rotate log if too large (max 1MB)
static void CheckLogSize(const std::string& logPath) {
    FILE* f = fopen(logPath.c_str(), "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size > 1024 * 1024) {
            DeleteFileA(logPath.c_str());
        }
    }
}

// Clean all moekoe debug logs (called at program startup)
static void CleanMoekoeLogs() {
    std::string debugLog = GetDebugLogPath("moekoe_debug.log");
    std::string krcLog = GetDebugLogPath("moekoe_krc.log");
    std::string oscLog = GetDebugLogPath("moekoe_osc.log");
    DeleteFileA(debugLog.c_str());
    DeleteFileA(krcLog.c_str());
    DeleteFileA(oscLog.c_str());
}

static std::string base64Encode(const std::string& input) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int val = 0, bits = -6;
    for (unsigned char c : input) {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0) {
            result += chars[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }
    if (bits > -6) result += chars[((val << 8) >> (bits + 8)) & 0x3F];
    while (result.size() % 4) result += '=';
    return result;
}

static std::string sha1Hash(const std::string& input) {
    unsigned int h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t bitLen = input.size() * 8;
    
    std::vector<unsigned char> data(input.begin(), input.end());
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    
    // Append original length in bits as 64-bit big-endian
    for (int i = 7; i >= 0; i--) {
        data.push_back((bitLen >> (i * 8)) & 0xFF);
    }
    
    auto rotl = [](unsigned int x, int n) { return (x << n) | (x >> (32 - n)); };
    
    for (size_t i = 0; i < data.size(); i += 64) {
        unsigned int w[80];
        for (int j = 0; j < 16; j++) {
            w[j] = ((unsigned int)data[i+j*4] << 24) | ((unsigned int)data[i+j*4+1] << 16) | 
                   ((unsigned int)data[i+j*4+2] << 8) | (unsigned int)data[i+j*4+3];
        }
        for (int j = 16; j < 80; j++) w[j] = rotl(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);
        
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int j = 0; j < 80; j++) {
            unsigned int f, k;
            if (j < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (j < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            unsigned int tmp = rotl(a, 5) + f + e + k + w[j];
            e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    
    std::string result;
    for (int i = 0; i < 5; i++) {
        result += (char)(h[i] >> 24);
        result += (char)(h[i] >> 16);
        result += (char)(h[i] >> 8);
        result += (char)h[i];
    }
    return result;
}

MoeKoeWS::MoeKoeWS(const std::string& host, int port) : host_(host), port_(port) {}

MoeKoeWS::~MoeKoeWS() {
    disconnect();
}

bool MoeKoeWS::connect() {
    if (running_) return connected_;
    
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    strcat_s(tempPath, "\\moekoe_debug.log");
    
    // Check log size before writing
    CheckLogSize(tempPath);
    FILE* f = fopen(tempPath, "a");
    if (f) fprintf(f, "=== CONNECT START ===\n");
    
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (f) { fprintf(f, "WSAStartup failed\n"); fclose(f); }
        return false;
    }
    if (f) fprintf(f, "WSAStartup OK\n");
    
    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) { 
        if (f) { fprintf(f, "socket() failed\n"); fclose(f); }
        WSACleanup(); 
        return false; 
    }
    if (f) fprintf(f, "socket() OK\n");
    
    DWORD timeout = 3000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
    
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    
    if (::connect(sock_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        if (f) { fprintf(f, "connect() failed: %d\n", WSAGetLastError()); fclose(f); }
        closesocket(sock_);
        WSACleanup();
        return false;
    }
    if (f) fprintf(f, "TCP connect OK\n");
    
    // WebSocket handshake
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    std::string key(16, 0);
    for (int i = 0; i < 16; i++) key[i] = dis(gen);
    std::string keyB64 = base64Encode(key);
    
    std::string handshake = 
        "GET / HTTP/1.1\r\n"
        "Host: " + host_ + ":" + std::to_string(port_) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + keyB64 + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    
    send(sock_, handshake.c_str(), (int)handshake.size(), 0);
    
    char response[1024];
    int len = recv(sock_, response, sizeof(response) - 1, 0);
    if (len <= 0) {
        if (f) { fprintf(f, "handshake recv failed: %d\n", WSAGetLastError()); fclose(f); }
        closesocket(sock_);
        WSACleanup();
        return false;
    }
    response[len] = 0;
    if (f) fprintf(f, "Handshake response: %s\n", response);
    
    std::string expected = base64Encode(sha1Hash(keyB64 + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    if (f) fprintf(f, "Key: %s\nExpected: %s\n", keyB64.c_str(), expected.c_str());
    if (!strstr(response, "101") || !strstr(response, expected.c_str())) {
        if (f) { fprintf(f, "handshake validation failed\n"); fclose(f); }
        closesocket(sock_);
        WSACleanup();
        return false;
    }
    
    if (f) fprintf(f, "WebSocket connected! Starting thread...\n");
    fclose(f);
    
    connected_ = true;
    running_ = true;
    thread_ = std::thread(&MoeKoeWS::run, this);
    return true;
}

void MoeKoeWS::disconnect() {
    running_ = false;
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) thread_.join();
    connected_ = false;
    WSACleanup();
}

void MoeKoeWS::run() {
    std::vector<char> buffer;
    char chunk[4096];
    
    std::string logPath = GetDebugLogPath("moekoe_debug.log");
    FILE* f = fopen(logPath.c_str(), "a");
    if (f) { fprintf(f, "=== RUN THREAD STARTED ===\n"); fclose(f); }
    
    while (running_) {
        // Auto-reconnect if not connected
        if (!connected_) {
            f = fopen(logPath.c_str(), "a");
            if (f) { fprintf(f, "Connection lost, waiting 2s before reconnect...\n"); fclose(f); }
            
            // Wait before reconnecting
            for (int i = 0; i < 20 && running_; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!running_) break;
            
            // Close old socket
            if (sock_ != INVALID_SOCKET) {
                closesocket(sock_);
                sock_ = INVALID_SOCKET;
            }
            
            // Try to reconnect
            f = fopen(logPath.c_str(), "a");
            if (f) { fprintf(f, "Attempting reconnect...\n"); fclose(f); }
            
            // Reconnect logic (simplified - just create new socket)
            sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (sock_ != INVALID_SOCKET) {
                DWORD timeout = 3000;
                setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
                setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));
                
                sockaddr_in addr = {};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port_);
                inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
                
                if (::connect(sock_, (sockaddr*)&addr, sizeof(addr)) == 0) {
                    // WebSocket handshake
                    std::random_device rd;
                    std::mt19937 gen(rd());
                    std::uniform_int_distribution<> dis(0, 255);
                    std::string key(16, 0);
                    for (int i = 0; i < 16; i++) key[i] = dis(gen);
                    std::string keyB64 = base64Encode(key);
                    
                    std::string handshake = 
                        "GET / HTTP/1.1\r\n"
                        "Host: " + host_ + ":" + std::to_string(port_) + "\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Key: " + keyB64 + "\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n";
                    
                    send(sock_, handshake.c_str(), (int)handshake.length(), 0);
                    
                    char resp[1024];
                    int respLen = recv(sock_, resp, sizeof(resp) - 1, 0);
                    if (respLen > 0) {
                        resp[respLen] = 0;
                        if (strstr(resp, "101") != nullptr) {
                            connected_ = true;
                            buffer.clear();
                            // Don't overwrite song info on reconnect - keep existing data
                            // Just mark as connected, next message will update song info
                            if (callback_) callback_(songInfo_);
                            f = fopen(logPath.c_str(), "a");
                            if (f) { fprintf(f, "Reconnect successful!\n"); fclose(f); }
                        }
                    }
                }
            }
            
            if (!connected_) {
                f = fopen(logPath.c_str(), "a");
                if (f) { fprintf(f, "Reconnect failed, will retry...\n"); fclose(f); }
                continue;
            }
        }
        
        int len = recv(sock_, chunk, sizeof(chunk), 0);
        if (len <= 0) {
            f = fopen(logPath.c_str(), "a");
            if (f) { fprintf(f, "recv failed or closed: %d\n", len); fclose(f); }
            connected_ = false;
            buffer.clear();
            continue;
        }
        
        f = fopen(logPath.c_str(), "a");
        if (f) { fprintf(f, "recv %d bytes\n", len); fclose(f); }
        
        buffer.insert(buffer.end(), chunk, chunk + len);
        
        while (buffer.size() >= 2) {
            bool fin = buffer[0] & 0x80;
            int opcode = buffer[0] & 0x0F;
            bool masked = buffer[1] & 0x80;
            unsigned long long payloadLen = buffer[1] & 0x7F;
            size_t headerSize = 2;
            
            if (payloadLen == 126) {
                if (buffer.size() < 4) break;
                payloadLen = ((unsigned short)buffer[2] << 8) | (unsigned char)buffer[3];
                headerSize = 4;
            } else if (payloadLen == 127) {
                if (buffer.size() < 10) break;
                payloadLen = 0;
                for (int i = 0; i < 8; i++) {
                    payloadLen = (payloadLen << 8) | (unsigned char)buffer[2 + i];
                }
                headerSize = 10;
            }
            
            if (masked) headerSize += 4;
            
            if (buffer.size() < headerSize + payloadLen) break;
            
            std::string payload;
            if (masked) {
                char mask[4] = {buffer[headerSize-4], buffer[headerSize-3], buffer[headerSize-2], buffer[headerSize-1]};
                for (size_t i = 0; i < payloadLen; i++) {
                    payload += buffer[headerSize + i] ^ mask[i % 4];
                }
            } else {
                payload.assign(buffer.data() + headerSize, payloadLen);
            }
            
            if (opcode == 1 || opcode == 2) {
                parseMessage(payload);
            }
            
            buffer.erase(buffer.begin(), buffer.begin() + headerSize + payloadLen);
        }
    }
}

// Parse LRC-style tags from lyricsData: [ar:Artist] [ti:Title] [total:123456]
static std::string extractTag(const std::string& lyricsData, const std::string& tag) {
    std::string search = "[" + tag + ":";
    size_t pos = lyricsData.find(search);
    if (pos == std::string::npos) return "";
    pos += search.length();
    size_t end = lyricsData.find("]", pos);
    if (end == std::string::npos) return "";
    return lyricsData.substr(pos, end - pos);
}

// Parse KRC lyrics format: [startTime,duration]<offset,dur,flag>char...
static std::vector<LyricLine> parseKRC(const std::string& krcData) {
    std::vector<LyricLine> lyrics;
    
    std::string krcLogPath = GetDebugLogPath("moekoe_krc.log");
    FILE* f = fopen(krcLogPath.c_str(), "a");
    
    // Split by lines
    std::string line;
    for (size_t i = 0; i < krcData.size(); i++) {
        if (krcData[i] == '\n' || i == krcData.size() - 1) {
            // Parse line
            // Format: [startTime,duration]<char1><char2>...
            if (!line.empty() && line[0] == '[') {
                size_t closeBracket = line.find(']');
                if (closeBracket != std::string::npos) {
                    // Extract timing: [startTime,duration]
                    std::string timing = line.substr(1, closeBracket - 1);
                    size_t comma = timing.find(',');
                    if (comma != std::string::npos) {
                        int startTime = 0, duration = 0;
                        try {
                            startTime = std::stoi(timing.substr(0, comma));
                            duration = std::stoi(timing.substr(comma + 1));
                        } catch (...) {}
                        
                        // Extract text by removing <...> timing tags
                        std::string text;
                        size_t pos = closeBracket + 1;
                        while (pos < line.size()) {
                            if (line[pos] == '<') {
                                // Skip timing tag until >
                                while (pos < line.size() && line[pos] != '>') pos++;
                                if (pos < line.size()) pos++;
                            } else {
                                text += line[pos++];
                            }
                        }
                        
                        // Debug: print raw line and extracted text
                        if (f && lyrics.size() < 5) {
                            fprintf(f, "Line: [%s]\n", line.c_str());
                            fprintf(f, "Text: [%s]\n", text.c_str());
                        }
                        
                        // Trim and filter metadata lines
                        // Remove leading/trailing whitespace
                        size_t start = text.find_first_not_of(" \t\r\n");
                        size_t end = text.find_last_not_of(" \t\r\n");
                        if (start != std::string::npos && end != std::string::npos) {
                            text = text.substr(start, end - start + 1);
                        }
                        
                        if (!text.empty() && startTime >= 0) {
                            // Skip metadata lines like "歌手 - 歌名" at the start
                            if (lyrics.empty() && startTime == 0) {
                                // Check if it's artist-title line (contains " - ")
                                if (text.find(" - ") != std::string::npos) {
                                    line.clear();
                                    continue;
                                }
                                // Also skip lines that look like metadata (short, no time)
                                if (text.length() < 5) {
                                    line.clear();
                                    continue;
                                }
                            }
                            
                            LyricLine lyric;
                            lyric.startTime = startTime;
                            lyric.duration = duration;
                            
                            // Convert UTF-8 to wstring with better error handling
                            int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
                            if (wlen > 0) {
                                lyric.text.resize(wlen - 1);
                                int converted = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &lyric.text[0], wlen);
                                
                                // If conversion failed or text is empty, skip this line
                                if (converted == 0 || lyric.text.empty()) {
                                    if (f) {
                                        fprintf(f, "Warning: UTF-8 conversion failed for text: [%s] (error: %d)\n", text.c_str(), GetLastError());
                                    }
                                    line.clear();
                                    continue;
                                }
                                
                                // Debug output
                                if (f && lyrics.size() < 5) {
                                    std::string utf8Text;
                                    int len = WideCharToMultiByte(CP_UTF8, 0, lyric.text.c_str(), -1, nullptr, 0, nullptr, nullptr);
                                    if (len > 0) {
                                        utf8Text.resize(len - 1);
                                        WideCharToMultiByte(CP_UTF8, 0, lyric.text.c_str(), -1, &utf8Text[0], len, nullptr, nullptr);
                                        fprintf(f, "Converted: [%s]\n", utf8Text.c_str());
                                    }
                                }
                            } else {
                                if (f) {
                                    fprintf(f, "Warning: Could not determine UTF-8 length for text: [%s]\n", text.c_str());
                                }
                                line.clear();
                                continue;
                            }
                            
                            lyrics.push_back(lyric);
                        }
                    }
                }
            }
            line.clear();
        } else if (krcData[i] != '\r') {
            line += krcData[i];
        }
    }
    
    if (f) fclose(f);
    return lyrics;
}

static double jsonGetDouble(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos = json.find(":", pos);
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' && 
           json[end] != ' ' && json[end] != '\n' && json[end] != '\r') {
        end++;
    }
    std::string val = json.substr(pos, end - pos);
    try { return std::stod(val); }
    catch (...) { return 0; }
}

static bool jsonGetBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find(":", pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    return json.substr(pos, 4) == "true";
}

void MoeKoeWS::parseMessage(const std::string& msg) {
    // Debug: write received message to file
    std::string logPath = GetDebugLogPath("moekoe_debug.log");
    FILE* f = fopen(logPath.c_str(), "a");
    if (f) {
        fprintf(f, "=== RECV ===\n%s\n", msg.c_str());
    }
    
    // Get message type
    size_t typePos = msg.find("\"type\"");
    if (typePos == std::string::npos) return;
    
    size_t typeStart = msg.find("\"", typePos + 7);
    if (typeStart == std::string::npos) return;
    typeStart++;
    size_t typeEnd = msg.find("\"", typeStart);
    if (typeEnd == std::string::npos) return;
    
    std::string type = msg.substr(typeStart, typeEnd - typeStart);
    
    if (type == "welcome") {
        // Only show "Connected" if we don't have song data yet
        {
            std::lock_guard<std::mutex> lock(songInfoMutex_);
            if (!songInfo_.hasData || songInfo_.title.empty()) {
                songInfo_.hasData = true;
                songInfo_.title = L"Connected";
                songInfo_.artist = L"Ready";
            }
        }
        if (callback_) {
            SongInfo info;
            {
                std::lock_guard<std::mutex> lock(songInfoMutex_);
                info = songInfo_;
            }
            callback_(info);
        }
    }
    else if (type == "lyrics") {
        // 使用局部变量收集解析结果，减少锁持有时间
        SongInfo localInfo;
        {
            std::lock_guard<std::mutex> lock(songInfoMutex_);
            localInfo = songInfo_;  // 复制当前状态
        }
        
        // Extract currentTime
        localInfo.currentTime = jsonGetDouble(msg, "currentTime");
        
        // Try to extract from currentSong first (more reliable)
        size_t csPos = msg.find("\"currentSong\"");
        if (csPos != std::string::npos) {
            // Find name
            size_t namePos = msg.find("\"name\"", csPos);
            if (namePos != std::string::npos) {
                size_t nameStart = msg.find("\"", namePos + 6);
                if (nameStart != std::string::npos) {
                    nameStart++;
                    size_t nameEnd = msg.find("\"", nameStart);
                    if (nameEnd != std::string::npos) {
                        std::string name = msg.substr(nameStart, nameEnd - nameStart);
                        localInfo.title = utf8ToWstring(name);
                    }
                }
            }
            // Find author
            size_t authorPos = msg.find("\"author\"", csPos);
            if (authorPos != std::string::npos) {
                size_t authorStart = msg.find("\"", authorPos + 8);
                if (authorStart != std::string::npos) {
                    authorStart++;
                    size_t authorEnd = msg.find("\"", authorStart);
                    if (authorEnd != std::string::npos) {
                        std::string author = msg.substr(authorStart, authorEnd - authorStart);
                        localInfo.artist = utf8ToWstring(author);
                    }
                }
            }
            // Find timeLength - extract value after the key
            size_t tlPos = msg.find("\"timeLength\"", csPos);
            if (tlPos != std::string::npos) {
                size_t colonPos = msg.find(":", tlPos);
                if (colonPos != std::string::npos) {
                    colonPos++;
                    // Skip whitespace
                    while (colonPos < msg.size() && (msg[colonPos] == ' ' || msg[colonPos] == '\t')) colonPos++;
                    // Find end of number
                    size_t endPos = colonPos;
                    while (endPos < msg.size() && (msg[endPos] >= '0' && msg[endPos] <= '9' || msg[endPos] == '.')) endPos++;
                    if (endPos > colonPos) {
                        std::string val = msg.substr(colonPos, endPos - colonPos);
                        try { localInfo.duration = std::stod(val); } catch (...) {}
                    }
                }
            }
            if (f) fprintf(f, "[DEBUG] timeLength from currentSong: %.2f\n", localInfo.duration);
            localInfo.hasData = true;
        }
        
        // Also check duration field (might be more accurate)
        size_t durPos = msg.find("\"duration\"");
        if (durPos != std::string::npos) {
            size_t colonPos = msg.find(":", durPos);
            if (colonPos != std::string::npos) {
                colonPos++;
                while (colonPos < msg.size() && (msg[colonPos] == ' ' || msg[colonPos] == '\t')) colonPos++;
                size_t endPos = colonPos;
                while (endPos < msg.size() && (msg[endPos] >= '0' && msg[endPos] <= '9' || msg[endPos] == '.')) endPos++;
                if (endPos > colonPos) {
                    std::string val = msg.substr(colonPos, endPos - colonPos);
                    try { 
                        localInfo.duration = std::stod(val);
                        if (f) fprintf(f, "[DEBUG] duration field: %.2f\n", localInfo.duration);
                    } catch (...) {}
                }
            }
        }
        
        // Extract lyricsData
        size_t ldPos = msg.find("\"lyricsData\"");
        if (ldPos != std::string::npos) {
            size_t ldStart = msg.find("\"", ldPos + 12);
            if (ldStart != std::string::npos) {
                ldStart++;
                size_t ldEnd = msg.find("\"", ldStart);
                if (ldEnd != std::string::npos) {
                    std::string lyricsData = msg.substr(ldStart, ldEnd - ldStart);
                    
                    // Unescape JSON strings (including \uXXXX Unicode escapes)
                    std::string unescaped;
                    for (size_t i = 0; i < lyricsData.size(); i++) {
                        if (lyricsData[i] == '\\' && i + 1 < lyricsData.size()) {
                            switch (lyricsData[i + 1]) {
                                case 'n': unescaped += '\n'; i++; break;
                                case 'r': unescaped += '\r'; i++; break;
                                case 't': unescaped += '\t'; i++; break;
                                case '\\': unescaped += '\\'; i++; break;
                                case '"': unescaped += '"'; i++; break;
                                case 'u': {
                                    // Handle \uXXXX Unicode escape (including surrogate pairs)
                                    // Need at least 6 characters: \uXXXX
                                    if (i + 5 < lyricsData.size()) {
                                        // Validate hex characters first
                                        bool validHex = true;
                                        for (int j = 0; j < 4; j++) {
                                            char c = lyricsData[i + 2 + j];
                                            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                                                validHex = false;
                                                break;
                                            }
                                        }
                                        
                                        if (validHex) {
                                            try {
                                                std::string hex = lyricsData.substr(i + 2, 4);
                                                unsigned int codepoint = std::stoul(hex, nullptr, 16);
                                                
                                                // Check for surrogate pair (high surrogate: D800-DBFF)
                                                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                                                    // Look for low surrogate (DC00-DFFF)
                                                    // Need \uXXXX\uXXXX = 12 characters total
                                                    if (i + 11 < lyricsData.size() && 
                                                        lyricsData[i + 6] == '\\' && 
                                                        lyricsData[i + 7] == 'u') {
                                                        // Validate second hex sequence
                                                        bool validHex2 = true;
                                                        for (int j = 0; j < 4; j++) {
                                                            char c = lyricsData[i + 8 + j];
                                                            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                                                                validHex2 = false;
                                                                break;
                                                            }
                                                        }
                                                        
                                                        if (validHex2) {
                                                            std::string hex2 = lyricsData.substr(i + 8, 4);
                                                            unsigned int lowSurrogate = std::stoul(hex2, nullptr, 16);
                                                            if (lowSurrogate >= 0xDC00 && lowSurrogate <= 0xDFFF) {
                                                                // Combine surrogate pair
                                                                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (lowSurrogate - 0xDC00);
                                                                i += 11;  // Skip both \uXXXX\uXXXX
                                                            } else {
                                                                // Invalid low surrogate, use replacement character for high surrogate
                                                                codepoint = 0xFFFD;  // Unicode replacement character
                                                                i += 5;
                                                            }
                                                        } else {
                                                            // Invalid hex in low surrogate, use replacement character
                                                            codepoint = 0xFFFD;
                                                            i += 5;
                                                        }
                                                    } else {
                                                        // No low surrogate found, use replacement character
                                                        codepoint = 0xFFFD;
                                                        i += 5;
                                                    }
                                                } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                                                    // Unexpected low surrogate, use replacement character
                                                    codepoint = 0xFFFD;
                                                    i += 5;
                                                } else {
                                                    i += 5;  // Skip \uXXXX
                                                }
                                                
                                                // Convert Unicode codepoint to UTF-8
                                                if (codepoint < 0x80) {
                                                    unescaped += (char)codepoint;
                                                } else if (codepoint < 0x800) {
                                                    unescaped += (char)(0xC0 | (codepoint >> 6));
                                                    unescaped += (char)(0x80 | (codepoint & 0x3F));
                                                } else if (codepoint < 0x10000) {
                                                    unescaped += (char)(0xE0 | (codepoint >> 12));
                                                    unescaped += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                                                    unescaped += (char)(0x80 | (codepoint & 0x3F));
                                                } else {
                                                    // 4-byte UTF-8 for codepoints >= 0x10000
                                                    unescaped += (char)(0xF0 | (codepoint >> 18));
                                                    unescaped += (char)(0x80 | ((codepoint >> 12) & 0x3F));
                                                    unescaped += (char)(0x80 | ((codepoint >> 6) & 0x3F));
                                                    unescaped += (char)(0x80 | (codepoint & 0x3F));
                                                }
                                            } catch (...) {
                                                // On any error, use replacement character
                                                unescaped += (char)0xEF;
                                                unescaped += (char)0xBF;
                                                unescaped += (char)0xBD;  // UTF-8 for U+FFFD
                                                i += 5;
                                            }
                                        } else {
                                            // Invalid hex characters, output replacement and skip
                                            unescaped += (char)0xEF;
                                            unescaped += (char)0xBF;
                                            unescaped += (char)0xBD;  // UTF-8 for U+FFFD
                                            i += 5;
                                        }
                                    } else {
                                        // Not enough characters for \uXXXX, just output backslash
                                        unescaped += lyricsData[i];
                                    }
                                    break;
                                }
                                default: unescaped += lyricsData[i]; break;
                            }
                        } else {
                            unescaped += lyricsData[i];
                        }
                    }
                    
                    // Parse LRC tags from unescaped string
                    std::string title = extractTag(unescaped, "ti");
                    std::string artist = extractTag(unescaped, "ar");
                    std::string total = extractTag(unescaped, "total");
                    
                    // Debug output
                    if (f) {
                        fprintf(f, "lyricsData raw: %s\n", lyricsData.c_str());
                        fprintf(f, "unescaped: %s\n", unescaped.c_str());
                        fprintf(f, "title: [%s] artist: [%s] total: [%s]\n", title.c_str(), artist.c_str(), total.c_str());
                    }
                    
                    if (!title.empty()) {
                        localInfo.title = utf8ToWstring(title);
                    }
                    if (!artist.empty()) {
                        localInfo.artist = utf8ToWstring(artist);
                    }
                    if (!total.empty()) {
                        try { 
                            localInfo.duration = std::stod(total) / 1000.0; // ms to seconds
                        } catch (...) {}
                    }
                    
                    // Parse lyrics
                    localInfo.lyrics = parseKRC(unescaped);
                    
                    // Debug: print first few lyrics
                    if (f) {
                        fprintf(f, "Parsed %zu lyrics lines, currentTime=%.2f sec\n", localInfo.lyrics.size(), localInfo.currentTime);
                        for (size_t i = 0; i < 5 && i < localInfo.lyrics.size(); i++) {
                            // Convert wstring back to UTF-8 for debug output
                            std::string utf8Text;
                            if (!localInfo.lyrics[i].text.empty()) {
                                int len = WideCharToMultiByte(CP_UTF8, 0, localInfo.lyrics[i].text.c_str(), -1, nullptr, 0, nullptr, nullptr);
                                if (len > 0) {
                                    utf8Text.resize(len - 1);
                                    WideCharToMultiByte(CP_UTF8, 0, localInfo.lyrics[i].text.c_str(), -1, &utf8Text[0], len, nullptr, nullptr);
                                }
                            }
                            fprintf(f, "  [%d] startTime=%d ms: %s\n", (int)i, localInfo.lyrics[i].startTime, utf8Text.c_str());
                        }
                    }
                    
                    // Fallback: estimate duration from last lyric line if not available
                    if (localInfo.duration <= 0 && !localInfo.lyrics.empty()) {
                        const auto& lastLine = localInfo.lyrics.back();
                        localInfo.duration = (lastLine.startTime + lastLine.duration) / 1000.0;
                        if (f) fprintf(f, "[DEBUG] Estimated duration from lyrics: %.2f sec\n", localInfo.duration);
                    }
                    
                    localInfo.hasData = true;
                }
            }
        }
        
        // 更新共享数据并调用回调
        {
            std::lock_guard<std::mutex> lock(songInfoMutex_);
            songInfo_ = localInfo;
        }
        if (callback_) callback_(localInfo);
    }
    else if (type == "playerState") {
        bool isPlaying;
        double currentTime;
        {
            std::lock_guard<std::mutex> lock(songInfoMutex_);
            songInfo_.isPlaying = jsonGetBool(msg, "isPlaying");
            songInfo_.currentTime = jsonGetDouble(msg, "currentTime");
            isPlaying = songInfo_.isPlaying;
            currentTime = songInfo_.currentTime;
        }
        if (callback_) {
            SongInfo info;
            {
                std::lock_guard<std::mutex> lock(songInfoMutex_);
                info = songInfo_;
            }
            callback_(info);
        }
    }
    
    if (f) fclose(f);
}

std::wstring MoeKoeWS::utf8ToWstring(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring wstr(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
    return wstr;
}

// 线程安全获取歌曲信息
const SongInfo MoeKoeWS::getSongInfo() const {
    std::lock_guard<std::mutex> lock(songInfoMutex_);
    return songInfo_;
}

// === OSCSender Implementation ===

OSCSender::OSCSender(const std::string& ip, int port) : ip_(ip), port_(port) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    }
}

OSCSender::~OSCSender() {
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
    }
}

void OSCSender::padTo4(std::vector<uint8_t>& data) {
    while (data.size() % 4 != 0) {
        data.push_back(0);
    }
}

std::vector<uint8_t> OSCSender::buildOSCMessage(const std::string& address, const std::string& message) {
    std::vector<uint8_t> data;
    
    // Address pattern
    for (char c : address) data.push_back((uint8_t)c);
    data.push_back(0);
    padTo4(data);
    
    // Type tag string: ,sT (string + True)
    data.push_back(',');
    data.push_back('s');
    data.push_back('T');
    data.push_back(0);
    
    // String argument
    for (char c : message) data.push_back((uint8_t)c);
    data.push_back(0);
    padTo4(data);
    
    return data;
}

// 外部变量：主程序的OSC发送时间戳（用于限流）
extern DWORD g_lastOscSendTime;
const DWORD MIN_SEND_INTERVAL = 2000;  // 2秒最小间隔

bool OSCSender::sendChatbox(const std::string& message) {
    // 全局发送间隔保护（使用主程序的计时器）
    DWORD now = GetTickCount();
    // g_lastOscSendTime 是 main_gui.cpp 中定义的全局变量，通过extern声明在文件开头
    if (now - ::g_lastOscSendTime < MIN_SEND_INTERVAL) {
        // 跳过这次发送，避免触发VRChat限流
        return true;
    }
    
    std::string oscLogPath = GetDebugLogPath("moekoe_osc.log");
    
    if (sock_ == INVALID_SOCKET) {
        // Try to recreate socket
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
            FILE* f = fopen(oscLogPath.c_str(), "a");
            if (f) {
                fprintf(f, "[OSC] Failed to create socket: %d\n", WSAGetLastError());
                fclose(f);
            }
            return false;
        }
    }
    if (message.empty()) return true;
    
    // Truncate to 144 bytes (VRChat limit), avoiding cutting UTF-8 chars
    std::string msg = message;
    if (msg.size() > 144) {
        // Find safe truncation point (don't cut in middle of UTF-8 char)
        size_t safeLen = 141;
        while (safeLen > 0 && (msg[safeLen] & 0xC0) == 0x80) {
            safeLen--;  // Skip continuation bytes
        }
        msg = msg.substr(0, safeLen) + "...";
    }
    
    // 更新全局发送时间戳
    ::g_lastOscSendTime = now;
    auto data = buildOSCMessage("/chatbox/input", msg);
    
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr);
    
    int result = sendto(sock_, (const char*)data.data(), (int)data.size(), 0,
                        (sockaddr*)&addr, sizeof(addr));
    
    // 更新最后发送时间（只在成功发送后更新）- 已在函数开头更新g_lastOscSendTime
    
    // Debug output
    FILE* f = fopen(oscLogPath.c_str(), "a");
    if (f) {
        if (result == SOCKET_ERROR) {
            fprintf(f, "[OSC] sendto failed: %d\n", WSAGetLastError());
        } else {
            fprintf(f, "[OSC] sent %d bytes, message: %s\n", result, msg.c_str());
        }
        fclose(f);
    }
    
    return result != SOCKET_ERROR;
}

bool OSCSender::sendChatbox(const std::wstring& message) {
    if (message.empty()) return true;
    
    // Convert wstring to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    
    std::string utf8(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, &utf8[0], len, nullptr, nullptr);
    
    return sendChatbox(utf8);
}

// === OSCReceiver Implementation ===

OSCReceiver::OSCReceiver(int port) : port_(port) {
}

OSCReceiver::~OSCReceiver() {
    stop();
}

bool OSCReceiver::start() {
    if (running_) return true;
    
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return false;
    }
    
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) {
        return false;
    }
    
    // Allow address reuse
    int reuse = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    
    // Bind to port
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }
    
    // Set non-blocking
    u_long mode = 1;
    ioctlsocket(sock_, FIONBIO, &mode);
    
    running_ = true;
    thread_ = std::thread(&OSCReceiver::run, this);
    
    std::string oscLogPath = GetDebugLogPath("moekoe_osc.log");
    FILE* f = fopen(oscLogPath.c_str(), "a");
    if (f) {
        fprintf(f, "[OSCReceiver] Listening on port %d for VRChat pause commands\n", port_);
        fclose(f);
    }
    
    return true;
}

void OSCReceiver::stop() {
    running_ = false;
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void OSCReceiver::run() {
    uint8_t buffer[4096];
    sockaddr_in fromAddr = {};
    int fromLen = sizeof(fromAddr);
    
    while (running_) {
        int received = recvfrom(sock_, (char*)buffer, sizeof(buffer), 0,
                                (sockaddr*)&fromAddr, &fromLen);
        
        if (received > 0) {
            parseOSCMessage(buffer, received);
        } else {
            // No data, sleep a bit
            Sleep(10);
        }
    }
}

std::string OSCReceiver::readString(const uint8_t* data, size_t len, size_t& pos) {
    std::string result;
    while (pos < len && data[pos] != 0) {
        result += (char)data[pos];
        pos++;
    }
    // Skip null terminator and padding
    pos++;  // Skip null
    while (pos < len && (pos % 4) != 0) {
        pos++;  // Skip padding
    }
    return result;
}

bool OSCReceiver::parseOSCMessage(const uint8_t* data, size_t len) {
    if (len < 4) return false;
    
    size_t pos = 0;
    
    // Read address pattern
    std::string address = readString(data, len, pos);
    if (address.empty()) return false;
    
    // Read type tag string
    if (pos >= len) return false;
    std::string typeTag = readString(data, len, pos);
    
    std::string oscLogPath = GetDebugLogPath("moekoe_osc.log");
    
    // Check for pause commands
    // VRChat sends avatar parameters to /avatar/parameters/ParameterName
    // Also support /lyrics/pause for convenience
    bool isPauseCommand = (address == "/avatar/parameters/LyricsPause" ||
                           address == "/avatar/parameters/lyricspause" ||
                           address == "/lyrics/pause" ||
                           address == "/vrc/osc/lyrics/pause");
    
    // For float parameters, check if value > 0.5 (triggered)
    if (isPauseCommand && !typeTag.empty() && typeTag[0] == ',') {
        bool shouldTrigger = false;
        
        if (typeTag.find('f') != std::string::npos && pos + 4 <= len) {
            // Float value (big-endian)
            uint32_t intValue = (data[pos] << 24) | (data[pos+1] << 16) | 
                               (data[pos+2] << 8) | data[pos+3];
            float value = *(float*)&intValue;
            shouldTrigger = (value > 0.5f);
            
            FILE* f = fopen(oscLogPath.c_str(), "a");
            if (f) {
                fprintf(f, "[OSCReceiver] Received %s with float value %.2f\n", address.c_str(), value);
                fclose(f);
            }
        } else if (typeTag.find('i') != std::string::npos && pos + 4 <= len) {
            // Integer value (big-endian)
            int value = (data[pos] << 24) | (data[pos+1] << 16) | 
                       (data[pos+2] << 8) | data[pos+3];
            shouldTrigger = (value != 0);
            
            FILE* f = fopen(oscLogPath.c_str(), "a");
            if (f) {
                fprintf(f, "[OSCReceiver] Received %s with int value %d\n", address.c_str(), value);
                fclose(f);
            }
        } else if (typeTag.find('T') != std::string::npos) {
            // True value
            shouldTrigger = true;
            
            FILE* f = fopen(oscLogPath.c_str(), "a");
            if (f) {
                fprintf(f, "[OSCReceiver] Received %s with True value\n", address.c_str());
                fclose(f);
            }
        }
        
        if (shouldTrigger && pauseCallback_) {
            FILE* f = fopen(oscLogPath.c_str(), "a");
            if (f) {
                fprintf(f, "[OSCReceiver] Triggering pause callback!\n");
                fclose(f);
            }
            pauseCallback_();
        }
    }
    
    return true;
}

} // namespace moekoe

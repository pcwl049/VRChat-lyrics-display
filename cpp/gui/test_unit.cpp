// test_unit.cpp - Unit tests for VRChat Lyrics Display
// Compile: cl /EHsc /O2 /DUNICODE /D_UNICODE test_unit.cpp /Fe:test_unit.exe /link advapi32.lib winhttp.lib ws2_32.lib

#define _CRT_SECURE_NO_WARNINGS
#define _WIN32_IE 0x0600

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>
#include <cassert>
#include <iostream>
#include <fstream>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

// ==================== Test Framework ====================
int g_testsPassed = 0;
int g_testsFailed = 0;
std::string g_currentTest;

#define TEST_BEGIN(name) \
    g_currentTest = name; \
    std::cout << "[TEST] " << name << " ... " << std::flush;

#define TEST_PASS() \
    std::cout << "PASSED" << std::endl; \
    g_testsPassed++;

#define TEST_FAIL(msg) \
    std::cout << "FAILED: " << msg << std::endl; \
    g_testsFailed++;

#define ASSERT_TRUE(cond) \
    if (!(cond)) { TEST_FAIL(#cond); return; }

#define ASSERT_FALSE(cond) \
    if (cond) { TEST_FAIL("!(" #cond ")"); return; }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { TEST_FAIL(#a " != " #b); return; }

#define ASSERT_STR_EQ(a, b) \
    if (strcmp((a), (b)) != 0) { TEST_FAIL(std::string(#a " != ") + (b)); return; }

#define ASSERT_WSTR_EQ(a, b) \
    if (wcscmp(std::wstring(a).c_str(), std::wstring(b).c_str()) != 0) { TEST_FAIL(#a " != " #b); return; }

// ==================== Functions Under Test ====================

// String conversion
std::string WstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string result(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWstring(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring result(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

// Version parsing
int ParseVersion(const std::wstring& ver) {
    int major = 0, minor = 0, patch = 0;
    swscanf_s(ver.c_str(), L"%d.%d.%d", &major, &minor, &patch);
    return major * 10000 + minor * 100 + patch;
}

// SHA256 calculation
std::string CalculateSHA256(const wchar_t* filePath) {
    std::string result;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return result;
    
    if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CloseHandle(hFile);
        return result;
    }
    
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        CloseHandle(hFile);
        return result;
    }
    
    BYTE buffer[8192];
    DWORD bytesRead;
    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        if (!CryptHashData(hHash, buffer, bytesRead, 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            CloseHandle(hFile);
            return result;
        }
    }
    
    DWORD hashLen = 0;
    DWORD hashLenSize = sizeof(DWORD);
    if (CryptGetHashParam(hHash, HP_HASHSIZE, (BYTE*)&hashLen, &hashLenSize, 0) && hashLen == 32) {
        BYTE hashData[32];
        if (CryptGetHashParam(hHash, HP_HASHVAL, hashData, &hashLen, 0)) {
            char hex[65];
            for (int i = 0; i < 32; i++) {
                sprintf_s(hex + i * 2, 3, "%02x", hashData[i]);
            }
            hex[64] = '\0';
            result = hex;
        }
    }
    
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return result;
}

// OSC message builder
std::vector<uint8_t> BuildOSCMessage(const std::string& address, const std::string& message) {
    std::vector<uint8_t> data;
    
    // Address
    data.insert(data.end(), address.begin(), address.end());
    data.push_back(0);
    while (data.size() % 4 != 0) data.push_back(0);
    
    // Type tag
    data.push_back(',');
    data.push_back('s');
    data.push_back(0);
    data.push_back(0);
    
    // Message
    size_t msgStart = data.size();
    data.insert(data.end(), message.begin(), message.end());
    data.push_back(0);
    while (data.size() % 4 != 0) data.push_back(0);
    
    return data;
}

// Lyric line structure
struct LyricLine {
    int startTime = 0;
    int duration = 0;
    std::wstring text;
};

// Parse LRC lyric line
bool ParseLrcLine(const std::string& line, int& timeMs, std::string& text) {
    // Format: [mm:ss.xx]text or [mm:ss:xx]text
    size_t bracketEnd = line.find(']');
    if (bracketEnd == std::string::npos || line[0] != '[') return false;
    
    // Parse time
    std::string timeStr = line.substr(1, bracketEnd - 1);
    int minutes = 0, seconds = 0, hundredths = 0;
    
    size_t colon1 = timeStr.find(':');
    if (colon1 == std::string::npos) return false;
    
    minutes = atoi(timeStr.substr(0, colon1).c_str());
    
    size_t colon2 = timeStr.find(':', colon1 + 1);
    size_t dot = timeStr.find('.', colon1 + 1);
    
    if (colon2 != std::string::npos) {
        // Format: mm:ss:xx (Netease)
        seconds = atoi(timeStr.substr(colon1 + 1, colon2 - colon1 - 1).c_str());
        hundredths = atoi(timeStr.substr(colon2 + 1).c_str());
    } else if (dot != std::string::npos) {
        // Format: mm:ss.xx (Standard LRC)
        seconds = atoi(timeStr.substr(colon1 + 1, dot - colon1 - 1).c_str());
        hundredths = atoi(timeStr.substr(dot + 1).c_str());
    } else {
        return false;
    }
    
    timeMs = minutes * 60000 + seconds * 1000 + hundredths * 10;
    text = line.substr(bracketEnd + 1);
    return true;
}

// Base64 decode (simple implementation for testing)
std::string DecodeBase64(const std::string& encoded) {
    static const std::string base64_chars = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;
    
    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

// ==================== Unit Tests ====================

void Test_StringConversion_Empty() {
    TEST_BEGIN("StringConversion_Empty");
    
    std::string empty8 = WstringToUtf8(L"");
    std::wstring empty16 = Utf8ToWstring("");
    
    ASSERT_TRUE(empty8.empty());
    ASSERT_TRUE(empty16.empty());
    
    TEST_PASS();
}

void Test_StringConversion_ASCII() {
    TEST_BEGIN("StringConversion_ASCII");
    
    std::string result1 = WstringToUtf8(L"Hello World");
    ASSERT_STR_EQ(result1.c_str(), "Hello World");
    
    std::wstring result2 = Utf8ToWstring("Hello World");
    ASSERT_WSTR_EQ(result2, L"Hello World");
    
    TEST_PASS();
}

void Test_StringConversion_Chinese() {
    TEST_BEGIN("StringConversion_Chinese");
    
    std::wstring chinese = L"\u4E2D\u6587\u6D4B\u8BD5"; // 中文测试
    std::string utf8 = WstringToUtf8(chinese);
    std::wstring back = Utf8ToWstring(utf8);
    
    ASSERT_WSTR_EQ(back, chinese);
    
    TEST_PASS();
}

void Test_StringConversion_Emoji() {
    TEST_BEGIN("StringConversion_Emoji");
    
    std::wstring emoji = L"\U0001F600"; // 😀
    std::string utf8 = WstringToUtf8(emoji);
    std::wstring back = Utf8ToWstring(utf8);
    
    ASSERT_WSTR_EQ(back, emoji);
    
    TEST_PASS();
}

void Test_VersionParsing_Major() {
    TEST_BEGIN("VersionParsing_Major");
    
    ASSERT_EQ(ParseVersion(L"1.0.0"), 10000);
    ASSERT_EQ(ParseVersion(L"2.0.0"), 20000);
    ASSERT_EQ(ParseVersion(L"10.0.0"), 100000);
    
    TEST_PASS();
}

void Test_VersionParsing_Minor() {
    TEST_BEGIN("VersionParsing_Minor");
    
    ASSERT_EQ(ParseVersion(L"0.1.0"), 100);
    ASSERT_EQ(ParseVersion(L"0.10.0"), 1000);
    ASSERT_EQ(ParseVersion(L"1.5.0"), 10500);
    
    TEST_PASS();
}

void Test_VersionParsing_Patch() {
    TEST_BEGIN("VersionParsing_Patch");
    
    ASSERT_EQ(ParseVersion(L"0.0.1"), 1);
    ASSERT_EQ(ParseVersion(L"0.0.99"), 99);
    ASSERT_EQ(ParseVersion(L"1.2.3"), 10203);
    
    TEST_PASS();
}

void Test_VersionParsing_Comparison() {
    TEST_BEGIN("VersionParsing_Comparison");
    
    // v0.2.2 vs v0.1.0
    ASSERT_TRUE(ParseVersion(L"0.2.2") > ParseVersion(L"0.1.0"));
    
    // v1.0.0 vs v0.9.99
    ASSERT_TRUE(ParseVersion(L"1.0.0") > ParseVersion(L"0.9.99"));
    
    // v0.2.1 vs v0.2.2
    ASSERT_TRUE(ParseVersion(L"0.2.2") > ParseVersion(L"0.2.1"));
    
    TEST_PASS();
}

void Test_SHA256_KnownValue() {
    TEST_BEGIN("SHA256_KnownValue");
    
    // Create a test file with known content
    const wchar_t* testFile = L"test_sha256.tmp";
    FILE* f = _wfopen(testFile, L"wb");
    ASSERT_TRUE(f != nullptr);
    
    // "hello" has known SHA256
    const char* content = "hello";
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    
    // SHA256 of "hello" = 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
    std::string hash = CalculateSHA256(testFile);
    
    ASSERT_STR_EQ(hash.c_str(), "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
    
    // Cleanup
    DeleteFileW(testFile);
    
    TEST_PASS();
}

void Test_SHA256_EmptyFile() {
    TEST_BEGIN("SHA256_EmptyFile");
    
    const wchar_t* testFile = L"test_sha256_empty.tmp";
    FILE* f = _wfopen(testFile, L"wb");
    ASSERT_TRUE(f != nullptr);
    fclose(f);
    
    // SHA256 of empty string = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    std::string hash = CalculateSHA256(testFile);
    
    ASSERT_STR_EQ(hash.c_str(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    
    DeleteFileW(testFile);
    
    TEST_PASS();
}

void Test_SHA256_NonExistentFile() {
    TEST_BEGIN("SHA256_NonExistentFile");
    
    std::string hash = CalculateSHA256(L"nonexistent_file_12345.tmp");
    ASSERT_TRUE(hash.empty());
    
    TEST_PASS();
}

void Test_SHA256_ChineseContent() {
    TEST_BEGIN("SHA256_ChineseContent");
    
    const wchar_t* testFile = L"test_sha256_chinese.tmp";
    FILE* f = _wfopen(testFile, L"wb");
    ASSERT_TRUE(f != nullptr);
    
    // Write UTF-8 encoded Chinese
    std::string content = WstringToUtf8(L"\u4E2D\u6587");
    fwrite(content.c_str(), 1, content.size(), f);
    fclose(f);
    
    // Verify hash is calculated and not empty
    std::string hash = CalculateSHA256(testFile);
    ASSERT_TRUE(!hash.empty());
    ASSERT_EQ(hash.length(), 64u);
    
    DeleteFileW(testFile);
    
    TEST_PASS();
}

void Test_OSCMessage_AddressPadding() {
    TEST_BEGIN("OSCMessage_AddressPadding");
    
    std::vector<uint8_t> msg = BuildOSCMessage("/chatbox/input", "test");
    
    // Address should be padded to 4 bytes
    // "/chatbox/input" = 14 chars + 2 null padding = 16 bytes
    size_t addrEnd = 0;
    while (addrEnd < msg.size() && msg[addrEnd] != ',') addrEnd++;
    ASSERT_EQ(addrEnd % 4, 0u);
    
    TEST_PASS();
}

void Test_OSCMessage_TypeTag() {
    TEST_BEGIN("OSCMessage_TypeTag");
    
    std::vector<uint8_t> msg = BuildOSCMessage("/test", "hello");
    
    // Find type tag
    bool foundComma = false;
    for (size_t i = 0; i < msg.size() - 1; i++) {
        if (msg[i] == ',') {
            foundComma = true;
            ASSERT_EQ(msg[i + 1], 's'); // String type
            break;
        }
    }
    ASSERT_TRUE(foundComma);
    
    TEST_PASS();
}

void Test_OSCMessage_MessageContent() {
    TEST_BEGIN("OSCMessage_MessageContent");
    
    std::vector<uint8_t> msg = BuildOSCMessage("/test", "Hello World");
    
    // Message should be padded to 4 bytes
    ASSERT_EQ(msg.size() % 4, 0u);
    
    TEST_PASS();
}

void Test_LrcLine_StandardFormat() {
    TEST_BEGIN("LrcLine_StandardFormat");
    
    int timeMs = 0;
    std::string text;
    
    bool result = ParseLrcLine("[01:23.45]Hello World", timeMs, text);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(timeMs, 83450); // 1*60000 + 23*1000 + 45*10
    ASSERT_STR_EQ(text.c_str(), "Hello World");
    
    TEST_PASS();
}

void Test_LrcLine_NeteaseFormat() {
    TEST_BEGIN("LrcLine_NeteaseFormat");
    
    int timeMs = 0;
    std::string text;
    
    // Netease uses [mm:ss:xx] format
    bool result = ParseLrcLine("[01:23:45]Test Lyric", timeMs, text);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(timeMs, 83450);
    ASSERT_STR_EQ(text.c_str(), "Test Lyric");
    
    TEST_PASS();
}

void Test_LrcLine_ZeroTime() {
    TEST_BEGIN("LrcLine_ZeroTime");
    
    int timeMs = 0;
    std::string text;
    
    bool result = ParseLrcLine("[00:00.00]Start", timeMs, text);
    
    ASSERT_TRUE(result);
    ASSERT_EQ(timeMs, 0);
    ASSERT_STR_EQ(text.c_str(), "Start");
    
    TEST_PASS();
}

void Test_LrcLine_InvalidFormat() {
    TEST_BEGIN("LrcLine_InvalidFormat");
    
    int timeMs = 0;
    std::string text;
    
    ASSERT_FALSE(ParseLrcLine("No brackets", timeMs, text));
    ASSERT_FALSE(ParseLrcLine("[invalid", timeMs, text));
    ASSERT_FALSE(ParseLrcLine("invalid]", timeMs, text));
    ASSERT_FALSE(ParseLrcLine("[abc:def]text", timeMs, text));
    
    TEST_PASS();
}

void Test_Base64_Empty() {
    TEST_BEGIN("Base64_Empty");
    
    std::string result = DecodeBase64("");
    ASSERT_TRUE(result.empty());
    
    TEST_PASS();
}

void Test_Base64_Hello() {
    TEST_BEGIN("Base64_Hello");
    
    // "Hello" in base64 = "SGVsbG8="
    std::string result = DecodeBase64("SGVsbG8=");
    ASSERT_STR_EQ(result.c_str(), "Hello");
    
    TEST_PASS();
}

void Test_Base64_Chinese() {
    TEST_BEGIN("Base64_Chinese");
    
    // "中文" UTF-8 bytes = E4 B8 AD E6 96 87
    // Base64 = "5Lit5paH"
    std::string result = DecodeBase64("5Lit5paH");
    std::string expected = WstringToUtf8(L"\u4E2D\u6587");
    ASSERT_STR_EQ(result.c_str(), expected.c_str());
    
    TEST_PASS();
}

void Test_ConfigReadWrite() {
    TEST_BEGIN("ConfigReadWrite");
    
    const wchar_t* testConfig = L"test_config.json";
    
    // Write config
    FILE* f = _wfopen(testConfig, L"wb");
    ASSERT_TRUE(f != nullptr);
    fprintf(f, "{\n  \"osc\": {\n    \"ip\": \"127.0.0.1\",\n    \"port\": 9000\n  },\n  \"test_value\": \"hello\"\n}\n");
    fclose(f);
    
    // Read and verify
    f = _wfopen(testConfig, L"rb");
    ASSERT_TRUE(f != nullptr);
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    std::string content(len, 0);
    fread(&content[0], 1, len, f);
    fclose(f);
    
    ASSERT_TRUE(content.find("127.0.0.1") != std::string::npos);
    ASSERT_TRUE(content.find("9000") != std::string::npos);
    ASSERT_TRUE(content.find("hello") != std::string::npos);
    
    DeleteFileW(testConfig);
    
    TEST_PASS();
}

// ==================== Main ====================

void PrintSummary() {
    std::cout << "\n====================" << std::endl;
    std::cout << "Tests Passed: " << g_testsPassed << std::endl;
    std::cout << "Tests Failed: " << g_testsFailed << std::endl;
    std::cout << "Total Tests: " << (g_testsPassed + g_testsFailed) << std::endl;
    std::cout << "====================" << std::endl;
}

int main() {
    std::cout << "VRChat Lyrics Display - Unit Tests" << std::endl;
    std::cout << "==================================" << std::endl << std::endl;
    
    // String conversion tests
    std::cout << "--- String Conversion ---" << std::endl;
    Test_StringConversion_Empty();
    Test_StringConversion_ASCII();
    Test_StringConversion_Chinese();
    Test_StringConversion_Emoji();
    
    // Version parsing tests
    std::cout << "\n--- Version Parsing ---" << std::endl;
    Test_VersionParsing_Major();
    Test_VersionParsing_Minor();
    Test_VersionParsing_Patch();
    Test_VersionParsing_Comparison();
    
    // SHA256 tests
    std::cout << "\n--- SHA256 ---" << std::endl;
    Test_SHA256_KnownValue();
    Test_SHA256_EmptyFile();
    Test_SHA256_NonExistentFile();
    Test_SHA256_ChineseContent();
    
    // OSC message tests
    std::cout << "\n--- OSC Message ---" << std::endl;
    Test_OSCMessage_AddressPadding();
    Test_OSCMessage_TypeTag();
    Test_OSCMessage_MessageContent();
    
    // LRC parsing tests
    std::cout << "\n--- LRC Parsing ---" << std::endl;
    Test_LrcLine_StandardFormat();
    Test_LrcLine_NeteaseFormat();
    Test_LrcLine_ZeroTime();
    Test_LrcLine_InvalidFormat();
    
    // Base64 tests
    std::cout << "\n--- Base64 ---" << std::endl;
    Test_Base64_Empty();
    Test_Base64_Hello();
    Test_Base64_Chinese();
    
    // Config tests
    std::cout << "\n--- Config ---" << std::endl;
    Test_ConfigReadWrite();
    
    PrintSummary();
    
    return g_testsFailed > 0 ? 1 : 0;
}

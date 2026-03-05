#include "utils.h"
#include <windows.h>
#include <iomanip>
#include <sstream>

namespace moekoe {

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

std::string wideToUtf8(const std::wstring& str) {
    if (str.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

std::wstring formatTime(double seconds) {
    int total = static_cast<int>(seconds);
    int min = total / 60;
    int sec = total % 60;
    
    std::wostringstream oss;
    oss << min << L":" << std::setw(2) << std::setfill(L'0') << sec;
    return oss.str();
}

std::wstring formatDuration(double seconds) {
    return formatTime(seconds);
}

std::wstring createProgressBar(double progress, int width) {
    std::wstring bar;
    int filled = static_cast<int>(progress * width);
    
    for (int i = 0; i < width; ++i) {
        if (i < filled) {
            bar += L"▓";
        } else {
            bar += L"░";
        }
    }
    
    return bar;
}

// 控制台颜色
static void setConsoleColor(WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

void printInfo(const std::wstring& message) {
    setConsoleColor(FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::wcout << L"[INFO] " << message << std::endl;
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void printSuccess(const std::wstring& message) {
    setConsoleColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::wcout << L"[OK] " << message << std::endl;
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void printWarning(const std::wstring& message) {
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::wcout << L"[WARN] " << message << std::endl;
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void printError(const std::wstring& message) {
    setConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::wcout << L"[ERR] " << message << std::endl;
    setConsoleColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

} // namespace moekoe

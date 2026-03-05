#pragma once

#include <string>
#include <chrono>

namespace moekoe {

// 字符串转换
std::wstring utf8ToWide(const std::string& str);
std::string wideToUtf8(const std::wstring& str);

// 时间格式化
std::wstring formatTime(double seconds);
std::wstring formatDuration(double seconds);

// 进度条
std::wstring createProgressBar(double progress, int width = 10);

// 控制台输出
void printInfo(const std::wstring& message);
void printSuccess(const std::wstring& message);
void printWarning(const std::wstring& message);
void printError(const std::wstring& message);

} // namespace moekoe

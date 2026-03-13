#pragma once

#include <windows.h>
#include <string>

// ============================================================================
// 绘制辅助函数 - GDI+ 圆角矩形和文本绘制
// ============================================================================

// 绘制抗锯齿圆角矩形
void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color);

// 绘制带边框的圆角矩形
void DrawRoundRectWithBorder(HDC hdc, int x, int y, int w, int h, int radius, COLORREF fillColor, COLORREF borderColor);

// 绘制半透明圆角矩形
void DrawRoundRectAlpha(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color, int alpha);

// 文本绘制辅助
void DrawTextCentered(HDC hdc, const wchar_t* text, int cx, int y, COLORREF color, HFONT font);
void DrawTextLeft(HDC hdc, const wchar_t* text, int x, int y, COLORREF color, HFONT font);
void DrawTextRight(HDC hdc, const wchar_t* text, int rightX, int y, COLORREF color, HFONT font);
void DrawTextVCentered(HDC hdc, const wchar_t* text, int x, int y, int h, COLORREF color, HFONT font);
void DrawTextVCenteredRight(HDC hdc, const wchar_t* text, int rightX, int y, int h, COLORREF color, HFONT font);
void DrawTextCenteredBoth(HDC hdc, const wchar_t* text, int x, int y, int w, int h, COLORREF color, HFONT font);

// 判断点是否在矩形内
inline bool IsInRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// ============================================================================
// 字符串转换和辅助函数
// ============================================================================

// 宽字符与 UTF-8 互转
std::string WstringToUtf8(const std::wstring& wstr);
std::wstring Utf8ToWstring(const std::string& str);

// 获取文本宽度
int GetTextWidth(HDC hdc, const wchar_t* text, HFONT font, int length);

// 构建进度条字符串
std::wstring BuildProgressBar(double progress, int bars);

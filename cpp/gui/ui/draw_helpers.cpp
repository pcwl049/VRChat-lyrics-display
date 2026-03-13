#include "draw_helpers.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

// 字体缓存 - 避免重复设置相同字体
HFONT g_currentFont = nullptr;

void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    SolidBrush brush(Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void DrawRoundRectWithBorder(HDC hdc, int x, int y, int w, int h, int radius, COLORREF fillColor, COLORREF borderColor) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    SolidBrush brush(Color(255, GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor)));
    graphics.FillPath(&brush, &path);
    
    Pen pen(Color(255, GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor)), 2);
    graphics.DrawPath(&pen, &path);
}

void DrawRoundRectAlpha(HDC hdc, int x, int y, int w, int h, int radius, COLORREF color, int alpha) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    
    GraphicsPath path;
    int d = radius * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
    
    SolidBrush brush(Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color)));
    graphics.FillPath(&brush, &path);
}

void DrawTextCentered(HDC hdc, const wchar_t* text, int cx, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, cx - sz.cx / 2, y, text, (int)wcslen(text));
}

void DrawTextLeft(HDC hdc, const wchar_t* text, int x, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    TextOutW(hdc, x, y, text, (int)wcslen(text));
}

void DrawTextRight(HDC hdc, const wchar_t* text, int rightX, int y, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, rightX - sz.cx, y, text, (int)wcslen(text));
}

void DrawTextVCentered(HDC hdc, const wchar_t* text, int x, int y, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, x, y + (h - sz.cy) / 2, text, (int)wcslen(text));
}

void DrawTextVCenteredRight(HDC hdc, const wchar_t* text, int rightX, int y, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, rightX - sz.cx, y + (h - sz.cy) / 2, text, (int)wcslen(text));
}

void DrawTextCenteredBoth(HDC hdc, const wchar_t* text, int x, int y, int w, int h, COLORREF color, HFONT font) {
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    SetFontCached(hdc, font);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, x + (w - sz.cx) / 2, y + (h - sz.cy) / 2, text, (int)wcslen(text));
}

// ============================================================================
// 字符串转换和辅助函数
// ============================================================================

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

int GetTextWidth(HDC hdc, const wchar_t* text, HFONT font, int length) {
    if (!text || length <= 0) return 0;

    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    SIZE size;
    GetTextExtentPoint32W(hdc, text, length, &size);
    SelectObject(hdc, oldFont);

    return size.cx;
}

std::wstring BuildProgressBar(double progress, int bars) {
    std::wstring bar;
    int filled = (int)(progress * bars);
    for (int i = 0; i < bars; i++) bar += (i < filled) ? L"\x2588" : L"\x2591";
    return bar;
}

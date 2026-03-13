#pragma once
#include <windows.h>
#include <gdiplus.h>

// External mutex handle for RestartAsAdmin
extern HANDLE g_mutex;

// Tray icon ID
#define TRAY_ICON_ID 1001

// Enable blur behind effect (毛玻璃效果)
void EnableBlurBehind(HWND hwnd);

// Update window system theme (title bar color, rounded corners)
void UpdateWindowSystemTheme(HWND hwnd);

// Check if running as administrator
bool IsRunningAsAdmin();

// Restart as administrator
void RestartAsAdmin(HWND hwnd);

// Create rounded rectangle path for GDI+
void CreateRoundRectPath(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& rect, int radius);

// Create a rounded window region
HRGN CreateRoundedWindowRegion(HWND hwnd, int cornerRadius = 12);

// Tray icon functions
void CreateTrayIcon(HWND hwnd);
void RemoveTrayIcon();
void UpdateTrayTip(const wchar_t* text);

// glass_window_lite.cpp - Lightweight Acrylic Glass Window
// Memory target: < 5MB

#include <windows.h>
#include <dwmapi.h>
#include <cmath>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

// Acrylic API
enum ACCENT_STATE { ACCENT_DISABLED = 0, ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };
struct ACCENT_POLICY { ACCENT_STATE AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; };
struct WINDOWCOMPOSITIONATTRIBDATA { int Attrib; void* pvData; size_t cbData; };
typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

HWND g_hwnd = nullptr;
float g_progress = 0.3f;
float g_time = 0.0f;
COLORREF g_accent = RGB(80, 140, 255);

void EnableAcrylic(HWND hwnd, BYTE alpha, COLORREF color) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;
    auto fn = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!fn) return;
    
    ACCENT_POLICY policy = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 2,
        ((DWORD)alpha << 24) | (GetBValue(color) << 16) | (GetGValue(color) << 8) | GetRValue(color), 0 };
    WINDOWCOMPOSITIONATTRIBDATA data = { 19, &policy, sizeof(policy) };
    fn(hwnd, &data);
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right, h = rc.bottom;
    
    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    
    // Dark background
    HBRUSH bgBrush = CreateSolidBrush(RGB(20, 20, 30));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);
    
    // Glow effect
    int glowX = (int)(g_progress * w);
    for (int i = 0; i < 30; i++) {
        float fade = 1.0f - (float)i / 30.0f;
        int alpha = (int)(35 * fade * (0.5f + 0.5f * sinf(g_time * 2.0f)));
        BYTE r = (BYTE)(GetRValue(g_accent) * alpha / 255);
        BYTE g = (BYTE)(GetGValue(g_accent) * alpha / 255);
        BYTE b = (BYTE)(GetBValue(g_accent) * alpha / 255);
        
        RECT glowRc = { glowX - 80 - i * 4, 0, glowX + 80 + i * 4, h };
        HBRUSH glowBrush = CreateSolidBrush(RGB(r, g, b));
        FillRect(memDC, &glowRc, glowBrush);
        DeleteObject(glowBrush);
    }
    
    SetBkMode(memDC, TRANSPARENT);
    
    // Title
    HFONT titleFont = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(memDC, titleFont);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT titleRc = { 25, 30, w - 25, 60 };
    DrawTextW(memDC, L"Now Playing", -1, &titleRc, DT_LEFT | DT_SINGLELINE);
    
    // Artist
    HFONT infoFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(memDC, infoFont);
    SetTextColor(memDC, RGB(180, 180, 200));
    RECT artistRc = { 25, 58, w - 25, 78 };
    DrawTextW(memDC, L"Artist Name", -1, &artistRc, DT_LEFT | DT_SINGLELINE);
    DeleteObject(infoFont);
    
    // Progress bar
    RECT bgRc = { 25, 95, w - 25, 101 };
    HBRUSH progressBgBrush = CreateSolidBrush(RGB(60, 60, 80));
    FillRect(memDC, &bgRc, progressBgBrush);
    DeleteObject(progressBgBrush);
    
    int pw = (int)((w - 50) * g_progress);
    if (pw > 0) {
        RECT progressRc = { 25, 95, 25 + pw, 101 };
        HBRUSH progressBrush = CreateSolidBrush(g_accent);
        FillRect(memDC, &progressRc, progressBrush);
        DeleteObject(progressBrush);
    }
    
    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 100));
    HPEN oldPen = (HPEN)SelectObject(memDC, borderPen);
    Rectangle(memDC, 0, 0, w, h);
    SelectObject(memDC, oldPen);
    DeleteObject(borderPen);
    
    SelectObject(memDC, oldFont);
    DeleteObject(titleFont);
    
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT:
            OnPaint(hwnd);
            return 0;
        case WM_TIMER:
            g_time += 0.016f;
            g_progress += 0.001f;
            if (g_progress > 1.0f) g_progress = 0.0f;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
            return hit == HTCLIENT ? HTCAPTION : hit;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GlassLite";
    RegisterClassExW(&wc);
    
    g_hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TOPMOST,
        L"GlassLite", L"", WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 140,
        nullptr, nullptr, hInst, nullptr);
    
    EnableAcrylic(g_hwnd, 180, RGB(30, 30, 40));
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    
    // Center
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_hwnd, nullptr, (sw - (rc.right - rc.left)) / 2, (sh - (rc.bottom - rc.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    ShowWindow(g_hwnd, SW_SHOW);
    SetTimer(g_hwnd, 1, 16, nullptr);
    
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
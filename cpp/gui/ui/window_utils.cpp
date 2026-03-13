#include "window_utils.h"
#include "../common/theme.h"
#include <shellapi.h>

// Tray icon data
static NOTIFYICONDATAW g_nid = {0};

// Enable blur behind effect (毛玻璃效果)
void EnableBlurBehind(HWND hwnd) {
    typedef struct _ACCENT_POLICY {
        int AccentState;
        int AccentFlags;
        int GradientColor;
        int AnimationId;
    } ACCENT_POLICY;
    
    HMODULE hUser = LoadLibraryW(L"user32.dll");
    if (!hUser) return;
    
    typedef BOOL(WINAPI* SetWindowCompositionAttribute_t)(HWND, void*);
    auto fn = (SetWindowCompositionAttribute_t)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    
    if (fn) {
        // 固定使用深色模式毛玻璃
        ACCENT_POLICY policy = { 4, 2, (COLOR_GLASS_TINT & 0xFFFFFF) | (GLASS_ALPHA << 24), 0 };
        struct _WINDOWCOMPOSITIONATTRIBDATA {
            int Attrib;
            void* pvData;
            int cbData;
        } data = { 19, &policy, sizeof(policy) };
        fn(hwnd, &data);
    }
    
    FreeLibrary(hUser);
}

// Update window system theme (title bar color, rounded corners)
void UpdateWindowSystemTheme(HWND hwnd) {
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
        auto fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            // DWMWA_WINDOW_CORNER_PREFERENCE = 33
            // 0=DEFAULT, 1=DONOTROUND, 2=ROUND, 3=ROUNDSMALL
            int corner = 2;  // ROUND - 使用系统圆角
            fn(hwnd, 33, &corner, sizeof(corner));
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20
            BOOL dark = TRUE;  // 固定深色模式
            fn(hwnd, 20, &dark, sizeof(dark));
        }
        FreeLibrary(hDwm);
    }
}

// Check if running as administrator
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, 
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// 创建圆角矩形路径的辅助函数
void CreateRoundRectPath(Gdiplus::GraphicsPath* path, const Gdiplus::RectF& rect, int radius) {
    Gdiplus::REAL d = (Gdiplus::REAL)(radius * 2);
    path->AddArc(rect.X, rect.Y, d, d, 180.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.0f, 90.0f);
    path->AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.0f, 90.0f);
    path->AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.0f, 90.0f);
    path->CloseFigure();
}

// Create a rounded window region for the main window
HRGN CreateRoundedWindowRegion(HWND hwnd, int cornerRadius) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    
    // 创建圆角矩形区域
    HRGN hrgn = CreateRoundRectRgn(
        rc.left, rc.top, rc.right, rc.bottom,
        cornerRadius, cornerRadius
    );
    
    return hrgn;
}

// Restart as administrator
void RestartAsAdmin(HWND hwnd) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    
    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.nShow = SW_SHOWNORMAL;
    
    if (ShellExecuteExW(&sei)) {
        // Release mutex before closing to allow new instance to start
        if (g_mutex) {
            ReleaseMutex(g_mutex);
            CloseHandle(g_mutex);
            g_mutex = nullptr;
        }
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
}

// Create tray icon
void CreateTrayIcon(HWND hwnd) {
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_USER + 100;  // WM_TRAYICON
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"VRChat Lyrics Display");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

// Remove tray icon
void RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

// Update tray tooltip
void UpdateTrayTip(const wchar_t* text) {
    wcscpy_s(g_nid.szTip, text);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

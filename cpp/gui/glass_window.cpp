// glass_window.cpp - Glass effect window with Mica support

#include "glass_window.h"
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace moekoe {
namespace gui {

GlassWindow::GlassWindow(HWND hwnd, const Config& config) 
    : hwnd_(hwnd), config_(config) {
    CreateFonts();
}

GlassWindow::~GlassWindow() {
    // 释放字体资源
    if (titleFont_) { DeleteObject(titleFont_); titleFont_ = nullptr; }
    if (artistFont_) { DeleteObject(artistFont_); artistFont_ = nullptr; }
    if (timeFont_) { DeleteObject(timeFont_); timeFont_ = nullptr; }
    if (lyricFont_) { DeleteObject(lyricFont_); lyricFont_ = nullptr; }
}

void GlassWindow::CreateFonts() {
    titleFont_ = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    artistFont_ = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    timeFont_ = CreateFontW(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    lyricFont_ = CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

void GlassWindow::Paint(HDC hdc) {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    
    HDC memDC = CreateCompatibleDC(hdc);
    if (!memDC) return;
    
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, w, h);
    if (!memBitmap) {
        DeleteDC(memDC);
        return;
    }
    
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Dark gradient background
    TRIVERTEX vertices[2] = {
        { 0, 0, 20 << 8, 20 << 8, 30 << 8, 0 },
        { w, h, 30 << 8, 30 << 8, 45 << 8, 0 }
    };
    GRADIENT_RECT gRect = { 0, 1 };
    GradientFill(memDC, vertices, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
    
    // Accent glow
    int glowX = (int)(progress_ * w);
    for (int i = 0; i < 50; i++) {
        double fade = 1.0 - (i / 50.0);
        int alpha = (int)(50 * fade);
        BYTE r = (BYTE)(config_.accentColor.r * alpha);
        BYTE g = (BYTE)(config_.accentColor.g * alpha);
        BYTE b = (BYTE)(config_.accentColor.b * alpha);
        
        int left = glowX - 80 - i * 3;
        int right = glowX + 80 + i * 3;
        if (left < 0) left = 0;
        if (right > w) right = w;
        
        HBRUSH glowBrush = CreateSolidBrush(RGB(r, g, b));
        RECT glowRc = { left, 0, right, h };
        FillRect(memDC, &glowRc, glowBrush);
        DeleteObject(glowBrush);
    }
    
    SetBkMode(memDC, TRANSPARENT);
    
    // Title
    SetTextColor(memDC, RGB(255, 255, 255));
    HFONT oldFont = (HFONT)SelectObject(memDC, titleFont_);
    RECT titleRc = { 20, 25, w - 20, 60 };
    DrawTextW(memDC, songTitle_.c_str(), -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    
    // Artist
    SetTextColor(memDC, RGB(200, 200, 220));
    SelectObject(memDC, artistFont_);
    RECT artistRc = { 20, 60, w - 20, 85 };
    DrawTextW(memDC, songArtist_.c_str(), -1, &artistRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    
    // Progress bar
    RECT progressBgRc = { 20, 100, w - 20, 110 };
    HBRUSH progressBgBrush = CreateSolidBrush(RGB(40, 40, 55));
    FillRect(memDC, &progressBgRc, progressBgBrush);
    DeleteObject(progressBgBrush);
    
    int progressWidth = (int)((w - 40) * progress_);
    if (progressWidth > 0) {
        RECT progressRc = { 20, 100, 20 + progressWidth, 110 };
        HBRUSH progressBrush = CreateSolidBrush(RGB(
            (int)(config_.accentColor.r * 255),
            (int)(config_.accentColor.g * 255),
            (int)(config_.accentColor.b * 255)
        ));
        FillRect(memDC, &progressRc, progressBrush);
        DeleteObject(progressBrush);
    }
    
    // Time
    SelectObject(memDC, timeFont_);
    SetTextColor(memDC, RGB(180, 180, 195));
    RECT timeRc = { 20, 118, w - 20, 138 };
    DrawTextW(memDC, timeText_.c_str(), -1, &timeRc, DT_LEFT | DT_SINGLELINE);
    
    // Lyrics display
    int lyricY = 145;
    if (!lyrics_.empty()) {
        int idx = FindCurrentLyricIndex();
        
        // Check if lyric index changed - start animation
        if (idx != currentLyricIndex_) {
            prevLyricIndex_ = currentLyricIndex_;
            currentLyricIndex_ = idx;
            lyricAnim_.Start(0, 1);  // Fade/slide animation
        }
        
        // Update lyric animation
        lyricAnim_.Update();
        double animProgress = lyricAnim_.current;
        
        // Show previous, current, next lyrics
        int showLines = 3;
        int startIdx = idx - 1;
        if (startIdx < 0) startIdx = 0;
        
        for (int i = 0; i < showLines && startIdx + i < (int)lyrics_.size(); i++) {
            int lineIdx = startIdx + i;
            const auto& line = lyrics_[lineIdx];
            
            bool isCurrent = (lineIdx == idx);
            bool isPast = (lineIdx < idx);
            
            // Calculate animation offset for current line
            int yOffset = 0;
            int alpha = 255;
            
            if (isCurrent && lyricAnim_.running) {
                // Slide up and fade in effect
                yOffset = (int)((1.0 - animProgress) * 10);
                alpha = (int)(animProgress * 255);
            }
            
            // Calculate text color with alpha
            int r, g, b;
            if (isCurrent) {
                // Highlight current line
                r = 255; g = 255; b = 255;
                // Add glow effect for current line
                if (!lyricAnim_.running || animProgress > 0.5) {
                    SetTextColor(memDC, RGB(r, g, b));
                }
            } else if (isPast) {
                // Past lyrics - dimmer
                r = 100; g = 100; b = 120;
                SetTextColor(memDC, RGB(r, g, b));
            } else {
                // Future lyrics
                r = 150; g = 150; b = 170;
                SetTextColor(memDC, RGB(r, g, b));
            }
            
            // Use larger font for current line
            if (isCurrent) {
                SelectObject(memDC, titleFont_);  // Use title font for emphasis
            } else {
                SelectObject(memDC, lyricFont_);
            }
            
            RECT lyricRc = { 20, lyricY + i * 24 + yOffset, w - 20, lyricY + i * 24 + 24 + yOffset };
            DrawTextW(memDC, line.text.c_str(), -1, &lyricRc, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    } else {
        // No lyrics
        SetTextColor(memDC, RGB(100, 100, 120));
        SelectObject(memDC, lyricFont_);
        RECT noLyricRc = { 20, lyricY + 20, w - 20, lyricY + 50 };
        DrawTextW(memDC, L"~ waiting for lyrics ~", -1, &noLyricRc, DT_CENTER | DT_SINGLELINE);
    }
    
    // Hint
    SetTextColor(memDC, RGB(120, 120, 140));
    SelectObject(memDC, timeFont_);
    RECT hintRc = { 20, h - 25, w - 20, h - 5 };
    DrawTextW(memDC, L"Drag: move | Right: close", -1, &hintRc, DT_CENTER | DT_SINGLELINE);
    
    SelectObject(memDC, oldFont);
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

void GlassWindow::Update() {
    progressAnim_.Update();
    lyricAnim_.Update();
    if (progressAnim_.running) {
        progress_ = progressAnim_.current;
    }
}

void GlassWindow::EnableBlurBehind() {
    // Windows 11 Mica effect (preferred)
    typedef enum _DWM_SYSTEMBACKDROP_TYPE {
        DWMSBT_AUTO = 0,
        DWMSBT_NONE = 1,
        DWMSBT_MAINWINDOW = 2,        // Mica
        DWMSBT_TRANSIENTWINDOW = 3,   // Acrylic
        DWMSBT_TABBEDWINDOW = 4       // Tabbed
    } DWM_SYSTEMBACKDROP_TYPE;
    
    typedef HRESULT(WINAPI* pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    
    HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
    if (dwmapi) {
        pDwmSetWindowAttribute DwmSetWindowAttribute = 
            (pDwmSetWindowAttribute)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
        
        if (DwmSetWindowAttribute) {
            // Try Mica effect first (Windows 11 22H2+)
            DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
            HRESULT hr = DwmSetWindowAttribute(hwnd_, 38, &backdrop, sizeof(backdrop));
            
            if (FAILED(hr)) {
                // Fallback: Try older DWMWA_USE_IMMERSIVE_DARK_MODE + Acrylic
                BOOL darkMode = TRUE;
                DwmSetWindowAttribute(hwnd_, 20, &darkMode, sizeof(darkMode));
                
                // Then try Acrylic
                backdrop = DWMSBT_TRANSIENTWINDOW;
                DwmSetWindowAttribute(hwnd_, 38, &backdrop, sizeof(backdrop));
            }
        }
        FreeLibrary(dwmapi);
    }
    
    // Fallback for Windows 10: SetWindowCompositionAttribute
    typedef enum _ACCENT_STATE {
        ACCENT_DISABLED = 0,
        ACCENT_ENABLE_BLURBEHIND = 3,
        ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    } ACCENT_STATE;
    
    typedef struct _ACCENT_POLICY {
        ACCENT_STATE AccentState;
        DWORD AccentFlags;
        DWORD GradientColor;
        DWORD AnimationId;
    } ACCENT_POLICY;
    
    typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, void*);
    
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        pSetWindowCompositionAttribute setWindowComposition = 
            (pSetWindowCompositionAttribute)GetProcAddress(user32, "SetWindowCompositionAttribute");
        
        if (setWindowComposition) {
            ACCENT_POLICY policy = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 2, 0x70000000, 0 };
            setWindowComposition(hwnd_, &policy);
        }
        FreeLibrary(user32);
    }
}

void GlassWindow::SetWindowCorner(int preference) {
    typedef enum DWM_WINDOW_CORNER_PREFERENCE {
        DWMWCP_DEFAULT = 0,
        DWMWCP_DONOTROUND = 1,
        DWMWCP_ROUND = 2,
        DWMWCP_ROUNDSMALL = 3
    } DWM_WINDOW_CORNER_PREFERENCE;
    
    typedef HRESULT(WINAPI* pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    
    HMODULE dwmapi = GetModuleHandleW(L"dwmapi.dll");
    if (dwmapi) {
        pDwmSetWindowAttribute DwmSetWindowAttribute = 
            (pDwmSetWindowAttribute)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
        
        if (DwmSetWindowAttribute) {
            DWM_WINDOW_CORNER_PREFERENCE pref = (DWM_WINDOW_CORNER_PREFERENCE)preference;
            DwmSetWindowAttribute(hwnd_, 33, &pref, sizeof(pref));
        }
    }
}

void GlassWindow::SetSongTitle(const std::wstring& title) { songTitle_ = title; }
void GlassWindow::SetSongArtist(const std::wstring& artist) { songArtist_ = artist; }

void GlassWindow::SetProgress(double progress) {
    double oldProgress = progress_;
    progress_ = progress;
    progressAnim_.Start(oldProgress, progress);
}

void GlassWindow::SetTimeText(const std::wstring& text) { timeText_ = text; }

void GlassWindow::SetLyrics(const std::vector<LyricLine>& lyrics) {
    lyrics_ = lyrics;
}

void GlassWindow::SetCurrentTime(double time) {
    currentTime_ = time;
}

int GlassWindow::FindCurrentLyricIndex() {
    if (lyrics_.empty()) return -1;
    
    double currentTimeMs = currentTime_ * 1000.0;
    int found = -1;
    
    for (int i = (int)lyrics_.size() - 1; i >= 0; i--) {
        if (lyrics_[i].startTime <= currentTimeMs) {
            found = i;
            break;
        }
    }
    return found;
}

} // namespace gui
} // namespace moekoe

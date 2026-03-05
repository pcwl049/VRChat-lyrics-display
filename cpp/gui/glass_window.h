// glass_window.h - Glass effect window

#pragma once
#include "moekoe_ws.h"

namespace moekoe {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    static Color FromRGBA(int r, int g, int b, int a = 255) {
        return { r/255.0f, g/255.0f, b/255.0f, a/255.0f };
    }
};

namespace gui {

namespace easing {
    inline double easeOutCubic(double t) { return 1 - pow(1 - t, 3); }
    inline double easeOutBack(double t) { double c1 = 1.70158; double c3 = c1 + 1; return 1 + c3 * pow(t - 1, 3) + c1 * pow(t - 1, 2); }
    inline double easeOutElastic(double t) {
        double c4 = (2 * 3.14159) / 3;
        return t == 0 ? 0 : t == 1 ? 1 : pow(2, -10 * t) * sin((t * 10 - 0.75) * c4) + 1;
    }
}

struct Animation {
    double from = 0, to = 1, current = 0;
    int duration = 300;
    DWORD startTime = 0;
    double (*easingFunc)(double) = easing::easeOutCubic;
    bool running = false;
    
    void Start(double fromVal, double toVal) {
        from = fromVal; to = toVal;
        current = fromVal;
        startTime = GetTickCount();
        running = true;
    }
    void Update() {
        if (!running) return;
        DWORD elapsed = GetTickCount() - startTime;
        double t = (double)elapsed / duration;
        if (t >= 1) { current = to; running = false; }
        else { current = from + (to - from) * easingFunc(t); }
    }
};

class GlassWindow {
public:
    struct Config {
        int width = 380;
        int height = 320;
        Color bgColor = Color::FromRGBA(20, 20, 30, 200);
        Color accentColor = Color::FromRGBA(96, 205, 255, 255);
        Color textColor = Color::FromRGBA(255, 255, 255, 255);
    };
    
    GlassWindow(HWND hwnd, const Config& config);
    
    void Paint(HDC hdc);
    void Update();
    void EnableBlurBehind();
    void SetWindowCorner(int corner);
    
    void SetSongTitle(const std::wstring& title);
    void SetSongArtist(const std::wstring& artist);
    void SetProgress(double progress);
    void SetTimeText(const std::wstring& text);
    void SetLyrics(const std::vector<LyricLine>& lyrics);
    void SetCurrentTime(double time);
    
private:
    void DrawRoundRect(HDC hdc, int x, int y, int w, int h, int radius, Color color);
    void DrawText(HDC hdc, const std::wstring& text, RECT* rc, int format, Color color);
    void CreateFonts();
    int FindCurrentLyricIndex();
    
    HWND hwnd_;
    Config config_;
    
    std::wstring songTitle_ = L"Waiting...";
    std::wstring songArtist_ = L"";
    std::wstring timeText_ = L"0:00 / 0:00";
    double progress_ = 0;
    double currentTime_ = 0;
    
    std::vector<LyricLine> lyrics_;
    int currentLyricIndex_ = -1;
    int prevLyricIndex_ = -1;
    double lyricFadeProgress_ = 1.0;  // 0 = old lyric, 1 = new lyric
    
    Animation fadeAnim_;
    Animation progressAnim_;
    Animation lyricAnim_;  // For lyric transition
    
    HFONT titleFont_ = nullptr;
    HFONT artistFont_ = nullptr;
    HFONT timeFont_ = nullptr;
    HFONT lyricFont_ = nullptr;
};

} // namespace gui
} // namespace moekoe

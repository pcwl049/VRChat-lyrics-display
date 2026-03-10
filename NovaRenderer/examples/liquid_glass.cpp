#include <Nova/Renderer.h>
#include <dwmapi.h>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "NovaCore.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Nova;

struct AppState { float time = 0; };

enum ACCENT_STATE { ACCENT_ENABLE_ACRYLICBLURBEHIND = 4 };
struct ACCENT_POLICY { ACCENT_STATE s; DWORD f, c, a; };
struct WCA_DATA { int a; void* d; size_t n; };
typedef BOOL(WINAPI* pfnSWCA)(HWND, WCA_DATA*);

void EnableAcrylic(HWND hwnd) {
    HMODULE h = GetModuleHandleW(L"user32.dll");
    auto fn = (pfnSWCA)GetProcAddress(h, "SetWindowCompositionAttribute");
    if (fn) {
        ACCENT_POLICY p = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 2, 0x99000000, 0 };
        WCA_DATA d = { 19, &p, sizeof(p) };
        fn(hwnd, &d);
    }
}

void onRender(const FrameContext& ctx) {
    AppState* st = (AppState*)GetRenderer()->getWindow()->userData;
    st->time += ctx.deltaTime;
    float w = ctx.screenWidth, h = ctx.screenHeight, t = st->time;
    
    // 不清屏，让 Windows Acrylic 效果显示
    
    float px = 30, py = 30, pw = w - 60, ph = h - 60, rad = 20;
    
    // Glow border
    float glow = 0.3f + 0.2f * sinf(t * 2);
    for (int i = 0; i < 4; i++) {
        Color gc = {0.5f, 0.7f, 1.0f, glow * (1 - i * 0.2f)};
        float off = i * 2;
        GetRenderer()->drawRoundRect({px - off, py - off, pw + off*2, ph + off*2}, rad + off, gc);
    }
    
    // Main glass
    GetRenderer()->drawRoundRect({px, py, pw, ph}, rad, Color(1, 1, 1, 0.08f));
    
    // Top highlight
    for (int i = 0; i < 6; i++) {
        float y = py + 10 + i * 5;
        float a = (0.12f - i * 0.018f) * (0.7f + 0.3f * sinf(t * 1.5f + i * 0.5f));
        GetRenderer()->drawRect({px + 20, y, pw - 40, 3}, Color(1, 1, 1, a));
    }
    
    // Dynamic spot
    float sx = px + pw * (0.2f + 0.15f * sinf(t * 0.7f));
    float sy = py + ph * 0.2f;
    for (int i = 0; i < 5; i++) {
        float r = 50 + 20 * sinf(t * 3) - i * 8;
        GetRenderer()->drawCircle({sx, sy}, r, Color(1, 1, 1, 0.08f - i * 0.015f), 48);
    }
    
    // Content placeholders
    GetRenderer()->drawRect({px + 25, py + 25, pw * 0.5f, 12}, Color(1, 1, 1, 0.2f));
    GetRenderer()->drawRect({px + 25, py + 45, pw * 0.3f, 6}, Color(1, 1, 1, 0.12f));
    GetRenderer()->drawRect({px + 25, py + ph * 0.45f, pw - 50, 1}, Color(1, 1, 1, 0.08f));
    
    // Progress bar
    float progY = py + ph * 0.55f;
    float prog = 0.3f + 0.2f * sinf(t * 0.5f);
    GetRenderer()->drawRect({px + 25, progY, pw - 50, 6}, Color(1, 1, 1, 0.1f));
    GetRenderer()->drawRect({px + 25, progY, (pw - 50) * prog, 6}, Color(0.5f, 0.7f, 1, 0.8f));
    
    // FPS
    char buf[32]; snprintf(buf, 32, "FPS %.0f", 1.0f / ctx.deltaTime);
    GetRenderer()->drawText(buf, {px + 15, py + ph - 25}, Color(1, 1, 1, 0.4f), 11);
}

int main() {
    printf("=== Nova Glass Panel ===\nESC to exit, drag to move\n\n");
    
    RendererConfig cfg;
    cfg.window.title = "Glass";
    cfg.window.width = 420;
    cfg.window.height = 280;
    cfg.window.decorated = false;
    cfg.window.transparent = true;  // 启用透明窗口
    cfg.enableValidation = false;
    cfg.vSync = true;
    
    Renderer renderer;
    auto r = renderer.initialize(cfg);
    if (!r.success) { printf("Failed: %s\n", r.error.c_str()); return 1; }
    
    EnableAcrylic(renderer.getWindow()->getHWND());
    SetLayeredWindowAttributes(renderer.getWindow()->getHWND(), 0, 255, LWA_ALPHA);
    
    printf("GPU: %s\n", renderer.getBackend()->getDeviceProperties().deviceName);
    
    AppState st;
    renderer.getWindow()->userData = &st;
    renderer.setRenderCallback(onRender);
    renderer.getInput()->setKeyCallback([&](const KeyEvent& e) {
        if (e.action == InputAction::Press && e.key == KeyCode::Escape) GetRenderer()->stop();
    });
    
    renderer.run();
    return 0;
}
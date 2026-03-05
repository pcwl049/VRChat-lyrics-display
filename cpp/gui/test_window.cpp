// test_window.cpp - Test GlassWindow

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string>
#include "glass_window.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    moekoe::gui::GlassWindow::Config config;
    config.width = 380;
    config.height = 280;
    config.x = 100;
    config.y = 100;
    config.alwaysOnTop = true;
    config.accentColor = moekoe::gui::Color::FromRGBA(96, 205, 255, 255);
    
    moekoe::gui::GlassWindow window(config);
    
    if (!window.Create()) {
        wchar_t msg[128];
        swprintf_s(msg, L"CreateWindow failed: %d", GetLastError());
        MessageBoxW(nullptr, msg, L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    window.SetSongTitle(L"Test Song");
    window.SetSongArtist(L"Test Artist");
    window.SetProgress(0.5);
    window.SetTimeText(L"1:30 / 3:00");
    window.SetStats(5, 42);
    
    window.Show();
    
    SetTimer(window.GetHandle(), 1, 16, nullptr);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    KillTimer(window.GetHandle(), 1);
    return (int)msg.wParam;
}
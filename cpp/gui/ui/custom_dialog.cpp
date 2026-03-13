// custom_dialog.cpp - 自定义对话框实现
#include "custom_dialog.h"
#include "draw_helpers.h"
#include "../common/logger.h"

// 外部依赖（来自 main_gui.cpp）
extern HWND g_hwnd;                    // 主窗口句柄
extern HFONT g_fontSubtitle;          // 副标题字体
extern HFONT g_fontNormal;            // 正文字体

// 主题颜色宏（与 main_gui.cpp 一致）
#define COLOR_BG RGB(18, 18, 24)
#define COLOR_CARD RGB(30, 40, 60)
#define COLOR_ACCENT RGB(80, 180, 255)
#define COLOR_ERROR RGB(255, 100, 100)
#define COLOR_WARNING RGB(255, 180, 50)
#define COLOR_TEXT RGB(240, 245, 255)
#define COLOR_TEXT_DIM RGB(140, 150, 170)
#define COLOR_BORDER RGB(50, 70, 100)
#define COLOR_BTN_BG RGB(40, 40, 55)
#define COLOR_BTN_HOVER RGB(55, 55, 75)

// 对话框全局状态定义
HWND g_dialogHwnd = nullptr;
int g_dialogResult = 0;
bool g_dialogClosed = false;
int g_dialogBtnHover = -1;
DialogConfig g_dialogConfig;
int g_dialogWidth = 400;
int g_dialogHeight = 200;
Animation g_dialogFadeAnim;
Animation g_dialogScaleAnim;
bool g_dialogAnimComplete = false;

// 毛玻璃效果（来自 main_gui.cpp）
extern void EnableBlurBehind(HWND hwnd);

// ============================================================================
// 对话框窗口过程
// ============================================================================

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            EnableBlurBehind(hwnd);
            SetTimer(hwnd, 1, 16, nullptr);
            
            g_dialogAnimComplete = false;
            g_dialogFadeAnim.value = 0.0;
            g_dialogFadeAnim.target = 1.0;
            g_dialogFadeAnim.speed = 0.1;
            g_dialogScaleAnim.value = 0.92;
            g_dialogScaleAnim.target = 1.0;
            g_dialogScaleAnim.speed = 0.15;
            
            SetLayeredWindowAttributes(hwnd, 0, 1, LWA_ALPHA);
            return 0;
        }
        case WM_TIMER: {
            if (!g_dialogAnimComplete) {
                g_dialogFadeAnim.update();
                g_dialogScaleAnim.update();
                
                BYTE alpha = (BYTE)(g_dialogFadeAnim.value * 255);
                SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
                
                if (!g_dialogFadeAnim.isActive() && !g_dialogScaleAnim.isActive()) {
                    g_dialogAnimComplete = true;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
            
            double scale = g_dialogScaleAnim.value;
            int drawW = (int)(w * scale);
            int drawH = (int)(h * scale);
            int offsetX = (w - drawW) / 2;
            int offsetY = (h - drawH) / 2;
            
            HBRUSH bgBrush = CreateSolidBrush(COLOR_BG);
            FillRect(memDC, &rc, bgBrush);
            DeleteObject(bgBrush);
            
            HDC tempDC = CreateCompatibleDC(hdc);
            HBITMAP tempBmp = CreateCompatibleBitmap(hdc, w, h);
            HBITMAP oldTempBmp = (HBITMAP)SelectObject(tempDC, tempBmp);
            
            // 绘制卡片背景
            DrawRoundRect(tempDC, 0, 0, w, h, 16, COLOR_CARD);
            
            // 强调条 - 根据类型选择颜色
            COLORREF accentColor = COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) accentColor = COLOR_ERROR;
            else if (g_dialogConfig.type == DIALOG_CONFIRM) accentColor = COLOR_WARNING;
            DrawRoundRect(tempDC, 0, 0, 5, h, 2, accentColor);
            
            // 标题
            SetTextColor(tempDC, COLOR_TEXT);
            SetBkMode(tempDC, TRANSPARENT);
            HFONT titleFont = g_fontSubtitle ? g_fontSubtitle : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(tempDC, titleFont);
            if (!g_dialogConfig.title.empty()) {
                TextOutW(tempDC, 30, 25, g_dialogConfig.title.c_str(), (int)g_dialogConfig.title.length());
            }
            SelectObject(tempDC, oldFont);
            
            // 内容（支持自动换行）
            HFONT contentFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(tempDC, contentFont);
            int lineY = 70;
            int lineH = 32;
            int maxWidth = w - 60;
            
            std::wstring content = g_dialogConfig.content;
            size_t pos = 0;
            while (pos < content.length()) {
                size_t nextPos = content.find(L'\n', pos);
                if (nextPos == std::wstring::npos) nextPos = content.length();
                std::wstring line = content.substr(pos, nextPos - pos);
                pos = nextPos + 1;
                
                if (line.length() > 0) {
                    SIZE textSize;
                    GetTextExtentPoint32W(tempDC, line.c_str(), (int)line.length(), &textSize);
                    
                    if (textSize.cx > maxWidth && line.length() > 1) {
                        std::wstring wrappedLine;
                        for (size_t i = 0; i < line.length(); i++) {
                            std::wstring testLine = wrappedLine + line[i];
                            GetTextExtentPoint32W(tempDC, testLine.c_str(), (int)testLine.length(), &textSize);
                            
                            if (textSize.cx > maxWidth && !wrappedLine.empty()) {
                                TextOutW(tempDC, 30, lineY, wrappedLine.c_str(), (int)wrappedLine.length());
                                lineY += lineH;
                                wrappedLine = line[i];
                            } else {
                                wrappedLine += line[i];
                            }
                        }
                        if (!wrappedLine.empty()) {
                            TextOutW(tempDC, 30, lineY, wrappedLine.c_str(), (int)wrappedLine.length());
                            lineY += lineH;
                        }
                    } else {
                        TextOutW(tempDC, 30, lineY, line.c_str(), (int)line.length());
                        lineY += lineH;
                    }
                }
            }
            SelectObject(tempDC, oldFont);
            
            // 按钮
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            // 按钮1（主按钮）
            COLORREF btn1Bg = (g_dialogBtnHover == 0) ? RGB(110, 190, 255) : COLOR_ACCENT;
            if (g_dialogConfig.type == DIALOG_ERROR) btn1Bg = (g_dialogBtnHover == 0) ? RGB(255, 120, 120) : COLOR_ERROR;
            DrawRoundRect(tempDC, btn1X, btnY, btnW, btnH, 8, btn1Bg);
            HFONT btnFont = g_fontNormal ? g_fontNormal : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            oldFont = (HFONT)SelectObject(tempDC, btnFont);
            SetTextColor(tempDC, RGB(0, 0, 0));
            RECT btn1Rc = {btn1X, btnY, btn1X + btnW, btnY + btnH};
            if (!g_dialogConfig.btn1Text.empty()) {
                DrawTextW(tempDC, g_dialogConfig.btn1Text.c_str(), (int)g_dialogConfig.btn1Text.length(), &btn1Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            
            // 按钮2（次按钮）
            if (g_dialogConfig.hasBtn2) {
                COLORREF btn2Bg = (g_dialogBtnHover == 1) ? COLOR_BTN_HOVER : COLOR_BTN_BG;
                DrawRoundRect(tempDC, btn2X, btnY, btnW, btnH, 8, btn2Bg);
                HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
                HPEN oldPen = (HPEN)SelectObject(tempDC, borderPen);
                SelectObject(tempDC, GetStockObject(NULL_BRUSH));
                RoundRect(tempDC, btn2X, btnY, btn2X + btnW, btnY + btnH, 16, 16);
                SelectObject(tempDC, oldPen);
                DeleteObject(borderPen);
                SetTextColor(tempDC, COLOR_TEXT);
                RECT btn2Rc = {btn2X, btnY, btn2X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn2Text.empty()) {
                    DrawTextW(tempDC, g_dialogConfig.btn2Text.c_str(), (int)g_dialogConfig.btn2Text.length(), &btn2Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            // 按钮3
            if (g_dialogConfig.hasBtn3) {
                COLORREF btn3Bg = (g_dialogBtnHover == 2) ? COLOR_BTN_HOVER : COLOR_BTN_BG;
                DrawRoundRect(tempDC, btn3X, btnY, btnW, btnH, 8, btn3Bg);
                SetTextColor(tempDC, COLOR_TEXT_DIM);
                RECT btn3Rc = {btn3X, btnY, btn3X + btnW, btnY + btnH};
                if (!g_dialogConfig.btn3Text.empty()) {
                    DrawTextW(tempDC, g_dialogConfig.btn3Text.c_str(), (int)g_dialogConfig.btn3Text.length(), &btn3Rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
            
            SelectObject(tempDC, oldFont);
            
            SetStretchBltMode(memDC, HALFTONE);
            StretchBlt(memDC, offsetX, offsetY, drawW, drawH, tempDC, 0, 0, w, h, SRCCOPY);
            
            SelectObject(tempDC, oldTempBmp);
            DeleteObject(tempBmp);
            DeleteDC(tempDC);
            
            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
                btn3X = btn2X;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            int oldHover = g_dialogBtnHover;
            if (x >= btn1X && x < btn1X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 0;
            } else if (g_dialogConfig.hasBtn2 && x >= btn2X && x < btn2X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 1;
            } else if (g_dialogConfig.hasBtn3 && x >= btn3X && x < btn3X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogBtnHover = 2;
            } else {
                g_dialogBtnHover = -1;
            }
            if (oldHover != g_dialogBtnHover) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_LBUTTONUP: {
            int x = LOWORD(lParam), y = HIWORD(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            
            int btnW = 110, btnH = 42;
            int btnY = h - 65;
            int btn1X, btn2X, btn3X;
            
            if (g_dialogConfig.hasBtn3) {
                btnW = 100;
                int totalW = btnW * 3 + 30;
                btn1X = (w - totalW) / 2;
                btn2X = btn1X + btnW + 10;
                btn3X = btn2X + btnW + 10;
            } else if (g_dialogConfig.hasBtn2) {
                btn1X = w/2 - btnW - 15;
                btn2X = w/2 + 15;
                btn3X = btn2X;
            } else {
                btn1X = w/2 - btnW/2;
                btn2X = btn1X;
                btn3X = btn1X;
            }
            
            if (x >= btn1X && x < btn1X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 1;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (g_dialogConfig.hasBtn2 && x >= btn2X && x < btn2X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 2;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (g_dialogConfig.hasBtn3 && x >= btn3X && x < btn3X + btnW && y >= btnY && y < btnY + btnH) {
                g_dialogResult = 3;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                g_dialogResult = 0;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            } else if (wParam == VK_RETURN) {
                g_dialogResult = 1;
                g_dialogClosed = true;
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY: {
            KillTimer(hwnd, 1);
            g_dialogHwnd = nullptr;
            return 0;
        }
        case WM_CLOSE: {
            g_dialogResult = 0;
            g_dialogClosed = true;
            DestroyWindow(hwnd);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
// 对话框显示函数
// ============================================================================

int ShowCustomDialog(const DialogConfig& config) {
    g_dialogConfig = config;
    g_dialogBtnHover = -1;
    
    int lineCount = 1;
    for (size_t i = 0; i < config.content.length(); i++) {
        if (config.content[i] == L'\n') lineCount++;
    }
    g_dialogWidth = 520;
    g_dialogHeight = 160 + lineCount * 32 + 30;
    if (g_dialogHeight < 220) g_dialogHeight = 220;
    
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"VRCLyricsDialog_Class";
        RegisterClassExW(&wc);
        registered = true;
    }
    
    int x, y;
    if (g_hwnd && IsWindow(g_hwnd)) {
        RECT parentRect;
        GetWindowRect(g_hwnd, &parentRect);
        int parentW = parentRect.right - parentRect.left;
        int parentH = parentRect.bottom - parentRect.top;
        x = parentRect.left + (parentW - g_dialogWidth) / 2;
        y = parentRect.top + (parentH - g_dialogHeight) / 2;
    } else {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        x = (screenW - g_dialogWidth) / 2;
        y = (screenH - g_dialogHeight) / 2;
    }
    
    g_dialogHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"VRCLyricsDialog_Class",
        config.title.c_str(),
        WS_POPUP,
        x, y, g_dialogWidth, g_dialogHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    
    if (!g_dialogHwnd) {
        return 0;
    }
    
    // 圆角
    typedef HRESULT(WINAPI* DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        auto fn = (DwmSetWindowAttribute_t)GetProcAddress(hDwm, "DwmSetWindowAttribute");
        if (fn) {
            int corner = 2;
            fn(g_dialogHwnd, 33, &corner, sizeof(corner));
        }
        FreeLibrary(hDwm);
    }
    
    g_dialogFadeAnim.value = 0.0;
    g_dialogFadeAnim.target = 1.0;
    g_dialogFadeAnim.speed = 0.12;
    g_dialogScaleAnim.value = 0.85;
    g_dialogScaleAnim.target = 1.0;
    g_dialogScaleAnim.speed = 0.18;
    g_dialogAnimComplete = false;
    
    SetLayeredWindowAttributes(g_dialogHwnd, 0, 1, LWA_ALPHA);
    ShowWindow(g_dialogHwnd, SW_SHOW);
    SetForegroundWindow(g_dialogHwnd);
    UpdateWindow(g_dialogHwnd);
    
    MSG msg;
    g_dialogClosed = false;
    while (!g_dialogClosed && g_dialogHwnd) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                break;
            }
            if (!IsDialogMessageW(g_dialogHwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            WaitMessage();
        }
    }
    
    if (g_dialogHwnd && IsWindow(g_dialogHwnd)) {
        DestroyWindow(g_dialogHwnd);
        g_dialogHwnd = nullptr;
    }
    
    return g_dialogResult;
}

// ============================================================================
// 便捷函数
// ============================================================================

bool ShowInfoDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_INFO;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x786E\x5B9A";  // 确定
    config.hasBtn2 = false;
    return ShowCustomDialog(config) > 0;
}

bool ShowErrorDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_ERROR;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x786E\x5B9A";  // 确定
    config.hasBtn2 = false;
    return ShowCustomDialog(config) > 0;
}

bool ShowConfirmDialog(const std::wstring& title, const std::wstring& content, const std::wstring& btnYes, const std::wstring& btnNo) {
    DialogConfig config;
    config.type = DIALOG_CONFIRM;
    config.title = title;
    config.content = content;
    config.btn1Text = btnYes;
    config.btn2Text = btnNo;
    config.hasBtn2 = true;
    return ShowCustomDialog(config) == 1;
}

int ShowUpdateDialog(const std::wstring& title, const std::wstring& content) {
    DialogConfig config;
    config.type = DIALOG_UPDATE;
    config.title = title;
    config.content = content;
    config.btn1Text = L"\x66F4\x65B0";  // 更新
    config.btn2Text = L"\x53D6\x6D88";  // 取消
    config.btn3Text = L"\x8DF3\x8FC7";  // 跳过
    config.hasBtn2 = true;
    config.hasBtn3 = true;
    return ShowCustomDialog(config);
}

// 外部依赖：IsRunningAsAdmin
extern bool IsRunningAsAdmin();

bool ShowFirstRunDialog() {
    DialogConfig config;
    config.type = DIALOG_INFO;
    config.title = L"VRChat Lyrics Display";
    
    bool isAdmin = IsRunningAsAdmin();
    std::wstring content;
    if (!isAdmin) {
        content = L"\x5EFA\x8BAE\x4EE5\x7BA1\x7406\x5458\x8EAB\x4EFD\x8FD0\x884C\x4EE5\x4FDD\x8BC1\x529F\x80FD\x6B63\x5E38\n\n";
    }
    content += L"\x8BF7\x9009\x62E9\x5173\x95ED\x65B9\x5F0F:\n";
    content += L"\x2022 \x6700\x5C0F\x5316\x5230\x6258\x76D8 \x2212 \x7EE7\x7EED\x540E\x53F0\x8FD0\x884C\n";
    content += L"\x2022 \x76F4\x63A5\x9000\x51FA \x2212 \x5173\x95ED\x7A0B\x5E8F\n\n";
    content += L"\x53EF\x5728\x8BBE\x7F6E\x4E2D\x968F\x65F6\x66F4\x6539";
    config.content = content;
    config.btn1Text = L"\x6258\x76D8";  // 托盘
    config.btn2Text = L"\x9000\x51FA";    // 退出
    config.hasBtn2 = true;
    int result = ShowCustomDialog(config);
    return result == 1;
}

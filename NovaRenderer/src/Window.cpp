/**
 * Nova Renderer - Window System Implementation
 */

#include "Nova/Window.h"
#include <windowsx.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace Nova {

std::unordered_map<HWND, Window*> Window::s_windows;

Window::Window() = default;

Window::~Window() {
    destroy();
}

bool Window::create(const WindowConfig& config) {
    if (valid_) return true;
    
    config_ = config;
    
    // 注册窗口类
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = L"NovaWindowClass";
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        // 类可能已注册
    }
    
    // 计算窗口大小
    DWORD style = WS_POPUP;
    if (config.decorated) {
        style = WS_OVERLAPPEDWINDOW;
        if (!config.resizable) {
            style &= ~WS_THICKFRAME;
            style &= ~WS_MAXIMIZEBOX;
        }
    }
    
    RECT rect = {0, 0, (LONG)config.width, (LONG)config.height};
    AdjustWindowRect(&rect, style, FALSE);
    
    i32 windowWidth = rect.right - rect.left;
    i32 windowHeight = rect.bottom - rect.top;
    
    // 计算位置
    i32 x = config.x;
    i32 y = config.y;
    if (x < 0 || y < 0) {
        // 居中
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        x = (screenWidth - windowWidth) / 2;
        y = (screenHeight - windowHeight) / 2;
    }
    
    // 创建窗口
    std::wstring wtitle(config.title.begin(), config.title.end());
    
    DWORD exStyle = 0;
    if (config.transparent) {
        // 透明窗口：使用 WS_EX_LAYERED，不用 WS_EX_NOREDIRECTIONBITMAP
        exStyle = WS_EX_LAYERED;
    } else if (!config.decorated) {
        // 无边框不透明窗口：避免黑色背景
        exStyle = WS_EX_NOREDIRECTIONBITMAP;
    }
    
    hwnd_ = CreateWindowExW(
        exStyle,
        L"NovaWindowClass",
        wtitle.c_str(),
        style,
        x, y,
        windowWidth, windowHeight,
        nullptr, nullptr,
        hInstance,
        nullptr
    );
    
    if (!hwnd_) {
        return false;
    }
    
    // 设置透明度
    if (config.transparent && config.opacity < 1.0f) {
        SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), (BYTE)(config.opacity * 255), LWA_ALPHA);
    }
    
    // 注册窗口
    s_windows[hwnd_] = this;
    
    // 获取 DPI
    updateDPI();
    
    // 显示窗口
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    
    // 启用 Windows Acrylic 效果（无边框透明窗口）
    if (!config.decorated && config.transparent) {
        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hwnd_, &margins);
        
        // Windows 11 Mica 效果
        int micaAttr = 2;  // MICA
        DwmSetWindowAttribute(hwnd_, 1029, &micaAttr, sizeof(micaAttr));
        
        // 设置窗口为分层窗口，alpha 255 = 完全不透明（让 DWM 处理透明度）
        SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    }
    
    valid_ = true;
    
    // 发送创建事件
    handleEvent({WindowEvent::Create, config.width, config.height, x, y, dpi_, true});
    
    return true;
}

void Window::destroy() {
    if (!valid_) return;
    
    // 发送关闭事件
    handleEvent({WindowEvent::Close, 0, 0, 0, 0, dpi_, false});
    
    s_windows.erase(hwnd_);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    valid_ = false;
}

bool Window::isValid() const {
    return valid_ && hwnd_ != nullptr;
}

bool Window::pollEvents() {
    if (!valid_) return false;
    
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    
    return true;
}

void Window::waitEvents() {
    WaitMessage();
    pollEvents();
}

void Window::waitEventsTimeout(f64 timeout) {
    MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)(timeout * 1000), QS_ALLINPUT);
    pollEvents();
}

u32 Window::getWidth() const {
    return config_.width;
}

u32 Window::getHeight() const {
    return config_.height;
}

Vec2 Window::getSize() const {
    return {(f32)config_.width, (f32)config_.height};
}

Vec2 Window::getPosition() const {
    RECT rect;
    GetWindowRect(hwnd_, &rect);
    return {(f32)rect.left, (f32)rect.top};
}

std::string Window::getTitle() const {
    return config_.title;
}

bool Window::isFullscreen() const {
    return config_.fullscreen;
}

bool Window::isMinimized() const {
    return minimized_;
}

bool Window::isMaximized() const {
    return maximized_;
}

bool Window::isFocused() const {
    return focused_;
}

f32 Window::getDPI() const {
    return dpi_;
}

f32 Window::getContentScale() const {
    return contentScale_;
}

void Window::setSize(u32 width, u32 height) {
    config_.width = width;
    config_.height = height;
    
    RECT rect;
    GetWindowRect(hwnd_, &rect);
    
    RECT clientRect = {0, 0, (LONG)width, (LONG)height};
    AdjustWindowRect(&clientRect, GetWindowLong(hwnd_, GWL_STYLE), FALSE);
    
    SetWindowPos(hwnd_, nullptr, rect.left, rect.top,
                 clientRect.right - clientRect.left,
                 clientRect.bottom - clientRect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::setPosition(i32 x, i32 y) {
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Window::setTitle(const std::string& title) {
    config_.title = title;
    std::wstring wtitle(title.begin(), title.end());
    SetWindowTextW(hwnd_, wtitle.c_str());
}

void Window::setFullscreen(bool fullscreen) {
    if (config_.fullscreen == fullscreen) return;
    
    config_.fullscreen = fullscreen;
    
    if (fullscreen) {
        // 保存当前窗口状态
        GetWindowRect(hwnd_, &windowedRect_);
        
        // 设置全屏
        SetWindowLong(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, 0, 0,
                     GetSystemMetrics(SM_CXSCREEN),
                     GetSystemMetrics(SM_CYSCREEN),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        // 恢复窗口
        SetWindowLong(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd_, nullptr,
                     windowedRect_.left, windowedRect_.top,
                     windowedRect_.right - windowedRect_.left,
                     windowedRect_.bottom - windowedRect_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void Window::setMinimized(bool minimized) {
    ShowWindow(hwnd_, minimized ? SW_MINIMIZE : SW_RESTORE);
}

void Window::setMaximized(bool maximized) {
    ShowWindow(hwnd_, maximized ? SW_MAXIMIZE : SW_RESTORE);
}

void Window::setOpacity(f32 opacity) {
    config_.opacity = opacity;
    if (config_.transparent) {
        SetLayeredWindowAttributes(hwnd_, RGB(0, 0, 0), (BYTE)(opacity * 255), LWA_ALPHA);
    }
}

void Window::setVisible(bool visible) {
    ShowWindow(hwnd_, visible ? SW_SHOW : SW_HIDE);
}

void Window::setAlwaysOnTop(bool top) {
    SetWindowPos(hwnd_, top ? HWND_TOPMOST : HWND_NOTOPMOST, 
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Window::setEventCallback(EventCallback callback) {
    eventCallback_ = std::move(callback);
}

Window* Window::getWindowFromHWND(HWND hwnd) {
    auto it = s_windows.find(hwnd);
    return it != s_windows.end() ? it->second : nullptr;
}

void Window::handleEvent(const WindowEventData& data) {
    if (eventCallback_) {
        eventCallback_(data);
    }
}

void Window::updateDPI() {
    // Windows 10 1703+ 支持 Per-Monitor DPI
    typedef UINT(WINAPI* GetDpiForWindow_t)(HWND);
    static auto getDpiForWindow = (GetDpiForWindow_t)GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    
    if (getDpiForWindow && hwnd_) {
        dpi_ = (f32)getDpiForWindow(hwnd_);
    } else {
        HDC hdc = GetDC(nullptr);
        dpi_ = (f32)GetDeviceCaps(hdc, LOGPIXELSX);
        ReleaseDC(nullptr, hdc);
    }
    
    contentScale_ = dpi_ / 96.0f;
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Window* window = getWindowFromHWND(hwnd);
    
    switch (msg) {
        case WM_CLOSE:
            if (window) {
                window->handleEvent({WindowEvent::Close, 0, 0, 0, 0, window->dpi_, false});
            }
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        case WM_SIZE: {
            if (window) {
                window->config_.width = LOWORD(lParam);
                window->config_.height = HIWORD(lParam);
                window->minimized_ = wParam == SIZE_MINIMIZED;
                window->maximized_ = wParam == SIZE_MAXIMIZED;
                
                WindowEvent event = WindowEvent::Resize;
                if (wParam == SIZE_MINIMIZED) event = WindowEvent::Minimize;
                else if (wParam == SIZE_MAXIMIZED) event = WindowEvent::Maximize;
                else if (wParam == SIZE_RESTORED && window->minimized_) event = WindowEvent::Restore;
                
                window->handleEvent({event, window->config_.width, window->config_.height,
                                    0, 0, window->dpi_, window->focused_});
            }
            break;
        }
        
        case WM_MOVE: {
            if (window) {
                window->handleEvent({WindowEvent::Move, 0, 0,
                                    (i32)(short)LOWORD(lParam), (i32)(short)HIWORD(lParam),
                                    window->dpi_, window->focused_});
            }
            break;
        }
        
        case WM_SETFOCUS:
            if (window) {
                window->focused_ = true;
                window->handleEvent({WindowEvent::Focus, 0, 0, 0, 0, window->dpi_, true});
            }
            break;
            
        case WM_KILLFOCUS:
            if (window) {
                window->focused_ = false;
                window->handleEvent({WindowEvent::Blur, 0, 0, 0, 0, window->dpi_, false});
            }
            break;
            
        case WM_DPICHANGED: {
            if (window) {
                window->dpi_ = (f32)HIWORD(wParam);
                window->contentScale_ = window->dpi_ / 96.0f;
                
                RECT* rect = (RECT*)lParam;
                SetWindowPos(hwnd, nullptr,
                             rect->left, rect->top,
                             rect->right - rect->left,
                             rect->bottom - rect->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                
                window->handleEvent({WindowEvent::DPIChange, 
                                    window->config_.width, window->config_.height,
                                    0, 0, window->dpi_, window->focused_});
            }
            break;
        }
        
        case WM_NCCALCSIZE: {
            if (window && !window->config_.decorated && wParam == TRUE) {
                // 无边框透明窗口的关键处理
                NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)lParam;
                params->rgrc[0] = params->rgrc[1];
                return 0;
            }
            break;
        }
        
        case WM_NCHITTEST: {
            if (window && !window->config_.decorated) {
                // 无边框窗口拖动
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                
                // 整个窗口可拖动（标题栏区域）
                if (pt.y < 40) {
                    return HTCAPTION;
                }
                return HTCLIENT;
            }
            break;
        }
    }
    
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace Nova

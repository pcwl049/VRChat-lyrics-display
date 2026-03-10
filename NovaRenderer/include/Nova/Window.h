/**
 * Nova Renderer - Window System
 * 窗口系统
 */

#pragma once

#include "Types.h"
#include <functional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Nova {

// 窗口配置
struct WindowConfig {
    std::string title = "Nova Window";
    u32 width = 800;
    u32 height = 600;
    bool fullscreen = false;
    bool resizable = true;
    bool decorated = true;
    bool transparent = false;
    f32 opacity = 1.0f;
    i32 x = -1;  // -1 = 居中
    i32 y = -1;
};

// 窗口事件
enum class WindowEvent {
    Create,
    Close,
    Resize,
    Move,
    Focus,
    Blur,
    Minimize,
    Maximize,
    Restore,
    DPIChange
};

struct WindowEventData {
    WindowEvent type;
    u32 width;
    u32 height;
    i32 x;
    i32 y;
    f32 dpi;
    bool focused;
};

// 窗口类
class Window {
public:
    Window();
    ~Window();
    
    // 创建/销毁
    bool create(const WindowConfig& config);
    void destroy();
    bool isValid() const;
    
    // 消息循环
    bool pollEvents();
    void waitEvents();
    void waitEventsTimeout(f64 timeout);
    
    // 属性
    u32 getWidth() const;
    u32 getHeight() const;
    Vec2 getSize() const;
    Vec2 getPosition() const;
    std::string getTitle() const;
    bool isFullscreen() const;
    bool isMinimized() const;
    bool isMaximized() const;
    bool isFocused() const;
    f32 getDPI() const;
    f32 getContentScale() const;
    
    // 设置
    void setSize(u32 width, u32 height);
    void setPosition(i32 x, i32 y);
    void setTitle(const std::string& title);
    void setFullscreen(bool fullscreen);
    void setMinimized(bool minimized);
    void setMaximized(bool maximized);
    void setOpacity(f32 opacity);
    void setVisible(bool visible);
    void setAlwaysOnTop(bool top);
    
    // 原生句柄
#ifdef _WIN32
    HWND getHWND() const { return hwnd_; }
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
    
    // 事件回调
    using EventCallback = std::function<void(const WindowEventData&)>;
    void setEventCallback(EventCallback callback);
    
    // 用户数据
    void* userData = nullptr;
    
private:
#ifdef _WIN32
    HWND hwnd_ = nullptr;
    RECT windowedRect_ = {0, 0, 0, 0};  // 用于保存窗口模式下的位置
    static Window* getWindowFromHWND(HWND hwnd);
    static std::unordered_map<HWND, Window*> s_windows;
#endif
    
    WindowConfig config_;
    EventCallback eventCallback_;
    bool valid_ = false;
    bool minimized_ = false;
    bool maximized_ = false;
    bool focused_ = true;
    f32 dpi_ = 96.0f;
    f32 contentScale_ = 1.0f;
    
    void handleEvent(const WindowEventData& data);
    void updateDPI();
};

} // namespace Nova

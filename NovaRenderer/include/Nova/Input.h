/**
 * Nova Renderer - Input System
 * 输入系统
 */

#pragma once

#include "Types.h"
#include <bitset>
#include <array>
#include <functional>

namespace Nova {

// 按键码
enum class KeyCode : u32 {
    Unknown = 0,
    
    // 字母
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    
    // 数字
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    
    // 功能键
    F1 = 112, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    
    // 特殊键
    Space = 32,
    Escape = 27,
    Enter = 13,
    Tab = 9,
    Backspace = 8,
    Insert = 45,
    Delete = 46,
    Home = 36,
    End = 35,
    PageUp = 33,
    PageDown = 34,
    
    // 方向键
    Left = 37,
    Up = 38,
    Right = 39,
    Down = 40,
    
    // 修饰键
    Shift = 16,
    Control = 17,
    Alt = 18,
    Super = 91,
    CapsLock = 20,
    NumLock = 144,
    
    // 小键盘
    Numpad0 = 96, Numpad1, Numpad2, Numpad3, Numpad4,
    Numpad5, Numpad6, Numpad7, Numpad8, Numpad9,
    NumpadAdd = 107,
    NumpadSubtract = 109,
    NumpadMultiply = 106,
    NumpadDivide = 111,
    NumpadDecimal = 110,
    NumpadEnter = 13,
    
    // 符号
    Minus = 189,
    Equals = 187,
    LeftBracket = 219,
    RightBracket = 221,
    Backslash = 220,
    Semicolon = 186,
    Apostrophe = 222,
    Comma = 188,
    Period = 190,
    Slash = 191,
    Grave = 192
};

// 鼠标按钮
enum class MouseButton : u32 {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
    X1 = 4,
    X2 = 5,
    Count = 6
};

// 输入动作
enum class InputAction {
    Press,
    Release,
    Repeat,
    Hold
};

// 键盘事件
struct KeyEvent {
    KeyCode key;
    InputAction action;
    u32 modifiers;
    bool shift;
    bool ctrl;
    bool alt;
    bool super;
    u32 scanCode;
};

// 鼠标事件
struct MouseEvent {
    MouseButton button;
    InputAction action;
    Vec2 position;
    Vec2 delta;
    i32 wheel;
    u32 modifiers;
};

// 鼠标移动事件
struct MouseMoveEvent {
    Vec2 position;
    Vec2 delta;
    bool dragged;
    MouseButton dragButton;
};

// 触摸事件
struct TouchEvent {
    u32 id;
    InputAction action;
    Vec2 position;
    Vec2 delta;
    f32 pressure;
};

// 文本输入事件
struct TextInputEvent {
    std::string text;
    u32 cursor;
    u32 selection;
};

// 输入修饰键标志
namespace Modifiers {
    constexpr u32 None = 0;
    constexpr u32 Shift = 1 << 0;
    constexpr u32 Control = 1 << 1;
    constexpr u32 Alt = 1 << 2;
    constexpr u32 Super = 1 << 3;
    constexpr u32 CapsLock = 1 << 4;
    constexpr u32 NumLock = 1 << 5;
}

// 输入回调类型
using KeyCallback = std::function<void(const KeyEvent&)>;
using MouseCallback = std::function<void(const MouseEvent&)>;
using MouseMoveCallback = std::function<void(const MouseMoveEvent&)>;
using TouchCallback = std::function<void(const TouchEvent&)>;
using TextInputCallback = std::function<void(const TextInputEvent&)>;

// 输入管理器
class InputManager {
public:
    InputManager();
    ~InputManager();
    
    // 帧更新
    void beginFrame();
    void endFrame();
    
    // 键盘状态
    bool isKeyDown(KeyCode key) const;
    bool isKeyUp(KeyCode key) const;
    bool isKeyPressed(KeyCode key) const;  // 本帧按下
    bool isKeyReleased(KeyCode key) const; // 本帧释放
    bool isKeyHeld(KeyCode key) const;     // 持续按下
    
    // 鼠标状态
    bool isMouseButtonDown(MouseButton button) const;
    bool isMouseButtonUp(MouseButton button) const;
    bool isMouseButtonPressed(MouseButton button) const;
    bool isMouseButtonReleased(MouseButton button) const;
    
    Vec2 getMousePosition() const;
    Vec2 getMouseDelta() const;
    Vec2 getMouseScroll() const;
    bool isMouseDragging() const;
    MouseButton getDragButton() const;
    
    // 设置
    void setMousePosition(const Vec2& pos);
    void setMouseVisible(bool visible);
    void setMouseGrabbed(bool grabbed);
    bool isMouseVisible() const;
    bool isMouseGrabbed() const;
    
    // 回调注册
    void setKeyCallback(KeyCallback callback);
    void setMouseCallback(MouseCallback callback);
    void setMouseMoveCallback(MouseMoveCallback callback);
    void setTouchCallback(TouchCallback callback);
    void setTextInputCallback(TextInputCallback callback);
    
    // 内部事件处理 (由窗口系统调用)
    void processKeyEvent(const KeyEvent& event);
    void processMouseEvent(const MouseEvent& event);
    void processMouseMove(const MouseMoveEvent& event);
    void processTouchEvent(const TouchEvent& event);
    void processTextInput(const TextInputEvent& event);
    
    // 平台接口
#ifdef _WIN32
    void processWindowsMessage(void* hwnd, u32 msg, u64 wParam, i64 lParam);
#endif
    
private:
    // 键盘状态
    std::bitset<256> keyState_;
    std::bitset<256> keyStatePrev_;
    std::bitset<256> keyPressed_;
    std::bitset<256> keyReleased_;
    
    // 鼠标状态
    std::bitset<static_cast<size_t>(MouseButton::Count)> mouseState_;
    std::bitset<static_cast<size_t>(MouseButton::Count)> mouseStatePrev_;
    std::bitset<static_cast<size_t>(MouseButton::Count)> mousePressed_;
    std::bitset<static_cast<size_t>(MouseButton::Count)> mouseReleased_;
    
    Vec2 mousePos_;
    Vec2 mousePosPrev_;
    Vec2 mouseDelta_;
    Vec2 mouseScroll_;
    
    bool mouseDragging_ = false;
    MouseButton dragButton_ = MouseButton::None;
    bool mouseVisible_ = true;
    bool mouseGrabbed_ = false;
    
    // 回调
    KeyCallback keyCallback_;
    MouseCallback mouseCallback_;
    MouseMoveCallback mouseMoveCallback_;
    TouchCallback touchCallback_;
    TextInputCallback textInputCallback_;
    
    // 平台数据
#ifdef _WIN32
    void* hwnd_ = nullptr;
#endif
};

// 快捷键管理
class HotkeyManager {
public:
    using HotkeyCallback = std::function<void()>;
    
    u32 registerHotkey(KeyCode key, u32 modifiers, HotkeyCallback callback);
    void unregisterHotkey(u32 id);
    void update(const InputManager& input);
    
private:
    struct Hotkey {
        u32 id;
        KeyCode key;
        u32 modifiers;
        HotkeyCallback callback;
    };
    
    std::vector<Hotkey> hotkeys_;
    u32 nextId_ = 1;
};

} // namespace Nova

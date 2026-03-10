/**
 * Nova Renderer - Input System Implementation
 */

#include "Nova/Input.h"
#include "Nova/Window.h"

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#endif

namespace Nova {

InputManager::InputManager() = default;

InputManager::~InputManager() = default;

void InputManager::beginFrame() {
    keyPressed_.reset();
    keyReleased_.reset();
    mousePressed_.reset();
    mouseReleased_.reset();
    mouseDelta_ = {0, 0};
    mouseScroll_ = {0, 0};
    
    keyStatePrev_ = keyState_;
    mouseStatePrev_ = mouseState_;
    mousePosPrev_ = mousePos_;
}

void InputManager::endFrame() {
    // 计算鼠标拖动
    if (!mouseDragging_ && isMouseButtonPressed(MouseButton::Left)) {
        mouseDragging_ = true;
        dragButton_ = MouseButton::Left;
    }
    
    if (mouseDragging_) {
        bool anyHeld = false;
        for (u32 i = 1; i < static_cast<u32>(MouseButton::Count); i++) {
            if (isMouseButtonDown(static_cast<MouseButton>(i))) {
                anyHeld = true;
                break;
            }
        }
        
        if (!anyHeld) {
            mouseDragging_ = false;
            dragButton_ = MouseButton::None;
        }
    }
}

bool InputManager::isKeyDown(KeyCode key) const {
    return keyState_[static_cast<size_t>(key)];
}

bool InputManager::isKeyUp(KeyCode key) const {
    return !keyState_[static_cast<size_t>(key)];
}

bool InputManager::isKeyPressed(KeyCode key) const {
    return keyPressed_[static_cast<size_t>(key)];
}

bool InputManager::isKeyReleased(KeyCode key) const {
    return keyReleased_[static_cast<size_t>(key)];
}

bool InputManager::isKeyHeld(KeyCode key) const {
    return keyState_[static_cast<size_t>(key)] && keyStatePrev_[static_cast<size_t>(key)];
}

bool InputManager::isMouseButtonDown(MouseButton button) const {
    return mouseState_[static_cast<size_t>(button)];
}

bool InputManager::isMouseButtonUp(MouseButton button) const {
    return !mouseState_[static_cast<size_t>(button)];
}

bool InputManager::isMouseButtonPressed(MouseButton button) const {
    return mousePressed_[static_cast<size_t>(button)];
}

bool InputManager::isMouseButtonReleased(MouseButton button) const {
    return mouseReleased_[static_cast<size_t>(button)];
}

Vec2 InputManager::getMousePosition() const {
    return mousePos_;
}

Vec2 InputManager::getMouseDelta() const {
    return mouseDelta_;
}

Vec2 InputManager::getMouseScroll() const {
    return mouseScroll_;
}

bool InputManager::isMouseDragging() const {
    return mouseDragging_;
}

MouseButton InputManager::getDragButton() const {
    return dragButton_;
}

void InputManager::setMousePosition(const Vec2& pos) {
    mousePos_ = pos;
#ifdef _WIN32
    if (hwnd_) {
        POINT pt = {(LONG)pos.x, (LONG)pos.y};
        ClientToScreen((HWND)hwnd_, &pt);
        SetCursorPos(pt.x, pt.y);
    }
#endif
}

void InputManager::setMouseVisible(bool visible) {
    mouseVisible_ = visible;
#ifdef _WIN32
    ShowCursor(visible ? TRUE : FALSE);
#endif
}

void InputManager::setMouseGrabbed(bool grabbed) {
    mouseGrabbed_ = grabbed;
#ifdef _WIN32
    if (hwnd_) {
        if (grabbed) {
            RECT rect;
            GetClientRect((HWND)hwnd_, &rect);
            MapWindowPoints((HWND)hwnd_, nullptr, (POINT*)&rect, 2);
            ClipCursor(&rect);
        } else {
            ClipCursor(nullptr);
        }
    }
#endif
}

bool InputManager::isMouseVisible() const {
    return mouseVisible_;
}

bool InputManager::isMouseGrabbed() const {
    return mouseGrabbed_;
}

void InputManager::setKeyCallback(KeyCallback callback) {
    keyCallback_ = std::move(callback);
}

void InputManager::setMouseCallback(MouseCallback callback) {
    mouseCallback_ = std::move(callback);
}

void InputManager::setMouseMoveCallback(MouseMoveCallback callback) {
    mouseMoveCallback_ = std::move(callback);
}

void InputManager::setTouchCallback(TouchCallback callback) {
    touchCallback_ = std::move(callback);
}

void InputManager::setTextInputCallback(TextInputCallback callback) {
    textInputCallback_ = std::move(callback);
}

void InputManager::processKeyEvent(const KeyEvent& event) {
    size_t index = static_cast<size_t>(event.key);
    
    if (index >= keyState_.size()) return;
    
    switch (event.action) {
        case InputAction::Press:
            keyState_[index] = true;
            keyPressed_[index] = true;
            break;
        case InputAction::Release:
            keyState_[index] = false;
            keyReleased_[index] = true;
            break;
        case InputAction::Repeat:
            // 重复按键
            break;
        case InputAction::Hold:
            // 持续按下
            break;
    }
    
    if (keyCallback_) {
        keyCallback_(event);
    }
}

void InputManager::processMouseEvent(const MouseEvent& event) {
    size_t index = static_cast<size_t>(event.button);
    
    if (index >= mouseState_.size()) return;
    
    switch (event.action) {
        case InputAction::Press:
            mouseState_[index] = true;
            mousePressed_[index] = true;
            break;
        case InputAction::Release:
            mouseState_[index] = false;
            mouseReleased_[index] = true;
            break;
        default:
            break;
    }
    
    if (mouseCallback_) {
        mouseCallback_(event);
    }
}

void InputManager::processMouseMove(const MouseMoveEvent& event) {
    mousePos_ = event.position;
    mouseDelta_ = event.delta;
    
    if (mouseMoveCallback_) {
        mouseMoveCallback_(event);
    }
}

void InputManager::processTouchEvent(const TouchEvent& event) {
    if (touchCallback_) {
        touchCallback_(event);
    }
}

void InputManager::processTextInput(const TextInputEvent& event) {
    if (textInputCallback_) {
        textInputCallback_(event);
    }
}

#ifdef _WIN32
void InputManager::processWindowsMessage(void* hwnd, u32 msg, u64 wParam, i64 lParam) {
    hwnd_ = hwnd;
    
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            KeyEvent event;
            event.key = static_cast<KeyCode>(wParam);
            event.action = (lParam & (1 << 30)) ? InputAction::Repeat : InputAction::Press;
            event.scanCode = (lParam >> 16) & 0xFF;
            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            event.super = (GetKeyState(VK_LWIN) & 0x8000) != 0 || 
                          (GetKeyState(VK_RWIN) & 0x8000) != 0;
            
            event.modifiers = 0;
            if (event.shift) event.modifiers |= Modifiers::Shift;
            if (event.ctrl) event.modifiers |= Modifiers::Control;
            if (event.alt) event.modifiers |= Modifiers::Alt;
            if (event.super) event.modifiers |= Modifiers::Super;
            
            processKeyEvent(event);
            break;
        }
        
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            KeyEvent event;
            event.key = static_cast<KeyCode>(wParam);
            event.action = InputAction::Release;
            event.scanCode = (lParam >> 16) & 0xFF;
            event.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            event.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            event.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            event.super = (GetKeyState(VK_LWIN) & 0x8000) != 0 || 
                          (GetKeyState(VK_RWIN) & 0x8000) != 0;
            
            event.modifiers = 0;
            if (event.shift) event.modifiers |= Modifiers::Shift;
            if (event.ctrl) event.modifiers |= Modifiers::Control;
            if (event.alt) event.modifiers |= Modifiers::Alt;
            if (event.super) event.modifiers |= Modifiers::Super;
            
            processKeyEvent(event);
            break;
        }
        
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            MouseEvent event;
            
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP) {
                event.button = MouseButton::Left;
            } else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) {
                event.button = MouseButton::Right;
            } else if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) {
                event.button = MouseButton::Middle;
            } else if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
                event.button = (HIWORD(wParam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;
            }
            
            event.action = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                           msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN)
                           ? InputAction::Press : InputAction::Release;
            
            event.position = {(f32)GET_X_LPARAM(lParam), (f32)GET_Y_LPARAM(lParam)};
            event.wheel = 0;
            event.modifiers = 0;
            if (wParam & MK_SHIFT) event.modifiers |= Modifiers::Shift;
            if (wParam & MK_CONTROL) event.modifiers |= Modifiers::Control;
            
            processMouseEvent(event);
            break;
        }
        
        case WM_MOUSEMOVE: {
            Vec2 newPos = {(f32)GET_X_LPARAM(lParam), (f32)GET_Y_LPARAM(lParam)};
            Vec2 delta = newPos - mousePos_;
            
            MouseMoveEvent event;
            event.position = newPos;
            event.delta = delta;
            event.dragged = mouseDragging_;
            event.dragButton = dragButton_;
            
            processMouseMove(event);
            break;
        }
        
        case WM_MOUSEWHEEL: {
            MouseEvent event;
            event.button = MouseButton::None;
            event.action = InputAction::Press;
            event.position = mousePos_;
            event.wheel = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            
            POINT pt = {(LONG)mousePos_.x, (LONG)mousePos_.y};
            ScreenToClient((HWND)hwnd, &pt);
            event.position = {(f32)pt.x, (f32)pt.y};
            
            mouseScroll_.y += (f32)event.wheel;
            
            processMouseEvent(event);
            break;
        }
        
        case WM_MOUSEHWHEEL: {
            MouseEvent event;
            event.button = MouseButton::None;
            event.action = InputAction::Press;
            event.position = mousePos_;
            event.wheel = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            
            POINT pt = {(LONG)mousePos_.x, (LONG)mousePos_.y};
            ScreenToClient((HWND)hwnd, &pt);
            event.position = {(f32)pt.x, (f32)pt.y};
            
            mouseScroll_.x += (f32)event.wheel;
            
            processMouseEvent(event);
            break;
        }
        
        case WM_CHAR: {
            TextInputEvent event;
            event.text = std::string(1, (char)wParam);
            event.cursor = 0;
            event.selection = 0;
            
            processTextInput(event);
            break;
        }
    }
}
#endif

// HotkeyManager
u32 HotkeyManager::registerHotkey(KeyCode key, u32 modifiers, HotkeyCallback callback) {
    Hotkey hk;
    hk.id = nextId_++;
    hk.key = key;
    hk.modifiers = modifiers;
    hk.callback = std::move(callback);
    hotkeys_.push_back(hk);
    return hk.id;
}

void HotkeyManager::unregisterHotkey(u32 id) {
    hotkeys_.erase(
        std::remove_if(hotkeys_.begin(), hotkeys_.end(),
            [id](const Hotkey& h) { return h.id == id; }),
        hotkeys_.end());
}

void HotkeyManager::update(const InputManager& input) {
    for (const auto& hk : hotkeys_) {
        if (input.isKeyPressed(hk.key)) {
            // 检查修饰键
            bool modsMatch = true;
            if ((hk.modifiers & Modifiers::Shift) && !input.isKeyDown(KeyCode::Shift))
                modsMatch = false;
            if ((hk.modifiers & Modifiers::Control) && !input.isKeyDown(KeyCode::Control))
                modsMatch = false;
            if ((hk.modifiers & Modifiers::Alt) && !input.isKeyDown(KeyCode::Alt))
                modsMatch = false;
            if ((hk.modifiers & Modifiers::Super) && !input.isKeyDown(KeyCode::Super))
                modsMatch = false;
            
            if (modsMatch && hk.callback) {
                hk.callback();
            }
        }
    }
}

} // namespace Nova

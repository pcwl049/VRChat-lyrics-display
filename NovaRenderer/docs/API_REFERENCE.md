# Nova Renderer API Reference

## 1. Overview

Nova Renderer is a modern, GPU-accelerated rendering framework built on Vulkan. It provides a high-level API for 2D graphics, UI rendering, and visual effects.

### Architecture

```
+------------------+
|    Application   |
+------------------+
         |
+------------------+
|    Renderer      |  <-- High-level API
+------------------+
         |
+------------------+
|  VulkanBackend   |  <-- Low-level Vulkan API
+------------------+
         |
+------------------+
|      Vulkan      |  <-- Graphics API
+------------------+
```

### Key Features

- GPU-accelerated rendering via Vulkan
- Built-in SDF (Signed Distance Field) text and shape rendering
- Efficient resource pooling with handle-based system
- Cross-platform window management
- Comprehensive input handling

---

## 2. Quick Start

### 2.1 Basic Application

```cpp
#include <Nova/Renderer.h>
#include <Nova/Window.h>
#include <Nova/Input.h>

using namespace Nova;

int main() {
    // Configure renderer
    RendererConfig config;
    config.window.title = "My App";
    config.window.width = 800;
    config.window.height = 600;
    config.enableValidation = true;
    config.vSync = true;
    
    // Create renderer
    Renderer renderer;
    auto result = renderer.initialize(config);
    
    if (!result.success) {
        printf("Failed: %s\n", result.error.c_str());
        return 1;
    }
    
    // Set render callback
    renderer.setRenderCallback([](const FrameContext& ctx) {
        GetRenderer()->clear({0.1f, 0.1f, 0.15f, 1.0f});
        
        // Draw a red rectangle
        GetRenderer()->drawRect({100, 100, 200, 150}, {1, 0, 0, 1});
        
        // Draw a rounded blue rectangle
        GetRenderer()->drawRoundRect({350, 100, 200, 150}, 20.0f, {0, 0.5f, 1, 1});
    });
    
    // Handle keyboard input
    renderer.getInput()->setKeyCallback([](const KeyEvent& e) {
        if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
            GetRenderer()->stop();
        }
    });
    
    // Run main loop
    renderer.run();
    
    return 0;
}
```

### 2.2 Compile Command

```batch
cl /std:c++17 /EHsc main.cpp /I"NovaRenderer/include" NovaCore.lib
```

---

## 3. Core Types (Types.h)

### 3.1 Basic Types

```cpp
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using f32 = float;
using f64 = double;
```

### 3.2 Vector Types

```cpp
struct Vec2 { f32 x, y; };
struct Vec3 { f32 x, y, z; };
struct Vec4 { f32 x, y, z, w; };  // Also r, g, b, a for colors
struct Vec2i { i32 x, y; };
struct Vec3i { i32 x, y, z; };
struct Vec4i { i32 x, y, z, w; };
```

**Vec2 Operations:**
```cpp
Vec2 a(1, 2), b(3, 4);

Vec2 sum = a + b;           // (4, 6)
Vec2 diff = a - b;          // (-2, -2)
Vec2 scaled = a * 2.0f;     // (2, 4)
Vec2 divided = a / 2.0f;    // (0.5, 1)
f32 len = a.length();       // sqrt(1 + 4) = 2.236...
Vec2 norm = a.normalized(); // unit vector
f32 dot = a.dot(b);         // 1*3 + 2*4 = 11
Vec2 lerp = Vec2::lerp(a, b, 0.5f); // (2, 3)
```

### 3.3 Matrix Types

```cpp
struct Mat3 { f32 m[9]; };   // 3x3 matrix for 2D transforms
struct Mat4 { f32 m[16]; };  // 4x4 matrix for 3D transforms
```

**Mat4 Factory Methods:**
```cpp
Mat4 identity = Mat4::identity();

// Orthographic projection (for 2D UI)
Mat4 ortho = Mat4::ortho(0, 800, 600, 0, -1, 1);

// Perspective projection (for 3D)
Mat4 persp = Mat4::perspective(3.14159f/4, 800/600.0f, 0.1f, 100.0f);

// Look-at view matrix
Mat4 view = Mat4::lookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
```

### 3.4 Rectangle

```cpp
struct Rect {
    f32 x, y, width, height;
    
    bool contains(const Vec2& p) const;
    bool intersects(const Rect& o) const;
    Rect intersect(const Rect& o) const;
    Vec2 center() const;
    Vec2 size() const;
};
```

### 3.5 Color

```cpp
// Constructors
Color c1(1.0f, 0.5f, 0.0f, 1.0f);  // RGBA (orange)
Color c2 = Color::fromHex(0xFF8000FF);  // From hex
Color c3 = Color::fromRGBA8(255, 128, 0, 255);

// Preset colors
Color white = Color::white();
Color black = Color::black();
Color red = Color::red();
Color green = Color::green();
Color blue = Color::blue();
Color transparent = Color::transparent();

// Operations
Color halfAlpha = c1.withAlpha(0.5f);
Color darker = c1 * 0.5f;  // Darken by 50%
Color blended = Color::lerp(c1, c2, 0.5f);  // Interpolate
```

### 3.6 Resource Handles

```cpp
// All resources use type-safe handles
BufferHandle buffer;
TextureHandle texture;
SamplerHandle sampler;
ShaderHandle shader;
PipelineHandle pipeline;
RenderPassHandle renderPass;
FramebufferHandle framebuffer;
FontHandle font;

// Check validity
if (handle.valid()) { ... }
if (handle) { ... }  // Implicit bool conversion
```

---

## 4. Window System (Window.h)

### 4.1 Window Configuration

```cpp
struct WindowConfig {
    std::string title = "Nova Window";
    u32 width = 800;
    u32 height = 600;
    bool fullscreen = false;
    bool resizable = true;
    bool decorated = true;   // Has window border
    bool transparent = false;
    f32 opacity = 1.0f;
    i32 x = -1;  // -1 = center
    i32 y = -1;
};
```

### 4.2 Window Class

```cpp
class Window {
public:
    bool create(const WindowConfig& config);
    void destroy();
    bool isValid() const;
    
    // Event processing
    bool pollEvents();
    void waitEvents();
    void waitEventsTimeout(f64 timeout);
    
    // Getters
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
    
    // Setters
    void setSize(u32 width, u32 height);
    void setPosition(i32 x, i32 y);
    void setTitle(const std::string& title);
    void setFullscreen(bool fullscreen);
    void setOpacity(f32 opacity);
    void setVisible(bool visible);
    void setAlwaysOnTop(bool top);
    
    // Events
    void setEventCallback(EventCallback callback);
    void* userData;  // User data pointer
    
    // Platform-specific
#ifdef _WIN32
    HWND getHWND() const;
#endif
};
```

### 4.3 Window Events

```cpp
enum class WindowEvent {
    Create, Close, Resize, Move,
    Focus, Blur, Minimize, Maximize,
    Restore, DPIChange
};

struct WindowEventData {
    WindowEvent type;
    u32 width, height;
    i32 x, y;
    f32 dpi;
    bool focused;
};
```

---

## 5. Input System (Input.h)

### 5.1 Keyboard

```cpp
// Key codes
enum class KeyCode : u32 {
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Space, Escape, Enter, Tab, Backspace,
    Left, Up, Right, Down,
    Shift, Control, Alt, Super,
    // ... more
};

// Input actions
enum class InputAction {
    Press,    // Just pressed
    Release,  // Just released
    Repeat,   // Key repeat (held down)
    Hold      // Currently held
};

// Check keyboard state
InputManager* input = renderer.getInput();
if (input->isKeyDown(KeyCode::Space)) { }
if (input->isKeyPressed(KeyCode::A)) { }   // This frame
if (input->isKeyReleased(KeyCode::A)) { }  // This frame
if (input->isKeyHeld(KeyCode::A)) { }      // Continuous
```

### 5.2 Mouse

```cpp
enum class MouseButton { None, Left, Right, Middle, X1, X2 };

// Mouse state
Vec2 pos = input->getMousePosition();
Vec2 delta = input->getMouseDelta();
Vec2 scroll = input->getMouseScroll();

if (input->isMouseButtonDown(MouseButton::Left)) { }
if (input->isMouseButtonPressed(MouseButton::Left)) { }
if (input->isMouseDragging()) { }

// Mouse settings
input->setMouseVisible(false);  // Hide cursor
input->setMouseGrabbed(true);   // Lock to window
```

### 5.3 Callbacks

```cpp
// Keyboard callback
input->setKeyCallback([](const KeyEvent& e) {
    if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
        GetRenderer()->stop();
    }
});

// Mouse button callback
input->setMouseCallback([](const MouseEvent& e) {
    if (e.button == MouseButton::Left && e.action == InputAction::Press) {
        printf("Clicked at (%.0f, %.0f)\n", e.position.x, e.position.y);
    }
});

// Mouse move callback
input->setMouseMoveCallback([](const MouseMoveEvent& e) {
    printf("Mouse moved to (%.0f, %.0f)\n", e.position.x, e.position.y);
});
```

---

## 6. Renderer API (Renderer.h)

### 6.1 Configuration

```cpp
struct RendererConfig {
    // Window
    WindowConfig window;
    
    // Vulkan
    bool enableValidation = true;   // Debug layer
    bool enableDebugUtils = true;
    bool preferDiscreteGPU = true;
    bool vSync = true;
    u32 maxFramesInFlight = 2;
    
    // Rendering
    bool enableMSAA = false;
    u32 msaaSamples = 4;
    bool enableHDR = false;
    
    // Performance
    bool enableMultiThreading = true;
    u32 workerThreadCount = 0;      // 0 = auto
    u32 maxDrawCallsPerFrame = 10000;
    u32 maxTextureSize = 4096;
};
```

### 6.2 Lifecycle

```cpp
class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // Initialization
    Result<void> initialize(const RendererConfig& config);
    void shutdown();
    bool isInitialized() const;
    
    // Frame control
    bool beginFrame();
    void endFrame();
    void present();
    
    // Main loop
    void run();   // Blocking main loop
    void stop();  // Exit main loop
    bool isRunning() const;
    
    // Accessors
    Window* getWindow();
    InputManager* getInput();
    VulkanBackend* getBackend();
    const FrameContext& getFrameContext() const;
    const RenderStats& getStats() const;
    
    // Callbacks
    void setRenderCallback(RenderCallback callback);
    void setResizeCallback(ResizeCallback callback);
};
```

### 6.3 Drawing Functions

```cpp
// Clear screen
void clear(const Color& color);

// Viewport and scissor
void setViewport(f32 x, f32 y, f32 width, f32 height);
void setScissor(i32 x, i32 y, u32 width, u32 height);

// Basic shapes
void drawRect(const Rect& rect, const Color& color);
void drawRect(const Vec2& pos, const Vec2& size, const Color& color);
void drawRoundRect(const Rect& rect, f32 radius, const Color& color);
void drawLine(const Vec2& start, const Vec2& end, f32 thickness, const Color& color);
void drawCircle(const Vec2& center, f32 radius, const Color& color, u32 segments = 32);

// Textures
void drawTexture(TextureHandle texture, const Rect& dest, const Color& tint = Color::white());
void drawTextureRect(TextureHandle texture, const Rect& src, const Rect& dest, const Color& tint);

// Text (MSDF)
void drawText(const char* text, const Vec2& position, const Color& color, f32 size = 16.0f);
void drawText(FontHandle font, const char* text, const Vec2& position, const Color& color, f32 size);
```

### 6.4 Frame Context

```cpp
struct FrameContext {
    u32 frameIndex;      // Current frame number
    u32 imageIndex;      // Swap chain image index
    f32 deltaTime;       // Time since last frame (seconds)
    f64 totalTime;       // Total running time
    f32 screenWidth;     // Window width
    f32 screenHeight;    // Window height
    Mat4 projection;     // Current projection matrix
    Mat4 view;           // Current view matrix
};
```

### 6.5 Render Statistics

```cpp
struct RenderStats {
    u32 drawCalls;
    u32 vertexCount;
    u32 triangleCount;
    u32 textureBindings;
    u32 pipelineBindings;
    
    f64 frameTime;  // milliseconds
    f64 cpuTime;
    f64 gpuTime;
    
    u64 memoryUsed;
    u64 memoryBudget;
};
```

---

## 7. Vulkan Backend (Advanced)

### 7.1 Resource Creation

```cpp
VulkanBackend* backend = renderer.getBackend();

// Buffer
BufferDesc bufferDesc;
bufferDesc.size = 1024;
bufferDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
bufferDesc.mapped = true;
BufferHandle buffer = backend->createBuffer(bufferDesc);

// Texture
TextureDesc texDesc;
texDesc.width = 512;
texDesc.height = 512;
texDesc.format = Format::RGBA8_UNORM;
TextureHandle texture = backend->createTexture(texDesc);

// Shader (GLSL source)
ShaderDesc shaderDesc;
shaderDesc.vertexSource = vertexGLSL;
shaderDesc.fragmentSource = fragmentGLSL;
ShaderHandle shader = backend->createShader(shaderDesc);

// Pipeline
PipelineDesc pipelineDesc;
pipelineDesc.shader = shader;
pipelineDesc.blendMode = 1;  // Alpha blending
pipelineDesc.depthTest = false;
pipelineDesc.bindings.push_back({0, sizeof(Vertex), false});
pipelineDesc.attributes.push_back({0, VK_FORMAT_R32G32_SFLOAT, 0});
PipelineHandle pipeline = backend->createPipeline(pipelineDesc);
```

### 7.2 Rendering Commands

```cpp
// Begin frame
auto result = backend->beginFrame();

// Render pass
backend->beginRenderPass({}, {}, {0.1f, 0.1f, 0.15f, 1.0f});

// Set state
backend->setPipeline(pipeline);
backend->setVertexBuffer(vertexBuffer, 0);
backend->setIndexBuffer(indexBuffer);
backend->setViewport(0, 0, width, height);
backend->setScissor(0, 0, width, height);

// Push constants (for shader uniforms)
struct PushConstants {
    Mat4 transform;
    Vec4 color;
} pc;
backend->pushConstants(shader, 0, sizeof(pc), &pc);

// Draw
backend->drawIndexed(6);
backend->draw(vertexCount);

// End render pass
backend->endRenderPass();

// Present
backend->endFrame();
backend->present();
```

### 7.3 Resource Cleanup

```cpp
backend->destroyBuffer(buffer);
backend->destroyTexture(texture);
backend->destroySampler(sampler);
backend->destroyShader(shader);
backend->destroyPipeline(pipeline);
```

---

## 8. Complete Examples

### 8.1 Animated Glass Window

```cpp
#include <Nova/Renderer.h>
#include <Nova/Window.h>
#include <Nova/Input.h>
#include <cmath>

using namespace Nova;

struct AppState {
    float time = 0.0f;
    float mouseX = 0.5f;
    float mouseY = 0.5f;
};

Color hslToRgb(float h, float s, float l) {
    auto hue2rgb = [](float p, float q, float t) -> float {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f/6.0f) return p + (q - p) * 6 * t;
        if (t < 1.0f/3.0f) return q;
        if (t < 0.5f) return p + (q - p) * (0.5f - t) * 6;
        return p;
    };
    
    if (s == 0) return {l, l, l, 1};
    
    float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    float p = 2 * l - q;
    
    return {
        hue2rgb(p, q, h + 1.0f/3.0f),
        hue2rgb(p, q, h),
        hue2rgb(p, q, h - 1.0f/3.0f),
        1.0f
    };
}

void onRender(const FrameContext& ctx) {
    auto* state = (AppState*)GetRenderer()->getWindow()->userData;
    state->time += ctx.deltaTime;
    
    float t = state->time;
    float w = ctx.screenWidth;
    float h = ctx.screenHeight;
    
    // Background
    GetRenderer()->clear({0.01f, 0.01f, 0.02f, 1.0f});
    
    // Glass panel
    float glassX = w * 0.05f;
    float glassY = h * 0.05f;
    float glassW = w * 0.9f;
    float glassH = h * 0.9f;
    float radius = 25.0f;
    
    // Multiple layers
    for (int layer = 3; layer >= 0; layer--) {
        float offset = layer * 3.0f;
        float alpha = 0.12f + layer * 0.04f;
        
        Color glassColor = hslToRgb(
            fmod(t * 0.03f + layer * 0.05f, 1.0f),
            0.25f,
            0.12f
        );
        glassColor.a = alpha;
        
        GetRenderer()->drawRoundRect(
            {glassX - offset, glassY - offset, 
             glassW + offset*2, glassH + offset*2},
            radius + offset,
            glassColor
        );
    }
    
    // Light spot
    float spotX = glassX + glassW * state->mouseX;
    float spotY = glassY + glassH * 0.35f + sinf(t * 1.5f) * 25;
    float spotR = 70 + sinf(t * 3.0f) * 15;
    
    GetRenderer()->drawCircle({spotX, spotY}, spotR, {1, 1, 1, 0.06f});
    
    // Edge glow
    float edgeGlow = 0.4f + 0.6f * sinf(t * 2.5f);
    Color edgeColor = {0.35f, 0.55f, 0.95f, edgeGlow * 0.25f};
    
    GetRenderer()->drawRect({glassX, glassY, glassW, 2}, edgeColor);
    GetRenderer()->drawRect({glassX, glassY + glassH - 2, glassW, 2}, edgeColor);
    
    // FPS
    char fps[64];
    snprintf(fps, sizeof(fps), "FPS: %.0f", 1.0f / ctx.deltaTime);
    GetRenderer()->drawText(fps, {glassX + 15, glassY + glassH - 30}, {1, 1, 1, 0.6f}, 12);
}

int main() {
    RendererConfig config;
    config.window.title = "Liquid Glass";
    config.window.width = 500;
    config.window.height = 350;
    config.window.decorated = false;
    config.enableValidation = false;
    config.vSync = true;
    
    Renderer renderer;
    renderer.initialize(config);
    
    AppState state;
    renderer.getWindow()->userData = &state;
    renderer.setRenderCallback(onRender);
    
    renderer.getInput()->setMouseMoveCallback([&](const MouseMoveEvent& e) {
        state.mouseX = e.position.x / renderer.getWindow()->getWidth();
        state.mouseY = e.position.y / renderer.getWindow()->getHeight();
    });
    
    renderer.getInput()->setKeyCallback([](const KeyEvent& e) {
        if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
            GetRenderer()->stop();
        }
    });
    
    renderer.run();
    return 0;
}
```

### 8.2 Interactive UI Demo

```cpp
#include <Nova/Renderer.h>
using namespace Nova;

struct Button {
    Rect rect;
    Color color;
    Color hoverColor;
    bool hovered = false;
    std::string label;
};

void onRender(const FrameContext& ctx) {
    static std::vector<Button> buttons = {
        {{50, 50, 150, 40}, {0.2f, 0.4f, 0.8f, 1}, {0.3f, 0.5f, 0.9f, 1}, false, "Button 1"},
        {{220, 50, 150, 40}, {0.8f, 0.2f, 0.4f, 1}, {0.9f, 0.3f, 0.5f, 1}, false, "Button 2"},
        {{50, 110, 150, 40}, {0.2f, 0.8f, 0.4f, 1}, {0.3f, 0.9f, 0.5f, 1}, false, "Button 3"},
    };
    
    auto* input = GetRenderer()->getInput();
    Vec2 mousePos = input->getMousePosition();
    
    GetRenderer()->clear({0.12f, 0.12f, 0.14f, 1});
    
    for (auto& btn : buttons) {
        btn.hovered = btn.rect.contains(mousePos);
        Color c = btn.hovered ? btn.hoverColor : btn.color;
        
        GetRenderer()->drawRoundRect(btn.rect, 8.0f, c);
        GetRenderer()->drawText(btn.label.c_str(), 
            {btn.rect.x + 10, btn.rect.y + 12}, 
            {1, 1, 1, 1}, 16);
    }
}

int main() {
    RendererConfig config;
    config.window.title = "UI Demo";
    config.window.width = 600;
    config.window.height = 400;
    
    Renderer renderer;
    renderer.initialize(config);
    renderer.setRenderCallback(onRender);
    
    renderer.getInput()->setKeyCallback([](const KeyEvent& e) {
        if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
            GetRenderer()->stop();
        }
    });
    
    renderer.run();
    return 0;
}
```

---

## 9. Error Handling

```cpp
// Result type for operations that can fail
template<typename T>
class Result {
public:
    bool success;
    T value;
    std::string error;
    
    static Result ok(T v);
    static Result err(const std::string& e);
    
    explicit operator bool() const;
};

// Usage
Result<void> result = renderer.initialize(config);
if (!result.success) {
    printf("Error: %s\n", result.error.c_str());
    return 1;
}

// Or with explicit check
auto result = renderer.initialize(config);
if (!result) {
    // Handle error
}
```

---

## 10. Performance Tips

1. **Use VSync wisely**: Disable for benchmarks, enable for smooth presentation
2. **Batch draw calls**: Group similar objects together
3. **Avoid state changes**: Minimize pipeline/texture switches
4. **Use GPU resources**: Create buffers/textures once, update as needed
5. **Profile regularly**: Use `RenderStats` to monitor performance

```cpp
// Check stats every second
static float accumTime = 0;
accumTime += ctx.deltaTime;

if (accumTime >= 1.0f) {
    auto& stats = GetRenderer()->getStats();
    printf("Draw calls: %u, Triangles: %u, Frame: %.2fms\n",
           stats.drawCalls, stats.triangleCount, stats.frameTime);
    accumTime = 0;
}
```

---

## 11. Build Requirements

- **Compiler**: MSVC 2022 (C++17)
- **Vulkan SDK**: 1.2+
- **Platform**: Windows 10/11
- **Libraries**: Vulkan, VMA, glslang

### CMakeLists.txt Example

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyApp)

set(CMAKE_CXX_STANDARD 17)

add_executable(MyApp main.cpp)

target_include_directories(MyApp PRIVATE 
    ${CMAKE_SOURCE_DIR}/NovaRenderer/include
    ${VULKAN_SDK}/Include
)

target_link_libraries(MyApp PRIVATE 
    ${CMAKE_SOURCE_DIR}/NovaRenderer/build/NovaCore.lib
    ${VULKAN_SDK}/Lib/vulkan-1.lib
)
```

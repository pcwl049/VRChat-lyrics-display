# Nova Renderer 详细教程

## 目录

1. [环境搭建](#1-环境搭建)
2. [第一个窗口](#2-第一个窗口)
3. [渲染基础图形](#3-渲染基础图形)
4. [动画和交互](#4-动画和交互)
5. [自定义着色器](#5-自定义着色器)
6. [性能优化](#6-性能优化)
7. [常见问题](#7-常见问题)

---

## 1. 环境搭建

### 1.1 系统要求

- Windows 10/11 (64-bit)
- Visual Studio 2022
- Vulkan SDK 1.2 或更高版本

### 1.2 安装 Vulkan SDK

1. 从 https://vulkan.lunarg.com/ 下载 Vulkan SDK
2. 安装到默认路径 (如 `E:\Vulkan_SDK`)
3. 设置环境变量 `VULKAN_SDK`

### 1.3 编译 NovaCore

```batch
cd NovaRenderer\build
cmake .. -G Ninja
ninja
```

生成的文件:
- `NovaCore.lib` - 静态库 (~61MB)
- `NovaExample.exe` - 示例程序

---

## 2. 第一个窗口

### 2.1 最小示例

```cpp
#include <Nova/Renderer.h>
using namespace Nova;

int main() {
    RendererConfig config;
    config.window.title = "Hello Nova";
    config.window.width = 800;
    config.window.height = 600;
    
    Renderer renderer;
    auto result = renderer.initialize(config);
    
    if (!result.success) {
        printf("Failed: %s\n", result.error.c_str());
        return 1;
    }
    
    renderer.setRenderCallback([](const FrameContext& ctx) {
        GetRenderer()->clear({0.1f, 0.1f, 0.2f, 1.0f});
    });
    
    renderer.run();
    return 0;
}
```

### 2.2 编译运行

创建 `compile.bat`:

```batch
@echo off
call "D:\Program Files\VScode\VC\Auxiliary\Build\vcvarsall.bat" x64

cl /std:c++17 /EHsc /MD main.cpp ^
   /I"NovaRenderer\include" ^
   /I"E:\Vulkan_SDK\Include" ^
   /link NovaRenderer\build\NovaCore.lib ^
        E:\Vulkan_SDK\Lib\vulkan-1.lib

main.exe
```

### 2.3 窗口配置详解

```cpp
WindowConfig windowConfig;
windowConfig.title = "My Window";     // 窗口标题
windowConfig.width = 1024;             // 宽度
windowConfig.height = 768;             // 高度
windowConfig.fullscreen = false;       // 全屏模式
windowConfig.resizable = true;         // 可调整大小
windowConfig.decorated = true;         // 显示边框
windowConfig.transparent = false;      // 透明窗口
windowConfig.opacity = 1.0f;           // 不透明度 (0-1)
windowConfig.x = -1;                   // X 位置 (-1 = 居中)
windowConfig.y = -1;                   // Y 位置
```

### 2.4 渲染器配置详解

```cpp
RendererConfig config;

// Vulkan 配置
config.enableValidation = true;    // 启用验证层 (调试用)
config.enableDebugUtils = true;    // 启用调试工具
config.preferDiscreteGPU = true;   // 优先使用独立显卡
config.vSync = true;               // 垂直同步

// 性能配置
config.maxFramesInFlight = 2;      // 帧缓冲数
config.enableMSAA = false;         // 多重采样抗锯齿
config.msaaSamples = 4;            // MSAA 采样数
```

---

## 3. 渲染基础图形

### 3.1 坐标系统

Nova 使用屏幕坐标:
- 原点 (0, 0) 在左上角
- X 向右递增
- Y 向下递增

```
(0,0) --------> X
  |
  |
  v
  Y
```

### 3.2 绘制矩形

```cpp
void onRender(const FrameContext& ctx) {
    // 清屏
    GetRenderer()->clear({0.05f, 0.05f, 0.1f, 1.0f});
    
    // 简单矩形
    GetRenderer()->drawRect(
        {100, 100, 200, 150},  // x, y, width, height
        {1.0f, 0.0f, 0.0f, 1.0f}  // RGBA 红色
    );
    
    // 使用 Vec2
    GetRenderer()->drawRect(
        Vec2(350, 100),   // position
        Vec2(200, 150),   // size
        Color::blue()     // color
    );
}
```

### 3.3 绘制圆角矩形

```cpp
// 圆角矩形 (使用 SDF 着色器)
GetRenderer()->drawRoundRect(
    {100, 100, 200, 150},  // rect
    20.0f,                  // 圆角半径
    {0.2f, 0.5f, 0.8f, 1.0f}  // color
);
```

### 3.4 绘制圆形

```cpp
// 圆形 (segments 控制精度)
GetRenderer()->drawCircle(
    {400, 300},   // center
    50.0f,        // radius
    {1.0f, 1.0f, 0.0f, 1.0f},  // yellow
    32            // segments (更多 = 更圆滑)
);
```

### 3.5 绘制线条

```cpp
GetRenderer()->drawLine(
    {100, 100},   // start
    {300, 200},   // end
    2.0f,         // thickness
    {0.0f, 1.0f, 0.0f, 1.0f}  // green
);
```

### 3.6 绘制文本

```cpp
// 默认字体
GetRenderer()->drawText(
    "Hello World",
    {100, 100},   // position
    {1, 1, 1, 1}, // color
    24.0f         // size
);

// 格式化文本
char buf[128];
snprintf(buf, sizeof(buf), "Score: %d", score);
GetRenderer()->drawText(buf, {10, 10}, Color::white(), 16);
```

---

## 4. 动画和交互

### 4.1 使用 FrameContext

```cpp
struct AppState {
    float time = 0.0f;
    float x = 100.0f;
};

void onRender(const FrameContext& ctx) {
    auto* state = (AppState*)GetRenderer()->getWindow()->userData;
    
    // 更新时间
    state->time += ctx.deltaTime;
    
    // 动画位置
    state->x = 100 + sinf(state->time * 2.0f) * 50;
    
    // 绘制
    GetRenderer()->clear({0.1f, 0.1f, 0.15f, 1.0f});
    GetRenderer()->drawRect({state->x, 200, 100, 100}, Color::red());
}

int main() {
    // ...
    AppState state;
    renderer.getWindow()->userData = &state;
    renderer.setRenderCallback(onRender);
    // ...
}
```

### 4.2 键盘输入

```cpp
renderer.getInput()->setKeyCallback([](const KeyEvent& e) {
    // 按下 ESC 退出
    if (e.key == KeyCode::Escape && e.action == InputAction::Press) {
        GetRenderer()->stop();
    }
    
    // 按下空格
    if (e.key == KeyCode::Space && e.action == InputAction::Press) {
        printf("Space pressed!\n");
    }
    
    // 组合键 (Ctrl+S)
    if (e.key == KeyCode::S && e.ctrl && e.action == InputAction::Press) {
        printf("Save!\n");
    }
});

// 或者轮询检查
void onRender(const FrameContext& ctx) {
    auto* input = GetRenderer()->getInput();
    
    if (input->isKeyDown(KeyCode::W)) {
        // W 键被按下
    }
    
    if (input->isKeyPressed(KeyCode::Space)) {
        // 这一帧刚按下
    }
    
    if (input->isKeyHeld(KeyCode::Shift)) {
        // 持续按住
    }
}
```

### 4.3 鼠标输入

```cpp
// 鼠标移动
renderer.getInput()->setMouseMoveCallback([](const MouseMoveEvent& e) {
    printf("Mouse: %.0f, %.0f\n", e.position.x, e.position.y);
    
    if (e.dragged) {
        printf("Dragging with button %d\n", (int)e.dragButton);
    }
});

// 鼠标点击
renderer.getInput()->setMouseCallback([](const MouseEvent& e) {
    if (e.button == MouseButton::Left && e.action == InputAction::Press) {
        printf("Left click at %.0f, %.0f\n", e.position.x, e.position.y);
    }
    
    if (e.button == MouseButton::Right && e.action == InputAction::Press) {
        printf("Right click\n");
    }
    
    // 滚轮
    if (e.wheel != 0) {
        printf("Scroll: %d\n", e.wheel);
    }
});

// 轮询鼠标状态
void onRender(const FrameContext& ctx) {
    auto* input = GetRenderer()->getInput();
    
    Vec2 mousePos = input->getMousePosition();
    Vec2 mouseDelta = input->getMouseDelta();
    
    if (input->isMouseButtonDown(MouseButton::Left)) {
        // 左键按下
    }
    
    if (input->isMouseDragging()) {
        // 正在拖动
    }
}
```

### 4.4 窗口事件

```cpp
renderer.getWindow()->setEventCallback([](const WindowEventData& e) {
    switch (e.type) {
        case WindowEvent::Resize:
            printf("Resized to %ux%u\n", e.width, e.height);
            break;
            
        case WindowEvent::Focus:
            printf("Window focused\n");
            break;
            
        case WindowEvent::Blur:
            printf("Window lost focus\n");
            break;
            
        case WindowEvent::Close:
            printf("Window closing\n");
            break;
            
        case WindowEvent::DPIChange:
            printf("DPI changed to %.0f\n", e.dpi);
            break;
    }
});
```

---

## 5. 自定义着色器

### 5.1 着色器基础

Nova 使用 GLSL 着色器，通过 glslang 编译为 SPIR-V。

**顶点着色器示例:**

```glsl
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
}
```

**片段着色器示例:**

```glsl
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
```

### 5.2 创建自定义着色器

```cpp
const char* myVertexShader = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 color;
    float time;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main() {
    gl_Position = pc.mvp * vec4(inPos, 0.0, 1.0);
    vUV = inUV;
    vColor = pc.color;
}
)";

const char* myFragmentShader = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 mvp;
    vec4 color;
    float time;
} pc;

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    // 波纹效果
    float d = length(vUV - 0.5);
    float wave = sin(d * 20.0 - pc.time * 3.0) * 0.5 + 0.5;
    
    outColor = vec4(vColor.rgb * wave, vColor.a);
}
)";

// 创建着色器
ShaderDesc shaderDesc;
shaderDesc.vertexSource = myVertexShader;
shaderDesc.fragmentSource = myFragmentShader;
shaderDesc.debugName = "MyShader";

VulkanBackend* backend = renderer.getBackend();
ShaderHandle shader = backend->createShader(shaderDesc);
```

### 5.3 创建管线

```cpp
// 定义顶点格式
struct MyVertex {
    float x, y;    // position
    float u, v;    // texcoord
};

// 创建管线
PipelineDesc pipelineDesc;
pipelineDesc.shader = shader;
pipelineDesc.blendMode = 1;  // Alpha blending
pipelineDesc.depthTest = false;
pipelineDesc.depthWrite = false;

// 顶点绑定
pipelineDesc.bindings.push_back({0, sizeof(MyVertex), false});

// 顶点属性
pipelineDesc.attributes.push_back({0, VK_FORMAT_R32G32_SFLOAT, 0});      // position
pipelineDesc.attributes.push_back({1, VK_FORMAT_R32G32_SFLOAT, 8});      // texcoord

PipelineHandle pipeline = backend->createPipeline(pipelineDesc);
```

### 5.4 使用自定义管线

```cpp
// 创建顶点缓冲
MyVertex vertices[] = {
    {0, 0, 0, 0},
    {1, 0, 1, 0},
    {1, 1, 1, 1},
    {0, 1, 0, 1}
};

BufferDesc vbDesc;
vbDesc.size = sizeof(vertices);
vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
vbDesc.mapped = true;
BufferHandle vertexBuffer = backend->createBuffer(vbDesc);
backend->updateBuffer(vertexBuffer, vertices, sizeof(vertices));

// 创建索引缓冲
uint16_t indices[] = {0, 1, 2, 0, 2, 3};

BufferDesc ibDesc;
ibDesc.size = sizeof(indices);
ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
ibDesc.mapped = true;
BufferHandle indexBuffer = backend->createBuffer(ibDesc);
backend->updateBuffer(indexBuffer, indices, sizeof(indices));

// 渲染时使用
void onRender(const FrameContext& ctx) {
    VulkanBackend* backend = GetRenderer()->getBackend();
    
    // Push constants 结构必须与着色器一致
    struct PushConstants {
        float mvp[16];
        float color[4];
        float time;
    } pc;
    
    // 填充数据
    Mat4 mvp = ctx.projection;  // 或自定义矩阵
    memcpy(pc.mvp, mvp.m, sizeof(pc.mvp));
    pc.color[0] = 1; pc.color[1] = 0; pc.color[2] = 0; pc.color[3] = 1;
    pc.time = totalTime;
    
    // 设置管线和缓冲
    backend->setPipeline(pipeline);
    backend->setVertexBuffer(vertexBuffer, 0);
    backend->setIndexBuffer(indexBuffer);
    
    // 推送常量
    backend->pushConstants(shader, 0, sizeof(pc), &pc);
    
    // 绘制
    backend->drawIndexed(6);
}
```

---

## 6. 性能优化

### 6.1 减少绘制调用

```cpp
// 不好: 每帧都创建资源
void onRender(const FrameContext& ctx) {
    static TextureHandle tex = GetRenderer()->createTexture(512, 512);
    // ...
}

// 好: 在初始化时创建
TextureHandle g_texture;

void init() {
    g_texture = GetRenderer()->createTexture(512, 512);
}

void onRender(const FrameContext& ctx) {
    GetRenderer()->drawTexture(g_texture, {0, 0, 512, 512});
}
```

### 6.2 批处理

```cpp
// 不好: 多次绘制调用
for (int i = 0; i < 100; i++) {
    GetRenderer()->drawRect({i * 10, 0, 8, 8}, color);
}

// 好: 合并为较少的绘制
GetRenderer()->drawRect({0, 0, 1000, 8}, color);
```

### 6.3 使用 VSync

```cpp
// 开发时关闭 VSync 测试真实性能
config.vSync = false;

// 发布时开启 VSync 避免撕裂
config.vSync = true;
```

### 6.4 监控性能

```cpp
void onRender(const FrameContext& ctx) {
    static float accumTime = 0;
    static int frameCount = 0;
    
    accumTime += ctx.deltaTime;
    frameCount++;
    
    if (accumTime >= 1.0f) {
        float fps = frameCount / accumTime;
        auto& stats = GetRenderer()->getStats();
        
        printf("FPS: %.1f | Draw calls: %u | Triangles: %u | Frame: %.2fms\n",
               fps, stats.drawCalls, stats.triangleCount, stats.frameTime);
        
        accumTime = 0;
        frameCount = 0;
    }
}
```

---

## 7. 常见问题

### Q: 窗口显示纯黑?

检查以下几点:
1. 是否调用了 `clear()`
2. 是否在 `setRenderCallback` 中绘制
3. 着色器编译是否成功 (查看控制台输出)

### Q: 着色器编译失败?

常见原因:
1. GLSL 版本不对 (使用 `#version 450`)
2. Push constants 结构不匹配
3. location 值超出范围
4. 语法错误

```cpp
// 查看编译输出
// 成功: "Shader compiled successfully, SPIR-V size: xxx words"
// 失败: "Shader parse error: ..."
```

### Q: 内存占用高?

NovaCore.lib 是静态库，会链接到可执行文件中。内存占用高通常因为:
1. Vulkan 驱动开销
2. 内置着色器和缓冲区
3. 交换链图像

### Q: 如何调试?

1. 启用验证层: `config.enableValidation = true`
2. 使用 RenderDoc 或 NVIDIA Nsight
3. 检查控制台输出

### Q: 如何处理窗口大小改变?

```cpp
renderer.getWindow()->setEventCallback([](const WindowEventData& e) {
    if (e.type == WindowEvent::Resize) {
        // 交换链会自动重建
        // 更新你的投影矩阵等
    }
});
```

---

## 附录: 完整示例项目结构

```
MyApp/
├── main.cpp
├── compile.bat
├── NovaRenderer/
│   ├── include/
│   │   └── Nova/
│   │       ├── Types.h
│   │       ├── Window.h
│   │       ├── Input.h
│   │       ├── Renderer.h
│   │       └── VulkanBackend.h
│   └── build/
│       └── NovaCore.lib
└── build/
    └── MyApp.exe
```

### compile.bat

```batch
@echo off
set VSCMD_START_DIR=%CD%
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

set INCLUDE_PATHS=/I"NovaRenderer\include" /I"%VULKAN_SDK%\Include"
set LIB_PATHS=/LIBPATH:"NovaRenderer\build" /LIBPATH:"%VULKAN_SDK%\Lib"
set LIBS=NovaCore.lib vulkan-1.lib

cl /std:c++17 /EHsc /MD main.cpp %INCLUDE_PATHS% /link %LIB_PATHS% %LIBS%

if %ERRORLEVEL% EQU 0 (
    echo Build successful!
    main.exe
) else (
    echo Build failed!
)
```

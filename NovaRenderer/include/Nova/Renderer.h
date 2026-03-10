/**
 * Nova Renderer - Main Renderer
 * 主渲染器接口
 */

#pragma once

#include "Types.h"
#include "Window.h"
#include "Input.h"
#include "VulkanBackend.h"
#include <functional>
#include <memory>

namespace Nova {

// 渲染器配置
struct RendererConfig {
    // 窗口配置
    WindowConfig window;
    
    // Vulkan 配置
    bool enableValidation = true;
    bool enableDebugUtils = true;
    bool preferDiscreteGPU = true;
    bool vSync = true;
    u32 maxFramesInFlight = 2;
    
    // 渲染配置
    bool enableMSAA = false;
    u32 msaaSamples = 4;
    bool enableHDR = false;
    
    // 性能配置
    bool enableMultiThreading = true;
    u32 workerThreadCount = 0;  // 0 = 自动检测
    u32 maxDrawCallsPerFrame = 10000;
    u32 maxTextureSize = 4096;
};

// 渲染统计
struct RenderStats {
    u32 drawCalls = 0;
    u32 vertexCount = 0;
    u32 triangleCount = 0;
    u32 textureBindings = 0;
    u32 pipelineBindings = 0;
    u32 descriptorSetBindings = 0;
    
    f64 frameTime = 0;
    f64 cpuTime = 0;
    f64 gpuTime = 0;
    
    u64 memoryUsed = 0;
    u64 memoryBudget = 0;
    
    void reset() {
        drawCalls = 0;
        vertexCount = 0;
        triangleCount = 0;
        textureBindings = 0;
        pipelineBindings = 0;
        descriptorSetBindings = 0;
    }
};

// 帧上下文
struct FrameContext {
    u32 frameIndex;
    u32 imageIndex;
    f32 deltaTime;
    f64 totalTime;
    f32 screenWidth;
    f32 screenHeight;
    Mat4 projection;
    Mat4 view;
};

// 渲染器
class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // 生命周期
    Result<void> initialize(const RendererConfig& config);
    void shutdown();
    bool isInitialized() const;
    
    // 帧循环
    bool beginFrame();
    void endFrame();
    void present();
    
    // 窗口
    Window* getWindow() { return window_.get(); }
    const Window* getWindow() const { return window_.get(); }
    
    // 输入
    InputManager* getInput() { return input_.get(); }
    const InputManager* getInput() const { return input_.get(); }
    
    // Vulkan 后端
    VulkanBackend* getBackend() { return backend_.get(); }
    const VulkanBackend* getBackend() const { return backend_.get(); }
    
    // 帧上下文
    const FrameContext& getFrameContext() const { return frameContext_; }
    
    // 统计
    const RenderStats& getStats() const { return stats_; }
    void resetStats();
    
    // 回调
    using RenderCallback = std::function<void(const FrameContext&)>;
    void setRenderCallback(RenderCallback callback);
    
    using ResizeCallback = std::function<void(u32, u32)>;
    void setResizeCallback(ResizeCallback callback);
    
    // 快捷绘制
    void clear(const Color& color);
    void setViewport(f32 x, f32 y, f32 width, f32 height);
    void setScissor(i32 x, i32 y, u32 width, u32 height);
    
    // 基础形状 (使用内置着色器)
    void drawRect(const Rect& rect, const Color& color);
    void drawRect(const Vec2& pos, const Vec2& size, const Color& color);
    void drawRoundRect(const Rect& rect, f32 radius, const Color& color);
    void drawLine(const Vec2& start, const Vec2& end, f32 thickness, const Color& color);
    void drawCircle(const Vec2& center, f32 radius, const Color& color, u32 segments = 32);
    void drawTexture(TextureHandle texture, const Rect& dest, const Color& tint = Color::white());
    void drawTextureRect(TextureHandle texture, const Rect& src, const Rect& dest, const Color& tint = Color::white());
    
    // 文本绘制 (使用 MSDF)
    void drawText(const char* text, const Vec2& position, const Color& color, f32 size = 16.0f);
    void drawText(FontHandle font, const char* text, const Vec2& position, const Color& color, f32 size = 16.0f);
    
    // 资源创建快捷方式
    TextureHandle createTexture(u32 width, u32 height, const void* data = nullptr);
    TextureHandle createTextureFromFile(const char* path);
    FontHandle createFont(const char* path, f32 size = 32.0f);
    
    // 配置
    const RendererConfig& getConfig() const { return config_; }
    
    // 运行循环
    void run();
    void stop();
    bool isRunning() const { return running_; }
    
private:
    RendererConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<InputManager> input_;
    std::unique_ptr<VulkanBackend> backend_;
    
    FrameContext frameContext_;
    RenderStats stats_;
    
    RenderCallback renderCallback_;
    ResizeCallback resizeCallback_;
    
    bool running_ = false;
    f64 lastFrameTime_ = 0.0;
    f64 totalTime_ = 0.0;
    
    // 内置资源
    PipelineHandle rectPipeline_;
    PipelineHandle roundRectPipeline_;
    PipelineHandle texturePipeline_;
    PipelineHandle textPipeline_;
    ShaderHandle basicShader_;
    ShaderHandle textureShader_;
    ShaderHandle roundRectShader_;
    BufferHandle quadVertexBuffer_;
    BufferHandle quadIndexBuffer_;
    SamplerHandle defaultSampler_;
    
    bool createBuiltinResources();
    void createBuiltinShaders();
    void destroyBuiltinResources();
    void handleWindowEvent(const WindowEventData& event);
};

// 全局渲染器实例
Renderer* GetRenderer();

} // namespace Nova

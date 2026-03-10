/**
 * Nova Renderer - Main Renderer Implementation
 */

#include "Nova/Renderer.h"
#include "Nova/Shader.h"
#include "Nova/Pipeline.h"
#include <chrono>
#include <cstring>

// STB 图像加载
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

namespace Nova {

// 全局实例
static Renderer* g_renderer = nullptr;

Renderer* GetRenderer() {
    return g_renderer;
}

Renderer::Renderer() {
    g_renderer = this;
}

Renderer::~Renderer() {
    shutdown();
    g_renderer = nullptr;
}

Result<void> Renderer::initialize(const RendererConfig& config) {
    config_ = config;
    
    // 创建窗口
    window_ = std::make_unique<Window>();
    if (!window_->create(config.window)) {
        return Result<void>::err("Failed to create window");
    }
    
    // 设置窗口事件回调
    window_->setEventCallback([this](const WindowEventData& event) {
        handleWindowEvent(event);
    });
    
    // 创建输入管理器
    input_ = std::make_unique<InputManager>();
    
    // 创建 Vulkan 后端
    VulkanConfig vkConfig;
    vkConfig.enableValidation = config.enableValidation;
    vkConfig.enableDebugUtils = config.enableDebugUtils;
    vkConfig.preferDiscreteGPU = config.preferDiscreteGPU;
    vkConfig.vSync = config.vSync;
    vkConfig.maxFramesInFlight = config.maxFramesInFlight;
    vkConfig.windowHandle = window_->getHWND();
    vkConfig.applicationName = config.window.title.c_str();
    
    backend_ = std::make_unique<VulkanBackend>();
    auto result = backend_->initialize(vkConfig);
    if (!result.success) {
        return Result<void>::err("Failed to initialize Vulkan: " + result.error);
    }
    
    // 创建内置资源
    if (!createBuiltinResources()) {
        return Result<void>::err("Failed to create builtin resources");
    }
    
    // 初始化帧上下文
    frameContext_.screenWidth = (f32)window_->getWidth();
    frameContext_.screenHeight = (f32)window_->getHeight();
    frameContext_.projection = Mat4::ortho(0, frameContext_.screenWidth, 
                                            frameContext_.screenHeight, 0, -1, 1);
    
    lastFrameTime_ = std::chrono::duration<f64>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    
    return Result<void>::ok();
}

void Renderer::shutdown() {
    if (!backend_) return;
    
    backend_->endFrame();
    backend_->present();
    
    destroyBuiltinResources();
    
    backend_.reset();
    input_.reset();
    window_.reset();
}

bool Renderer::isInitialized() const {
    return backend_ && backend_->isInitialized();
}

bool Renderer::beginFrame() {
    // 处理输入
    input_->beginFrame();
    
    // 处理窗口事件
    if (!window_->pollEvents()) {
        return false;
    }
    
    // 开始 Vulkan 帧
    auto result = backend_->beginFrame();
    if (!result.success) {
        return false;
    }
    
    frameContext_.frameIndex = backend_->getCurrentFrameIndex();
    frameContext_.imageIndex = result.value;
    
    // 计算时间
    f64 currentTime = std::chrono::duration<f64>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    frameContext_.deltaTime = (f32)(currentTime - lastFrameTime_);
    frameContext_.totalTime = currentTime - totalTime_;
    lastFrameTime_ = currentTime;
    
    // 更新统计
    stats_.reset();
    stats_.frameTime = frameContext_.deltaTime * 1000.0;
    
    return true;
}

void Renderer::endFrame() {
    backend_->endFrame();
    input_->endFrame();
}

void Renderer::present() {
    backend_->present();
}

void Renderer::clear(const Color& color) {
    backend_->beginRenderPass({}, {}, {color.r, color.g, color.b, color.a});
    backend_->setViewport(0, 0, frameContext_.screenWidth, frameContext_.screenHeight);
    backend_->setScissor(0, 0, (u32)frameContext_.screenWidth, (u32)frameContext_.screenHeight);
}

void Renderer::setViewport(f32 x, f32 y, f32 width, f32 height) {
    backend_->setViewport(x, y, width, height);
}

void Renderer::setScissor(i32 x, i32 y, u32 width, u32 height) {
    backend_->setScissor(x, y, width, height);
}

// ============================================================================
// 绘制函数实现
// ============================================================================

void Renderer::drawRect(const Rect& rect, const Color& color) {
    if (!rectPipeline_.valid()) return;
    
    // 设置管线
    backend_->setPipeline(rectPipeline_);
    
    // 设置顶点缓冲
    backend_->setVertexBuffer(quadVertexBuffer_, 0);
    backend_->setIndexBuffer(quadIndexBuffer_);
    
    // 推送常量：变换矩阵和颜色
    struct PushConstants {
        f32 transform[16];  // 4x4 矩阵
        f32 color[4];
    } pc;
    
    // 构建变换矩阵：缩放 + 平移
    Mat4 model = Mat4::identity();
    // 缩放
    model.m[0] = rect.width;
    model.m[5] = rect.height;
    // 平移
    model.m[12] = rect.x;
    model.m[13] = rect.y;
    
    // 投影矩阵
    Mat4 mvp = frameContext_.projection * model;
    memcpy(pc.transform, mvp.m, sizeof(pc.transform));
    pc.color[0] = color.r;
    pc.color[1] = color.g;
    pc.color[2] = color.b;
    pc.color[3] = color.a;
    
    // 推送常量
    backend_->pushConstants(basicShader_, 0, sizeof(pc), &pc);
    
    // 绘制
    backend_->drawIndexed(6);
    
    stats_.drawCalls++;
    stats_.triangleCount += 2;
}

void Renderer::drawRect(const Vec2& pos, const Vec2& size, const Color& color) {
    drawRect(Rect{pos.x, pos.y, size.x, size.y}, color);
}

void Renderer::drawRoundRect(const Rect& rect, f32 radius, const Color& color) {
    if (!roundRectPipeline_.valid()) {
        printf("ERROR: roundRectPipeline_ not valid!\n");
        return;
    }
    if (!roundRectShader_.valid()) {
        printf("ERROR: roundRectShader_ not valid!\n");
        return;
    }
    
    backend_->setPipeline(roundRectPipeline_);
    backend_->setVertexBuffer(quadVertexBuffer_, 0);
    backend_->setIndexBuffer(quadIndexBuffer_);
    
    // 必须与着色器中的结构完全匹配
    struct RoundRectPushConstants {
        f32 transform[16];   // mat4 - 64 bytes
        f32 rectParams[4];   // vec4 - 16 bytes
        f32 color[4];        // vec4 - 16 bytes
        f32 radius;          // float - 4 bytes
    } pc;
    
    Mat4 model = Mat4::identity();
    model.m[0] = rect.width;
    model.m[5] = rect.height;
    model.m[12] = rect.x;
    model.m[13] = rect.y;
    
    Mat4 mvp = frameContext_.projection * model;
    memcpy(pc.transform, mvp.m, sizeof(pc.transform));
    
    pc.rectParams[0] = rect.x;
    pc.rectParams[1] = rect.y;
    pc.rectParams[2] = rect.width;
    pc.rectParams[3] = rect.height;
    pc.color[0] = color.r;
    pc.color[1] = color.g;
    pc.color[2] = color.b;
    pc.color[3] = color.a;
    pc.radius = radius;
    
    // Debug: 只打印一次
    static bool debugPrinted = false;
    if (!debugPrinted) {
        printf("drawRoundRect: rect=(%.0f,%.0f,%.0f,%.0f) radius=%.1f color=(%.2f,%.2f,%.2f,%.2f)\n",
               rect.x, rect.y, rect.width, rect.height, radius, color.r, color.g, color.b, color.a);
        printf("PushConstants size: %zu bytes\n", sizeof(pc));
        printf("Projection: [%.3f, %.3f, %.3f, %.3f]\n", 
               frameContext_.projection.m[0], frameContext_.projection.m[5],
               frameContext_.projection.m[12], frameContext_.projection.m[13]);
        printf("MVP[0]=%.3f MVP[5]=%.3f MVP[12]=%.3f MVP[13]=%.3f\n",
               mvp.m[0], mvp.m[5], mvp.m[12], mvp.m[13]);
        debugPrinted = true;
    }
    
    backend_->pushConstants(roundRectShader_, 0, sizeof(pc), &pc);
    
    backend_->drawIndexed(6);
    
    stats_.drawCalls++;
    stats_.triangleCount += 2;
}

void Renderer::drawLine(const Vec2& start, const Vec2& end, f32 thickness, const Color& color) {
    // 构建线段四边形
    Vec2 dir = (end - start).normalized();
    Vec2 perp = Vec2{-dir.y, dir.x} * (thickness * 0.5f);
    
    struct LineVertex {
        f32 x, y;
    };
    
    LineVertex vertices[4] = {
        {start.x + perp.x, start.y + perp.y},
        {start.x - perp.x, start.y - perp.y},
        {end.x - perp.x, end.y - perp.y},
        {end.x + perp.x, end.y + perp.y}
    };
    
    // 更新顶点缓冲并绘制
    // 使用 rectPipeline_ 绘制
    if (rectPipeline_.valid()) {
        backend_->setPipeline(rectPipeline_);
        backend_->draw(4);
        stats_.drawCalls++;
        stats_.triangleCount += 2;
    }
}

void Renderer::drawCircle(const Vec2& center, f32 radius, const Color& color, u32 segments) {
    // 使用三角形扇形绘制圆形
    // 实际实现应该使用圆的 SDF 着色器
    
    if (roundRectPipeline_.valid()) {
        // 临时用圆角矩形代替，实际应该用圆形 SDF
        drawRoundRect(Rect{center.x - radius, center.y - radius, radius * 2, radius * 2}, radius, color);
    }
}

void Renderer::drawTexture(TextureHandle texture, const Rect& dest, const Color& tint) {
    if (!texturePipeline_.valid() || !texture.valid()) return;
    
    backend_->setPipeline(texturePipeline_);
    backend_->setVertexBuffer(quadVertexBuffer_, 0);
    backend_->setIndexBuffer(quadIndexBuffer_);
    
    struct TexturePushConstants {
        f32 transform[16];
        f32 tint[4];
    } pc;
    
    Mat4 model = Mat4::identity();
    model.m[0] = dest.width;
    model.m[5] = dest.height;
    model.m[12] = dest.x;
    model.m[13] = dest.y;
    
    Mat4 mvp = frameContext_.projection * model;
    memcpy(pc.transform, mvp.m, sizeof(pc.transform));
    pc.tint[0] = tint.r;
    pc.tint[1] = tint.g;
    pc.tint[2] = tint.b;
    pc.tint[3] = tint.a;
    
    // TODO: 绑定纹理描述符集
    
    backend_->drawIndexed(6);
    
    stats_.drawCalls++;
    stats_.triangleCount += 2;
    stats_.textureBindings++;
}

void Renderer::drawTextureRect(TextureHandle texture, const Rect& src, const Rect& dest, const Color& tint) {
    if (!texturePipeline_.valid() || !texture.valid()) return;
    
    // 修改顶点 UV 坐标来裁剪纹理区域
    // 实际实现需要动态更新顶点数据或使用实例化数据
    
    drawTexture(texture, dest, tint);
}

void Renderer::drawText(const char* text, const Vec2& position, const Color& color, f32 size) {
    // 使用默认字体绘制文本
    // 实际实现需要 MSDF 字体系统
}

void Renderer::drawText(FontHandle font, const char* text, const Vec2& position, const Color& color, f32 size) {
    if (!textPipeline_.valid() || !font.valid()) return;
    
    // MSDF 文本渲染
    // 1. 遍历文本，获取每个字符的字形
    // 2. 计算每个字符的位置和 UV
    // 3. 批量绘制所有字符
    
    f32 x = position.x;
    f32 y = position.y;
    
    // TODO: 完整的 MSDF 字体渲染实现
}

TextureHandle Renderer::createTexture(u32 width, u32 height, const void* data) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = static_cast<Format>(VK_FORMAT_R8G8B8A8_SRGB);
    desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    
    auto handle = backend_->createTexture(desc);
    
    if (data) {
        backend_->updateTexture(handle, data, width, height);
    }
    
    return handle;
}

TextureHandle Renderer::createTextureFromFile(const char* path) {
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    
    if (!pixels) {
        return {};
    }
    
    auto handle = createTexture(width, height, pixels);
    stbi_image_free(pixels);
    
    return handle;
}

FontHandle Renderer::createFont(const char* path, f32 size) {
    // 创建 MSDF 字体
    // 实际实现需要 FreeType 加载字体并生成 MSDF
    return {};
}

void Renderer::setRenderCallback(RenderCallback callback) {
    renderCallback_ = std::move(callback);
}

void Renderer::setResizeCallback(ResizeCallback callback) {
    resizeCallback_ = std::move(callback);
}

void Renderer::resetStats() {
    stats_.reset();
}

void Renderer::run() {
    running_ = true;
    
    while (running_) {
        if (!beginFrame()) {
            running_ = false;
            break;
        }
        
        if (renderCallback_) {
            renderCallback_(frameContext_);
        }
        
        endFrame();
        present();
    }
}

void Renderer::stop() {
    running_ = false;
}

bool Renderer::createBuiltinResources() {
    // 创建单位四边形顶点缓冲 - 只有 position
    struct Vertex {
        f32 x, y;
    };
    
    Vertex quadVertices[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    
    BufferDesc vbDesc;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbDesc.mapped = true;
    
    quadVertexBuffer_ = backend_->createBuffer(vbDesc);
    backend_->updateBuffer(quadVertexBuffer_, quadVertices, sizeof(quadVertices));
    
    // 创建索引缓冲
    u16 indices[] = {0, 1, 2, 0, 2, 3};
    
    BufferDesc ibDesc;
    ibDesc.size = sizeof(indices);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibDesc.mapped = true;
    
    quadIndexBuffer_ = backend_->createBuffer(ibDesc);
    backend_->updateBuffer(quadIndexBuffer_, indices, sizeof(indices));
    
    // 创建默认采样器
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.mipFilter = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    
    defaultSampler_ = backend_->createSampler(samplerDesc);
    
    // 创建内置着色器
    createBuiltinShaders();
    
    return true;
}

void Renderer::createBuiltinShaders() {
    // 基础顶点着色器
    const char* basicVertexShader = R"(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 color;
} pc;

layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
}
)";
    
    // 圆角矩形顶点着色器 - 匹配片段着色器的 push constants
    const char* roundRectVertexShader = R"(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 rectParams;
    vec4 color;
    float radius;
} pc;

layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
}
)";
    
    // 纯色片段着色器
    const char* solidColorFragment = R"(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 color;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = pc.color;
}
)";
    
    // 纹理片段着色器
    const char* textureFragment = R"(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 color;
} pc;

layout(binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    // 使用 gl_FragCoord 计算纹理坐标
    vec2 texCoord = gl_FragCoord.xy / vec2(800.0, 600.0);
    outColor = texture(texSampler, texCoord) * pc.color;
}
)";
    
    // 圆角矩形片段着色器 (SDF)
    const char* roundedRectFragment = R"(
#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
    vec4 rectParams;
    vec4 color;
    float radius;
} pc;

layout(location = 0) out vec4 outColor;

float roundedRectSDF(vec2 p, vec2 size, float r) {
    vec2 d = abs(p) - size + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 p = fragCoord - pc.rectParams.xy - pc.rectParams.zw * 0.5;
    vec2 halfSize = pc.rectParams.zw * 0.5;
    float d = roundedRectSDF(p, halfSize, pc.radius);
    float a = 1.0 - smoothstep(-1.0, 1.0, d);
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}
)";
    
    // 创建着色器和管线
    ShaderDesc shaderDesc;
    shaderDesc.vertexSource = basicVertexShader;
    shaderDesc.fragmentSource = solidColorFragment;
    shaderDesc.debugName = "BasicShader";
    
    basicShader_ = backend_->createShader(shaderDesc);
    
    // 创建管线
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = basicShader_;
    pipelineDesc.blendMode = 1;  // Alpha 混合
    
    // 添加顶点属性 - 只有 position
    pipelineDesc.bindings.push_back({0, sizeof(f32) * 2, false});
    pipelineDesc.attributes.push_back({0, VK_FORMAT_R32G32_SFLOAT, 0});
    
    rectPipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 纹理着色器
    shaderDesc.fragmentSource = textureFragment;
    textureShader_ = backend_->createShader(shaderDesc);
    
    pipelineDesc.shader = textureShader_;
    texturePipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 圆角矩形着色器 - 使用匹配的顶点着色器
    shaderDesc.vertexSource = roundRectVertexShader;
    shaderDesc.fragmentSource = roundedRectFragment;
    shaderDesc.debugName = "RoundRectShader";
    roundRectShader_ = backend_->createShader(shaderDesc);
    
    pipelineDesc.shader = roundRectShader_;
    roundRectPipeline_ = backend_->createPipeline(pipelineDesc);
}

void Renderer::destroyBuiltinResources() {
    if (quadVertexBuffer_.valid()) backend_->destroyBuffer(quadVertexBuffer_);
    if (quadIndexBuffer_.valid()) backend_->destroyBuffer(quadIndexBuffer_);
    if (defaultSampler_.valid()) backend_->destroySampler(defaultSampler_);
    
    if (rectPipeline_.valid()) backend_->destroyPipeline(rectPipeline_);
    if (roundRectPipeline_.valid()) backend_->destroyPipeline(roundRectPipeline_);
    if (texturePipeline_.valid()) backend_->destroyPipeline(texturePipeline_);
    if (textPipeline_.valid()) backend_->destroyPipeline(textPipeline_);
}

void Renderer::handleWindowEvent(const WindowEventData& event) {
    switch (event.type) {
        case WindowEvent::Resize:
            if (backend_) {
                backend_->onResize(event.width, event.height);
            }
            frameContext_.screenWidth = (f32)event.width;
            frameContext_.screenHeight = (f32)event.height;
            frameContext_.projection = Mat4::ortho(0, frameContext_.screenWidth,
                                                    frameContext_.screenHeight, 0, -1, 1);
            if (resizeCallback_) {
                resizeCallback_(event.width, event.height);
            }
            break;
            
        case WindowEvent::Close:
            running_ = false;
            break;
            
        case WindowEvent::Focus:
        case WindowEvent::Blur:
            // 处理焦点变化
            break;
            
        default:
            break;
    }
}

} // namespace Nova

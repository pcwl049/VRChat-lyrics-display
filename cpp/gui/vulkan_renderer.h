/**
 * Nova Renderer - Vulkan 渲染框架
 * 终极优化版本 v1.0
 * 
 * 特性:
 * - Vulkan 后端，GPU 精细控制
 * - MSDF 字体渲染，无限缩放质量
 * - 着色器驱动特效（模糊、阴影、发光）
 * - 多线程命令录制
 * - 资源池管理
 * - 后端自动切换（Vulkan → D3D11 → GDI+）
 * 
 * 适用项目:
 * - VRChat Lyrics Display
 * - VR Phone Overlay
 * - 未来所有 Windows GUI 项目
 */

#pragma once

// ============================================================================
// 核心依赖
// ============================================================================

#include <windows.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <array>
#include <span>

// ============================================================================
// 版本信息
// ============================================================================

#define NOVA_RENDERER_VERSION_MAJOR 1
#define NOVA_RENDERER_VERSION_MINOR 0
#define NOVA_RENDERER_VERSION_PATCH 0

namespace Nova {

// ============================================================================
// 前向声明
// ============================================================================

class Context;
class Device;
class SwapChain;
class CommandBuffer;
class Pipeline;
class RenderPass;
class Framebuffer;
class Buffer;
class Image;
class Texture;
class Sampler;
class Shader;
class DescriptorSet;
class DescriptorPool;
class Fence;
class Semaphore;
class Queue;
class MSDFGenerator;
class FontAtlas;
class ShaderEffect;
class ComputePipeline;
class ResourcePool;
class RenderGraph;
class Layer;
class LayerCompositor;

// ============================================================================
// 基础类型
// ============================================================================

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T>
using WeakRef = std::weak_ptr<T>;

template<typename T, typename... Args>
Ref<T> MakeRef(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}

// ============================================================================
// 数学类型
// ============================================================================

struct Vec2 {
    f32 x, y;
    Vec2() : x(0), y(0) {}
    Vec2(f32 x, f32 y) : x(x), y(y) {}
    static Vec2 zero() { return {0, 0}; }
    static Vec2 one() { return {1, 1}; }
};

struct Vec3 {
    f32 x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(f32 x, f32 y, f32 z) : x(x), y(y), z(z) {}
    static Vec3 zero() { return {0, 0, 0}; }
    static Vec3 one() { return {1, 1, 1}; }
};

struct Vec4 {
    f32 x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(1) {}
    Vec4(f32 x, f32 y, f32 z, f32 w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3& v, f32 w) : x(v.x), y(v.y), z(v.z), w(w) {}
    static Vec4 zero() { return {0, 0, 0, 0}; }
    static Vec4 one() { return {1, 1, 1, 1}; }
};

struct Mat3 {
    f32 m[9];
    Mat3() { for(int i = 0; i < 9; i++) m[i] = (i % 4 == 0) ? 1.0f : 0.0f; }
    static Mat3 identity() { return {}; }
    static Mat3 translate(f32 tx, f32 ty);
    static Mat3 scale(f32 sx, f32 sy);
    static Mat3 rotate(f32 angle);
    static Mat3 ortho(f32 left, f32 right, f32 bottom, f32 top);
};

struct Mat4 {
    f32 m[16];
    Mat4() { for(int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f; }
    static Mat4 identity() { return {}; }
    static Mat4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ);
    static Mat4 perspective(f32 fov, f32 aspect, f32 nearZ, f32 farZ);
};

struct Rect {
    f32 x, y, width, height;
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(f32 x, f32 y, f32 w, f32 h) : x(x), y(y), width(w), height(h) {}
    
    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(f32 px, f32 py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    bool intersects(const Rect& other) const {
        return x < other.x + other.width && x + width > other.x &&
               y < other.y + other.height && y + height > other.y;
    }
    Rect intersect(const Rect& other) const {
        f32 x1 = max(x, other.x);
        f32 y1 = max(y, other.y);
        f32 x2 = min(x + width, other.x + other.width);
        f32 y2 = min(y + height, other.y + other.height);
        if (x2 > x1 && y2 > y1) return {x1, y1, x2 - x1, y2 - y1};
        return {};
    }
};

struct Color {
    f32 r, g, b, a;
    
    Color() : r(0), g(0), b(0), a(1) {}
    Color(f32 r, f32 g, f32 b, f32 a = 1.0f) : r(r), g(g), b(b), a(a) {}
    
    static Color fromRGBA8(u8 r, u8 g, u8 b, u8 a = 255) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }
    
    static Color fromHex(u32 hex) {
        return {
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8) & 0xFF) / 255.0f,
            (hex & 0xFF) / 255.0f
        };
    }
    
    Vec4 toVec4() const { return {r, g, b, a}; }
    
    Color withAlpha(f32 newAlpha) const { return {r, g, b, newAlpha}; }
    
    // 预乘 alpha
    Color premultiplied() const { return {r * a, g * a, b * a, a}; }
    
    // 预定义颜色
    static Color transparent() { return {0, 0, 0, 0}; }
    static Color black() { return {0, 0, 0, 1}; }
    static Color white() { return {1, 1, 1, 1}; }
    static Color red() { return {1, 0, 0, 1}; }
    static Color green() { return {0, 1, 0, 1}; }
    static Color blue() { return {0, 0, 1, 1}; }
};

// ============================================================================
// Vulkan 错误处理
// ============================================================================

#define VK_CHECK(call) \
    do { \
        VkResult result = call; \
        if (result != VK_SUCCESS) { \
            Nova::Internal::LogError("Vulkan error: %s at %s:%d", \
                Nova::Internal::VkResultToString(result), __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

#define VK_CHECK_FATAL(call) \
    do { \
        VkResult result = call; \
        if (result != VK_SUCCESS) { \
            Nova::Internal::LogFatal("Vulkan fatal error: %s at %s:%d", \
                Nova::Internal::VkResultToString(result), __FILE__, __LINE__); \
            std::abort(); \
        } \
    } while(0)

namespace Internal {
    const char* VkResultToString(VkResult result);
    void LogError(const char* fmt, ...);
    void LogFatal(const char* fmt, ...);
    void LogInfo(const char* fmt, ...);
    void LogDebug(const char* fmt, ...);
}

// ============================================================================
// 配置结构
// ============================================================================

struct RendererConfig {
    // 窗口
    HWND hwnd = nullptr;
    u32 width = 800;
    u32 height = 600;
    bool vsync = true;
    
    // Vulkan 配置
    const char* appName = "Nova App";
    u32 appVersion = VK_MAKE_VERSION(1, 0, 0);
    bool enableValidation = true;
    bool enableRenderDoc = false;
    std::vector<const char*> requiredExtensions;
    std::vector<const char*> requiredLayers;
    
    // 渲染配置
    u32 maxFramesInFlight = 2;
    u32 threadCount = 0;  // 0 = 自动检测
    bool preferIntegratedGPU = false;  // 笔记本优化
    
    // 资源限制
    u32 maxTextures = 1024;
    u32 maxBuffers = 256;
    u32 maxDescriptors = 4096;
    u32 textureAtlasSize = 2048;
    u32 fontAtlasSize = 2048;
    
    // MSDF 字体
    bool enableMSDF = true;
    u32 msdfPixelRange = 4;
    u32 msdfMargin = 2;
    
    // 后端切换
    bool allowSoftwareFallback = true;
    BackendType preferredBackend = BackendType::Vulkan;
};

enum class BackendType {
    Vulkan,
    D3D12,
    D3D11,
    GDI,
    Auto
};

struct BackendCapabilities {
    BackendType type;
    bool supportsCompute;
    bool supportsGeometry;
    bool supportsTessellation;
    bool supportsRayTracing;
    bool supportsMeshShaders;
    u32 maxTextureSize;
    u32 maxComputeWorkGroupSize;
    f32 maxAnisotropy;
    std::string deviceName;
    std::string driverVersion;
};

// ============================================================================
// 资源句柄
// ============================================================================

using ResourceID = u64;
constexpr ResourceID INVALID_RESOURCE_ID = 0;

template<typename Tag>
class Handle {
public:
    Handle() : id_(INVALID_RESOURCE_ID) {}
    explicit Handle(ResourceID id) : id_(id) {}
    
    bool valid() const { return id_ != INVALID_RESOURCE_ID; }
    ResourceID id() const { return id_; }
    
    bool operator==(const Handle& other) const { return id_ == other.id_; }
    bool operator!=(const Handle& other) const { return id_ != other.id_; }
    
private:
    ResourceID id_;
};

struct TextureTag {};
struct BufferTag {};
struct ShaderTag {};
struct PipelineTag {};
struct FontTag {};
struct LayerTag {};

using TextureHandle = Handle<TextureTag>;
using BufferHandle = Handle<BufferTag>;
using ShaderHandle = Handle<ShaderTag>;
using PipelineHandle = Handle<PipelineTag>;
using FontHandle = Handle<FontTag>;
using LayerHandle = Handle<LayerTag>;

// ============================================================================
// 图形资源描述
// ============================================================================

struct TextureDesc {
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageType type = VK_IMAGE_TYPE_2D;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool generateMips = false;
    const char* debugName = nullptr;
};

struct BufferDesc {
    u64 size = 0;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO;
    bool mapped = false;  // 持久映射
    const char* debugName = nullptr;
};

struct SamplerDesc {
    VkFilter minFilter = VK_FILTER_LINEAR;
    VkFilter magFilter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode addressW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    f32 mipLodBias = 0.0f;
    f32 maxAnisotropy = 1.0f;
    VkCompareOp compareOp = VK_COMPARE_OP_NEVER;
    f32 minLod = 0.0f;
    f32 maxLod = VK_LOD_CLAMP_NONE;
    VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    bool unnormalizedCoordinates = false;
};

// ============================================================================
// 着色器系统
// ============================================================================

enum class ShaderStage : u32 {
    Vertex = VK_SHADER_STAGE_VERTEX_BIT,
    Fragment = VK_SHADER_STAGE_FRAGMENT_BIT,
    Compute = VK_SHADER_STAGE_COMPUTE_BIT,
    Geometry = VK_SHADER_STAGE_GEOMETRY_BIT,
    TessControl = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    TessEval = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
};

struct ShaderDefine {
    std::string name;
    std::string value;
};

struct ShaderDesc {
    std::string name;
    ShaderStage stage;
    std::vector<u8> bytecode;  // SPIR-V
    std::vector<ShaderDefine> defines;
    const char* entryPoint = "main";
};

// 着色器特效
struct EffectDesc {
    std::string name;
    ShaderHandle vertexShader;
    ShaderHandle fragmentShader;
    ShaderHandle computeShader;
    
    // Uniform 描述
    struct Uniform {
        std::string name;
        u32 binding;
        u32 size;
        VkDescriptorType type;
    };
    std::vector<Uniform> uniforms;
    
    // 渲染状态
    struct BlendState {
        bool enabled = true;
        VkBlendFactor srcColor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstColor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendFactor srcAlpha = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendOp colorOp = VK_BLEND_OP_ADD;
        VkBlendOp alphaOp = VK_BLEND_OP_ADD;
    } blend;
    
    struct DepthState {
        bool enabled = false;
        bool writeEnabled = false;
        VkCompareOp compareOp = VK_COMPARE_OP_LESS;
    } depth;
    
    struct RasterState {
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        f32 lineWidth = 1.0f;
    } raster;
};

// ============================================================================
// 绘制命令
// ============================================================================

struct DrawCommand {
    // 基础信息
    PipelineHandle pipeline;
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 firstVertex = 0;
    u32 firstInstance = 0;
    
    // 索引绘制
    bool indexed = false;
    BufferHandle indexBuffer;
    VkIndexType indexType = VK_INDEX_TYPE_UINT16;
    u32 indexCount = 0;
    u32 firstIndex = 0;
    i32 vertexOffset = 0;
    
    // 资源绑定
    std::vector<std::pair<u32, BufferHandle>> vertexBuffers;
    std::vector<std::pair<u32, TextureHandle>> sampledImages;
    std::vector<std::pair<u32, BufferHandle>> uniformBuffers;
    
    // Push Constants
    static constexpr u32 MAX_PUSH_CONSTANT_SIZE = 256;
    u8 pushConstants[MAX_PUSH_CONSTANT_SIZE];
    u32 pushConstantSize = 0;
    
    // 裁剪
    Rect scissor;
    bool hasScissor = false;
};

// ============================================================================
// 顶点格式
// ============================================================================

struct Vertex_Pos2_Tex2_Col4 {
    Vec2 position;
    Vec2 texCoord;
    Vec4 color;
    
    static VkVertexInputBindingDescription getBindingDesc() {
        return {0, sizeof(Vertex_Pos2_Tex2_Col4), VK_VERTEX_INPUT_RATE_VERTEX};
    }
    
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescs() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex_Pos2_Tex2_Col4, position)},
            {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex_Pos2_Tex2_Col4, texCoord)},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex_Pos2_Tex2_Col4, color)},
        }};
    }
};

// ============================================================================
// 字体系统
// ============================================================================

struct GlyphInfo {
    u32 glyphIndex = 0;
    Vec2 size;           // 字形尺寸
    Vec2 bearing;        // 基线偏移
    f32 advance = 0;     // 前进宽度
    Rect atlasRect;      // 图集中的位置
    bool hasMSDF = true; // 是否有 MSDF 数据
};

struct FontDesc {
    std::string path;        // 字体文件路径
    std::string name;        // 字体名称
    f32 pixelSize = 32.0f;   // 像素大小
    u32 oversampling = 2;    // 超采样倍数
    u32 rangeStart = 0x0020; // Unicode 范围起始
    u32 rangeEnd = 0xFFFF;   // Unicode 范围结束
    bool generateMSDF = true;
    bool preloadCommon = true; // 预加载常用字符
};

struct TextRun {
    std::string text;
    FontHandle font;
    Vec2 position;
    f32 scale = 1.0f;
    Color color;
    bool centerAlign = false;
    f32 maxWidth = 0;  // 0 = 无限制
    
    // 输出
    f32 measuredWidth = 0;
    f32 measuredHeight = 0;
};

// ============================================================================
// 图层系统
// ============================================================================

struct LayerDesc {
    std::string name;
    u32 width;
    u32 height;
    bool hasAlpha = true;
    bool cached = true;       // 是否缓存
    bool persistent = false;  // 是否持久化缓存
    bool renderOffscreen = true; // 是否离屏渲染
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

class Layer {
public:
    virtual ~Layer() = default;
    
    virtual LayerHandle handle() const = 0;
    virtual const std::string& name() const = 0;
    virtual Rect bounds() const = 0;
    virtual void setBounds(const Rect& bounds) = 0;
    
    virtual bool dirty() const = 0;
    virtual void markDirty() = 0;
    virtual void markDirty(const Rect& area) = 0;
    
    virtual bool visible() const = 0;
    virtual void setVisible(bool v) = 0;
    
    virtual i32 zOrder() const = 0;
    virtual void setZOrder(i32 z) = 0;
    
    virtual f32 opacity() const = 0;
    virtual void setOpacity(f32 o) = 0;
    
    // 渲染
    virtual void render(CommandBuffer* cmd) = 0;
    
    // 获取纹理
    virtual TextureHandle texture() const = 0;
};

// ============================================================================
// 动画系统
// ============================================================================

enum class EasingType {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInCubic,
    EaseOutCubic,
    EaseInOutCubic,
    EaseInElastic,
    EaseOutElastic,
    EaseOutBounce,
};

using AnimTime = f32;  // 0.0 - 1.0

using EasingFunc = f32(*)(AnimTime);

namespace Easing {
    f32 Linear(AnimTime t);
    f32 EaseIn(AnimTime t);
    f32 EaseOut(AnimTime t);
    f32 EaseInOut(AnimTime t);
    f32 EaseInQuad(AnimTime t);
    f32 EaseOutQuad(AnimTime t);
    f32 EaseInOutQuad(AnimTime t);
    f32 EaseInCubic(AnimTime t);
    f32 EaseOutCubic(AnimTime t);
    f32 EaseInOutCubic(AnimTime t);
    f32 EaseInElastic(AnimTime t);
    f32 EaseOutElastic(AnimTime t);
    f32 EaseOutBounce(AnimTime t);
}

template<typename T>
using LerpFunc = T(*)(const T& a, const T& b, f32 t);

template<typename T>
T Lerp(const T& a, const T& b, f32 t) {
    return a + (b - a) * t;
}

template<> Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t);
template<> Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t);
template<> Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t);
template<> Color Lerp(const Color& a, const Color& b, f32 t);

template<typename T>
struct AnimValue {
    T start;
    T end;
    f32 duration = 1.0f;     // 秒
    f32 elapsed = 0.0f;
    EasingFunc easing = Easing::EaseOut;
    LerpFunc<T> lerp = Lerp<T>;
    bool playing = false;
    bool loop = false;
    bool reverse = false;
    bool reverseOnLoop = false;
    f32 speed = 1.0f;
    
    T current() const {
        f32 t = easing(clamp(elapsed / duration, 0.0f, 1.0f));
        return lerp(start, end, reverse ? (1.0f - t) : t);
    }
    
    void play() { playing = true; elapsed = 0.0f; }
    void pause() { playing = false; }
    void stop() { playing = false; elapsed = 0.0f; }
    void restart() { elapsed = 0.0f; playing = true; }
    
    void update(f32 dt) {
        if (!playing) return;
        elapsed += dt * speed;
        if (elapsed >= duration) {
            if (loop) {
                if (reverseOnLoop) {
                    reverse = !reverse;
                }
                elapsed = 0.0f;
            } else {
                elapsed = duration;
                playing = false;
            }
        }
    }
    
    bool finished() const { return !playing && elapsed >= duration; }
};

using AnimFloat = AnimValue<f32>;
using AnimVec2 = AnimValue<Vec2>;
using AnimVec3 = AnimValue<Vec3>;
using AnimVec4 = AnimValue<Vec4>;
using AnimColor = AnimValue<Color>;

// ============================================================================
// 渲染后端接口
// ============================================================================

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    
    // 生命周期
    virtual bool initialize(const RendererConfig& config) = 0;
    virtual void shutdown() = 0;
    virtual void resize(u32 width, u32 height) = 0;
    
    // 帧控制
    virtual bool beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void present() = 0;
    
    // 资源创建
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle createPipeline(const EffectDesc& desc) = 0;
    virtual FontHandle createFont(const FontDesc& desc) = 0;
    virtual LayerHandle createLayer(const LayerDesc& desc) = 0;
    
    // 资源销毁
    virtual void destroyTexture(TextureHandle handle) = 0;
    virtual void destroyBuffer(BufferHandle handle) = 0;
    virtual void destroyShader(ShaderHandle handle) = 0;
    virtual void destroyPipeline(PipelineHandle handle) = 0;
    virtual void destroyFont(FontHandle handle) = 0;
    virtual void destroyLayer(LayerHandle handle) = 0;
    
    // 资源更新
    virtual void updateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset = 0) = 0;
    virtual void updateTexture(TextureHandle handle, const void* data, 
                               u32 width, u32 height, u32 mipLevel = 0) = 0;
    virtual void* mapBuffer(BufferHandle handle) = 0;
    virtual void unmapBuffer(BufferHandle handle) = 0;
    
    // 绘制
    virtual void draw(const DrawCommand& cmd) = 0;
    virtual void drawText(const TextRun& text) = 0;
    virtual void pushLayer(LayerHandle layer) = 0;
    virtual void popLayer() = 0;
    
    // Compute
    virtual void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) = 0;
    virtual void memoryBarrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) = 0;
    
    // 信息
    virtual BackendType type() const = 0;
    virtual const BackendCapabilities& capabilities() const = 0;
    virtual const char* name() const = 0;
    virtual bool isHardwareAccelerated() const = 0;
    
    // 统计
    struct Stats {
        u32 drawCalls = 0;
        u32 vertexCount = 0;
        u32 triangleCount = 0;
        u32 textureBinds = 0;
        u32 bufferBinds = 0;
        u32 pipelineBinds = 0;
        f64 frameTime = 0.0;
        f64 gpuTime = 0.0;
    };
    virtual const Stats& stats() const = 0;
    virtual void resetStats() = 0;
};

// ============================================================================
// Vulkan 后端
// ============================================================================

class VulkanBackend : public RenderBackend {
public:
    VulkanBackend();
    ~VulkanBackend() override;
    
    // RenderBackend 接口实现...
    bool initialize(const RendererConfig& config) override;
    void shutdown() override;
    void resize(u32 width, u32 height) override;
    
    bool beginFrame() override;
    void endFrame() override;
    void present() override;
    
    TextureHandle createTexture(const TextureDesc& desc) override;
    BufferHandle createBuffer(const BufferDesc& desc) override;
    ShaderHandle createShader(const ShaderDesc& desc) override;
    PipelineHandle createPipeline(const EffectDesc& desc) override;
    FontHandle createFont(const FontDesc& desc) override;
    LayerHandle createLayer(const LayerDesc& desc) override;
    
    void destroyTexture(TextureHandle handle) override;
    void destroyBuffer(BufferHandle handle) override;
    void destroyShader(ShaderHandle handle) override;
    void destroyPipeline(PipelineHandle handle) override;
    void destroyFont(FontHandle handle) override;
    void destroyLayer(LayerHandle handle) override;
    
    void updateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset = 0) override;
    void updateTexture(TextureHandle handle, const void* data, 
                       u32 width, u32 height, u32 mipLevel = 0) override;
    void* mapBuffer(BufferHandle handle) override;
    void unmapBuffer(BufferHandle handle) override;
    
    void draw(const DrawCommand& cmd) override;
    void drawText(const TextRun& text) override;
    void pushLayer(LayerHandle layer) override;
    void popLayer() override;
    
    void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void memoryBarrier(VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) override;
    
    BackendType type() const override { return BackendType::Vulkan; }
    const BackendCapabilities& capabilities() const override { return capabilities_; }
    const char* name() const override { return "Vulkan"; }
    bool isHardwareAccelerated() const override { return true; }
    const Stats& stats() const override { return stats_; }
    void resetStats() override { stats_ = {}; }
    
    // Vulkan 特定接口
    VkInstance instance() const { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    VkQueue computeQueue() const { return computeQueue_; }
    VkQueue presentQueue() const { return presentQueue_; }
    
private:
    // Vulkan 核心对象
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    
    // 队列
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    u32 graphicsQueueFamily_ = 0;
    u32 computeQueueFamily_ = 0;
    u32 presentQueueFamily_ = 0;
    
    // Swapchain
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkFormat swapchainFormat_;
    VkExtent2D swapchainExtent_;
    
    // 渲染过程
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    
    // 同步
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    u32 currentFrame_ = 0;
    u32 currentImageIndex_ = 0;
    
    // VMA 内存分配器
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    // 资源池
    ResourcePool* resourcePool_ = nullptr;
    
    // 字体图集
    FontAtlas* fontAtlas_ = nullptr;
    
    // MSDF 生成器
    MSDFGenerator* msdfGenerator_ = nullptr;
    
    // 着色器特效
    std::unordered_map<std::string, Ref<ShaderEffect>> effects_;
    
    // 能力信息
    BackendCapabilities capabilities_;
    
    // 统计
    Stats stats_;
    
    // 配置
    RendererConfig config_;
    
    // 私有方法
    bool createInstance();
    bool setupDebugMessenger();
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain();
    bool createImageViews();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPool();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createAllocator();
    void cleanupSwapchain();
    void recreateSwapchain();
    
    // 着色器编译
    bool compileShader(const ShaderDesc& desc, VkShaderModule* module);
    
    // MSDF 字体
    bool initMSDFGenerator();
    bool generateMSDFGlyph(FontHandle font, u32 codepoint);
};

// ============================================================================
// 资源池 - 线程安全
// ============================================================================

class ResourcePool {
public:
    ResourcePool(u32 maxTextures, u32 maxBuffers, u32 maxDescriptors);
    ~ResourcePool();
    
    // 纹理
    TextureHandle allocateTexture();
    void deallocateTexture(TextureHandle handle);
    void setTextureData(TextureHandle handle, Ref<Image> image);
    Ref<Image> getTextureData(TextureHandle handle);
    
    // 缓冲区
    BufferHandle allocateBuffer();
    void deallocateBuffer(BufferHandle handle);
    void setBufferData(BufferHandle handle, Ref<Buffer> buffer);
    Ref<Buffer> getBufferData(BufferHandle handle);
    
    // 描述符集
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout);
    void resetDescriptors();
    
private:
    std::mutex mutex_;
    std::unordered_map<ResourceID, Ref<Image>> textures_;
    std::unordered_map<ResourceID, Ref<Buffer>> buffers_;
    
    std::queue<ResourceID> freeTextureIDs_;
    std::queue<ResourceID> freeBufferIDs_;
    std::atomic<ResourceID> nextTextureID_{1};
    std::atomic<ResourceID> nextBufferID_{1};
    
    // 描述符池
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    u32 maxTextures_;
    u32 maxBuffers_;
};

// ============================================================================
// MSDF 字体生成器
// ============================================================================

class MSDFGenerator {
public:
    MSDFGenerator(u32 atlasSize, u32 pixelRange, u32 margin);
    ~MSDFGenerator();
    
    // 加载字体
    bool loadFont(const FontDesc& desc, FontHandle handle);
    
    // 生成字形
    bool generateGlyph(FontHandle font, u32 codepoint, GlyphInfo& outInfo);
    
    // 预生成常用字形
    void pregenerateCommon(FontHandle font);
    
    // 获取图集纹理
    TextureHandle atlasTexture() const;
    
    // 获取字形信息
    const GlyphInfo* getGlyphInfo(FontHandle font, u32 codepoint) const;
    
    // 光栅化到缓冲区
    void rasterizeText(const TextRun& run, std::vector<Vertex_Pos2_Tex2_Col4>& vertices,
                       std::vector<u16>& indices);
    
private:
    struct FontData;
    std::unordered_map<ResourceID, std::unique_ptr<FontData>> fonts_;
    
    TextureHandle atlasTexture_;
    u32 atlasSize_;
    u32 pixelRange_;
    u32 margin_;
    
    // 图集分配器
    struct AtlasAllocator {
        u32 currentX = 0;
        u32 currentY = 0;
        u32 rowHeight = 0;
        
        bool allocate(u32 width, u32 height, u32 atlasSize, Rect& outRect);
    } atlasAllocator_;
};

// ============================================================================
// 着色器特效
// ============================================================================

class ShaderEffect {
public:
    static Ref<ShaderEffect> create(VulkanBackend* backend, const EffectDesc& desc);
    
    PipelineHandle pipeline() const { return pipeline_; }
    const std::string& name() const { return name_; }
    
    // 设置 uniform
    void setUniform(const std::string& name, f32 value);
    void setUniform(const std::string& name, const Vec2& value);
    void setUniform(const std::string& name, const Vec3& value);
    void setUniform(const std::string& name, const Vec4& value);
    void setUniform(const std::string& name, const Mat3& value);
    void setUniform(const std::string& name, const Mat4& value);
    void setUniform(const std::string& name, const Color& value);
    
    // 应用到命令缓冲区
    void apply(class VulkanCommandBuffer* cmd);
    
private:
    ShaderEffect() = default;
    
    std::string name_;
    PipelineHandle pipeline_;
    std::unordered_map<std::string, u32> uniformLocations_;
    std::vector<u8> uniformData_;
};

// ============================================================================
// 计算着色器特效
// ============================================================================

class ComputeEffect {
public:
    static Ref<ComputeEffect> create(VulkanBackend* backend, const std::string& name,
                                     ShaderHandle computeShader);
    
    PipelineHandle pipeline() const { return pipeline_; }
    
    void setUniform(const std::string& name, f32 value);
    void setUniform(const std::string& name, const Vec2& value);
    void setUniform(const std::string& name, const Vec3& value);
    void setUniform(const std::string& name, const Vec4& value);
    
    void setInputTexture(u32 binding, TextureHandle texture);
    void setOutputTexture(u32 binding, TextureHandle texture);
    
    void dispatch(class VulkanCommandBuffer* cmd, u32 x, u32 y, u32 z = 1);
    
private:
    ComputeEffect() = default;
    
    PipelineHandle pipeline_;
    std::vector<u8> uniformData_;
    std::vector<std::pair<u32, TextureHandle>> inputTextures_;
    std::vector<std::pair<u32, TextureHandle>> outputTextures_;
};

// ============================================================================
// 预定义特效
// ============================================================================

namespace Effects {
    // 基础形状
    Ref<ShaderEffect> createFillRect(VulkanBackend* backend);
    Ref<ShaderEffect> createFillRoundRect(VulkanBackend* backend);
    Ref<ShaderEffect> createFillGradient(VulkanBackend* backend);
    Ref<ShaderEffect> createFillGradientRoundRect(VulkanBackend* backend);
    
    // 文字
    Ref<ShaderEffect> createTextMSDF(VulkanBackend* backend);
    Ref<ShaderEffect> createTextSDF(VulkanBackend* backend);
    
    // 图片
    Ref<ShaderEffect> createTexture(VulkanBackend* backend);
    Ref<ShaderEffect> createTextureAlpha(VulkanBackend* backend);
    
    // 计算特效
    Ref<ComputeEffect> createBlur(VulkanBackend* backend, u32 radius);
    Ref<ComputeEffect> createGaussianBlur(VulkanBackend* backend, u32 radius);
    Ref<ComputeEffect> createBoxBlur(VulkanBackend* backend, u32 radius);
    Ref<ComputeEffect> createBloom(VulkanBackend* backend, f32 threshold, f32 intensity);
    Ref<ComputeEffect> createChromaticAberration(VulkanBackend* backend, f32 strength);
    Ref<ComputeEffect> createColorAdjust(VulkanBackend* backend);
}

// ============================================================================
// 渲染器主类
// ============================================================================

class Renderer {
public:
    Renderer();
    ~Renderer();
    
    // 初始化
    bool initialize(const RendererConfig& config);
    void shutdown();
    
    // 窗口大小
    void resize(u32 width, u32 height);
    Vec2 size() const { return {f32(config_.width), f32(config_.height)}; }
    
    // 后端
    BackendType currentBackend() const;
    const BackendCapabilities& capabilities() const;
    
    // 帧控制
    bool beginFrame();
    void endFrame();
    void present();
    
    // 资源创建
    TextureHandle createTexture(const TextureDesc& desc);
    BufferHandle createBuffer(const BufferDesc& desc);
    ShaderHandle createShader(const ShaderDesc& desc);
    PipelineHandle createPipeline(const EffectDesc& desc);
    FontHandle createFont(const FontDesc& desc);
    LayerHandle createLayer(const LayerDesc& desc);
    
    // 资源销毁
    void destroyTexture(TextureHandle handle);
    void destroyBuffer(BufferHandle handle);
    void destroyShader(ShaderHandle handle);
    void destroyPipeline(BufferHandle handle);
    void destroyFont(FontHandle handle);
    void destroyLayer(LayerHandle handle);
    
    // 即时模式绘制 API
    void drawRect(const Rect& rect, const Color& color);
    void drawRoundRect(const Rect& rect, f32 radius, const Color& color);
    void drawRectGradient(const Rect& rect, const Color& color1, const Color& color2, bool vertical);
    void drawRoundRectGradient(const Rect& rect, f32 radius, const Color& color1, const Color& color2, bool vertical);
    void drawTexture(const Rect& rect, TextureHandle texture, const Color& tint = Color::white());
    void drawTextureRect(const Rect& destRect, TextureHandle texture, const Rect& srcRect);
    void drawText(const TextRun& text);
    void drawLine(const Vec2& start, const Vec2& end, f32 thickness, const Color& color);
    void drawShadow(const Rect& rect, f32 radius, f32 blur, const Color& color);
    
    // 图层操作
    void pushLayer(LayerHandle layer);
    void popLayer();
    LayerHandle currentLayer() const;
    
    // 裁剪
    void pushClip(const Rect& rect);
    void popClip();
    
    // Compute
    void dispatchCompute(Ref<ComputeEffect> effect, u32 x, u32 y, u32 z = 1);
    
    // 后处理
    void applyBlur(TextureHandle input, TextureHandle output, f32 radius);
    void applyBloom(TextureHandle input, TextureHandle output, f32 threshold, f32 intensity);
    
    // 统计
    const RenderBackend::Stats& stats() const;
    void resetStats();
    
    // 动画更新
    void updateAnimations(f32 deltaTime);
    
private:
    std::unique_ptr<RenderBackend> backend_;
    RendererConfig config_;
    bool initialized_ = false;
    
    // 后端选择
    bool selectBackend();
    bool tryVulkanBackend();
    bool tryD3D11Backend();
    bool tryGDIBackend();
    
    // 内置特效缓存
    std::unordered_map<std::string, Ref<ShaderEffect>> cachedEffects_;
    std::unordered_map<std::string, Ref<ComputeEffect>> cachedComputeEffects_;
    
    // 默认字体
    FontHandle defaultFont_;
};

// ============================================================================
// 工具函数
// ============================================================================

namespace Utils {
    // 编译 GLSL 到 SPIR-V
    std::vector<u32> compileGLSL(const std::string& source, VkShaderStageFlagBits stage);
    
    // 加载 SPIR-V 文件
    std::vector<u8> loadSPIRV(const std::string& path);
    
    // 纹理压缩
    bool compressTextureBC7(const u8* rgba, u32 width, u32 height, std::vector<u8>& output);
    
    // 高质量缩放
    void resizeImageBicubic(const u8* src, u32 srcW, u32 srcH,
                            u8* dst, u32 dstW, u32 dstH);
}

// ============================================================================
// 全局渲染器访问
// ============================================================================

Renderer* GetRenderer();
void SetRenderer(Renderer* renderer);

// 便捷宏
#define NOVA Nova::GetRenderer()

} // namespace Nova

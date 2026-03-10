/**
 * Nova Renderer - Render Graph
 * 渲染图架构 - 自动资源管理和依赖调度
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <queue>

namespace Nova {

// 前向声明
class VulkanBackend;
struct FrameContext;

// ============================================================================
// 资源描述
// ============================================================================

// 资源类型
enum class ResourceType : u32 {
    Texture,
    Buffer,
    Reference  // 外部资源引用
};

// 资源使用方式
enum class ResourceUsage : u32 {
    None = 0,
    ShaderRead = 1 << 0,
    ColorAttachment = 1 << 1,
    DepthStencilAttachment = 1 << 2,
    TransferSrc = 1 << 3,
    TransferDst = 1 << 4,
    Storage = 1 << 5,
    Present = 1 << 6
};

inline ResourceUsage operator|(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline ResourceUsage operator&(ResourceUsage a, ResourceUsage b) {
    return static_cast<ResourceUsage>(static_cast<u32>(a) & static_cast<u32>(b));
}

inline bool hasUsage(ResourceUsage flags, ResourceUsage flag) {
    return (static_cast<u32>(flags) & static_cast<u32>(flag)) != 0;
}

// 资源句柄 (渲染图内部使用)
struct RGResourceHandle {
    u32 index;
    u32 generation;
    
    bool valid() const { return index != UINT32_MAX; }
    bool operator==(const RGResourceHandle& o) const { return index == o.index && generation == o.generation; }
};

// 资源描述
struct RGTextureDesc {
    std::string name;
    u32 width = 0;
    u32 height = 0;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    bool transient = false;  // 是否为瞬时资源
};

struct RGBufferDesc {
    std::string name;
    u64 size = 0;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bool transient = false;
};

// ============================================================================
// Pass 定义
// ============================================================================

// Pass 执行回调
using PassExecuteCallback = std::function<void(VkCommandBuffer, const FrameContext&)>;

// Pass 资源访问
struct RGResourceAccess {
    RGResourceHandle resource;
    ResourceUsage usage;
    VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    VkAccessFlags accessMask = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;  // 对于纹理
};

// Pass 描述
struct RGPassDesc {
    std::string name;
    
    // 输入输出资源
    std::vector<RGResourceAccess> inputs;
    std::vector<RGResourceAccess> outputs;
    
    // 执行回调
    PassExecuteCallback execute;
    
    // Pass 类型
    enum class Type : u32 {
        Graphics,
        Compute,
        Transfer,
        Present
    } type = Type::Graphics;
    
    // 优先级 (影响执行顺序)
    i32 priority = 0;
    
    // 是否可以与其他 Pass 并行执行
    bool allowParallel = true;
    
    // 快捷方法
    RGPassDesc& read(RGResourceHandle resource, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    RGPassDesc& write(RGResourceHandle resource, VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    RGPassDesc& readWrite(RGResourceHandle resource);
    RGPassDesc& setColorOutput(RGResourceHandle resource, u32 attachmentIndex = 0);
    RGPassDesc& setDepthOutput(RGResourceHandle resource);
    RGPassDesc& setExecute(PassExecuteCallback callback);
};

// ============================================================================
// 渲染图
// ============================================================================

class RenderGraph {
public:
    RenderGraph();
    ~RenderGraph();
    
    // 初始化
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 每帧重置
    void beginFrame();
    void endFrame();
    
    // 创建资源
    RGResourceHandle createTexture(const RGTextureDesc& desc);
    RGResourceHandle createBuffer(const RGBufferDesc& desc);
    RGResourceHandle importTexture(TextureHandle texture, const std::string& name);
    RGResourceHandle importBuffer(BufferHandle buffer, const std::string& name);
    
    // 添加 Pass
    RGPassDesc& addPass(const std::string& name, RGPassDesc::Type type = RGPassDesc::Type::Graphics);
    
    // 获取资源
    TextureHandle getTexture(RGResourceHandle handle) const;
    BufferHandle getBuffer(RGResourceHandle handle) const;
    
    // 编译和执行
    void compile();
    void execute(VkCommandBuffer cmd, const FrameContext& ctx);
    
    // 设置后备缓冲
    void setBackbuffer(RGResourceHandle handle);
    
    // 获取调试信息
    void printGraph() const;
    
private:
    // 内部资源结构
    struct Resource {
        std::string name;
        ResourceType type;
        RGResourceHandle handle;
        
        // 描述
        RGTextureDesc textureDesc;
        RGBufferDesc bufferDesc;
        
        // 实际资源
        TextureHandle texture;
        BufferHandle buffer;
        
        // 状态跟踪
        VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags lastStage = 0;
        VkAccessFlags lastAccess = 0;
        
        // 生命周期
        u32 firstUsePass = UINT32_MAX;
        u32 lastUsePass = 0;
        
        // 是否为外部资源
        bool imported = false;
        bool transient = false;
    };
    
    // 内部 Pass 结构
    struct Pass {
        std::string name;
        RGPassDesc::Type type;
        RGPassDesc desc;
        
        // 编译后的数据
        u32 index;
        std::vector<u32> dependencies;
        std::vector<u32> dependents;
        
        // 资源状态
        std::vector<RGResourceAccess> reads;
        std::vector<RGResourceAccess> writes;
        
        // 执行状态
        bool executed = false;
    };
    
    // 辅助函数
    void calculateDependencies();
    void calculateResourceLifetimes();
    void insertBarriers(VkCommandBuffer cmd, const Pass& pass);
    void insertImageBarrier(VkCommandBuffer cmd, Resource& resource,
                            VkImageLayout newLayout,
                            VkPipelineStageFlags dstStage,
                            VkAccessFlags dstAccess);
    void createTransientResources();
    void destroyTransientResources();
    
    // 拓扑排序
    std::vector<u32> topologicalSort();
    
    VulkanBackend* backend_ = nullptr;
    
    std::vector<Resource> resources_;
    std::vector<Pass> passes_;
    
    RGResourceHandle backbuffer_;
    
    std::vector<u32> executionOrder_;
    bool compiled_ = false;
    
    // 资源别名 (相同大小的瞬时资源可以共享内存)
    std::unordered_map<std::string, std::vector<RGResourceHandle>> aliasGroups_;
};

// ============================================================================
// 渲染图构建器 (辅助类)
// ============================================================================

class RenderGraphBuilder {
public:
    RenderGraphBuilder(RenderGraph& graph);
    
    // 常用 Pass 模板
    RGPassDesc& addGraphicsPass(const std::string& name);
    RGPassDesc& addComputePass(const std::string& name);
    RGPassDesc& addTransferPass(const std::string& name);
    
    // 常用资源配置
    RGResourceHandle createColorAttachment(const std::string& name, u32 width, u32 height, VkFormat format);
    RGResourceHandle createDepthAttachment(const std::string& name, u32 width, u32 height);
    RGResourceHandle createStorageTexture(const std::string& name, u32 width, u32 height, VkFormat format);
    
private:
    RenderGraph& graph_;
};

// ============================================================================
// 内置 Pass 模板
// ============================================================================

namespace PassTemplates {
    
    // 后处理 Pass
    RGPassDesc createBlurPass(RenderGraph& graph, 
                               RGResourceHandle input,
                               RGResourceHandle output,
                               f32 blurRadius = 5.0f);
    
    RGPassDesc createBloomPass(RenderGraph& graph,
                                RGResourceHandle input,
                                RGResourceHandle output,
                                f32 threshold = 1.0f,
                                f32 intensity = 0.5f);
    
    RGPassDesc createTonemapPass(RenderGraph& graph,
                                  RGResourceHandle input,
                                  RGResourceHandle output,
                                  const std::string& method = "ACES");
    
    // UI Pass
    RGPassDesc createUIPass(RenderGraph& graph,
                             RGResourceHandle colorTarget,
                             RGResourceHandle depthTarget);
    
}

} // namespace Nova

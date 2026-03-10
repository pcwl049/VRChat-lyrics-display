/**
 * Nova Renderer - Instanced Rendering
 * 实例化渲染批处理系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <memory>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 实例数据
// ============================================================================

// 实例变换数据
struct alignas(16) InstanceData {
    Mat4 transform;
    Vec4 color;
    Vec4 customData;  // 自定义数据 (如 UV 偏移、大小等)
};

// 实例化绘制参数
struct InstanceDrawParams {
    PipelineHandle pipeline;
    BufferHandle vertexBuffer;
    BufferHandle indexBuffer;
    TextureHandle texture;
    u32 vertexCount = 0;
    u32 indexCount = 0;
    u32 firstVertex = 0;
    u32 firstIndex = 0;
};

// ============================================================================
// 实例化批处理
// ============================================================================

class InstanceBatch {
public:
    InstanceBatch();
    ~InstanceBatch();
    
    // 初始化
    bool initialize(VulkanBackend* backend, u32 maxInstances = 1024);
    void shutdown();
    
    // 添加实例
    u32 addInstance(const InstanceData& data);
    void updateInstance(u32 index, const InstanceData& data);
    void removeInstance(u32 index);
    void clear();
    
    // 获取数据
    const std::vector<InstanceData>& getInstances() const { return instances_; }
    u32 getInstanceCount() const { return static_cast<u32>(instances_.size()); }
    
    // 获取缓冲
    BufferHandle getInstanceBuffer() const { return instanceBuffer_; }
    
    // 绘制
    void draw(VkCommandBuffer cmd, const InstanceDrawParams& params);
    
private:
    void updateBuffer();
    
    VulkanBackend* backend_ = nullptr;
    std::vector<InstanceData> instances_;
    BufferHandle instanceBuffer_;
    u32 maxInstances_ = 1024;
    bool dirty_ = false;
};

// ============================================================================
// 批处理管理器
// ============================================================================

// 批处理键 (用于分组)
struct BatchKey {
    PipelineHandle pipeline;
    TextureHandle texture;
    BufferHandle vertexBuffer;
    
    bool operator==(const BatchKey& o) const {
        return pipeline == o.pipeline && 
               texture == o.texture && 
               vertexBuffer == o.vertexBuffer;
    }
};

struct BatchKeyHash {
    size_t operator()(const BatchKey& k) const {
        size_t h = 0;
        h ^= std::hash<u32>{}(k.pipeline.index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.texture.index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.vertexBuffer.index) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// 批处理条目
struct BatchEntry {
    InstanceBatch batch;
    InstanceDrawParams params;
};

// 批处理管理器
class BatchManager {
public:
    BatchManager();
    ~BatchManager();
    
    bool initialize(VulkanBackend* backend, u32 maxInstancesPerBatch = 1024);
    void shutdown();
    
    // 每帧操作
    void beginFrame();
    void endFrame();
    
    // 添加实例化绘制
    void drawInstanced(const InstanceDrawParams& params, const InstanceData& instance);
    void drawInstancedBatch(const InstanceDrawParams& params, const std::vector<InstanceData>& instances);
    
    // 执行所有批处理
    void flush(VkCommandBuffer cmd);
    
    // 统计
    u32 getBatchCount() const { return static_cast<u32>(batches_.size()); }
    u32 getTotalInstanceCount() const;
    u32 getDrawCallCount() const { return drawCallCount_; }
    
    // 设置实例限制
    void setMaxInstancesPerBatch(u32 max) { maxInstancesPerBatch_ = max; }
    
private:
    BatchKey createKey(const InstanceDrawParams& params) const;
    void flushBatch(VkCommandBuffer cmd, BatchEntry& entry);
    
    VulkanBackend* backend_ = nullptr;
    std::unordered_map<BatchKey, BatchEntry, BatchKeyHash> batches_;
    
    u32 maxInstancesPerBatch_ = 1024;
    u32 drawCallCount_ = 0;
};

// ============================================================================
// 动态实例缓冲
// ============================================================================

class DynamicInstanceBuffer {
public:
    DynamicInstanceBuffer();
    ~DynamicInstanceBuffer();
    
    bool initialize(VulkanBackend* backend, u32 initialSize = 1024, u32 maxSize = 65536);
    void shutdown();
    
    // 分配实例数据空间
    u32 allocate(u32 count);
    void write(u32 offset, const InstanceData* data, u32 count);
    
    // 获取缓冲
    BufferHandle getBuffer() const { return buffer_; }
    
    // 重置 (每帧调用)
    void reset();
    
    // 统计
    u32 getUsedCount() const { return usedCount_; }
    u32 getCapacity() const { return capacity_; }
    
private:
    bool grow(u32 newCapacity);
    
    VulkanBackend* backend_ = nullptr;
    BufferHandle buffer_;
    InstanceData* mappedData_ = nullptr;
    
    u32 capacity_ = 0;
    u32 usedCount_ = 0;
    u32 maxSize_ = 65536;
};

// ============================================================================
// 间接绘制
// ============================================================================

// 间接绘制命令
struct IndirectDrawCommand {
    u32 vertexCount;
    u32 instanceCount;
    u32 firstVertex;
    u32 firstInstance;
};

// 间接绘制索引命令
struct IndirectDrawIndexedCommand {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

// 间接绘制管理器
class IndirectDrawManager {
public:
    IndirectDrawManager();
    ~IndirectDrawManager();
    
    bool initialize(VulkanBackend* backend, u32 maxDraws = 1024);
    void shutdown();
    
    // 添加绘制命令
    u32 addDrawCommand(const IndirectDrawCommand& cmd);
    u32 addDrawIndexedCommand(const IndirectDrawIndexedCommand& cmd);
    
    // 更新命令
    void updateDrawCommand(u32 index, const IndirectDrawCommand& cmd);
    void updateDrawIndexedCommand(u32 index, const IndirectDrawIndexedCommand& cmd);
    
    // 执行
    void executeDraw(VkCommandBuffer cmd, u32 firstDraw = 0, u32 drawCount = UINT32_MAX);
    void executeDrawIndexed(VkCommandBuffer cmd, u32 firstDraw = 0, u32 drawCount = UINT32_MAX);
    
    // GPU 更新 (用于 GPU 驱动的剔除)
    BufferHandle getDrawCommandBuffer() const { return drawCommandBuffer_; }
    BufferHandle getDrawCountBuffer() const { return drawCountBuffer_; }
    
    // 重置
    void reset();
    
private:
    VulkanBackend* backend_ = nullptr;
    
    BufferHandle drawCommandBuffer_;
    BufferHandle drawCountBuffer_;
    
    std::vector<IndirectDrawCommand> drawCommands_;
    std::vector<IndirectDrawIndexedCommand> drawIndexedCommands_;
    
    u32 maxDraws_ = 1024;
};

// ============================================================================
// GPU 驱动的实例剔除
// ============================================================================

struct CullingParams {
    Mat4 viewProj;
    Vec4 frustumPlanes[6];  // 视锥体平面
    Vec3 lodReferencePoint;
    f32 lodBias;
};

class GPUInstanceCuller {
public:
    GPUInstanceCuller();
    ~GPUInstanceCuller();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 执行剔除
    void cull(VkCommandBuffer cmd,
              BufferHandle instanceBuffer,
              BufferHandle outputBuffer,
              BufferHandle drawCommandBuffer,
              u32 instanceCount,
              const CullingParams& params);
    
    // LOD 选择
    void selectLOD(VkCommandBuffer cmd,
                   BufferHandle instanceBuffer,
                   BufferHandle lodBuffer,
                   u32 instanceCount,
                   const CullingParams& params);
    
    // 着色器
    bool loadCullingShader();
    
private:
    VulkanBackend* backend_ = nullptr;
    
    PipelineHandle cullPipeline_;
    PipelineHandle lodPipeline_;
    ShaderHandle cullShader_;
    
    BufferHandle indirectBuffer_;
    BufferHandle counterBuffer_;
};

// ============================================================================
// 粒子实例化渲染
// ============================================================================

struct ParticleInstance {
    Vec3 position;
    f32 size;
    Vec4 color;
    Vec2 uvOffset;
    Vec2 uvScale;
    f32 rotation;
    f32 padding[3];
};

class ParticleInstancedRenderer {
public:
    ParticleInstancedRenderer();
    ~ParticleInstancedRenderer();
    
    bool initialize(VulkanBackend* backend, u32 maxParticles = 10000);
    void shutdown();
    
    // 设置粒子数据
    void setParticles(const std::vector<ParticleInstance>& particles);
    
    // 渲染
    void render(VkCommandBuffer cmd, TextureHandle particleTexture);
    
private:
    VulkanBackend* backend_ = nullptr;
    
    BufferHandle instanceBuffer_;
    BufferHandle vertexBuffer_;
    BufferHandle indexBuffer_;
    
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    SamplerHandle sampler_;
    
    u32 maxParticles_ = 10000;
    u32 particleCount_ = 0;
};

} // namespace Nova

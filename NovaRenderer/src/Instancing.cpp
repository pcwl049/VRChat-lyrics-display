/**
 * Nova Renderer - Instanced Rendering Implementation
 */

#include "Nova/Instancing.h"
#include "Nova/VulkanBackend.h"
#include <algorithm>

namespace Nova {

// ============================================================================
// InstanceBatch Implementation
// ============================================================================

InstanceBatch::InstanceBatch() = default;

InstanceBatch::~InstanceBatch() {
    shutdown();
}

bool InstanceBatch::initialize(VulkanBackend* backend, u32 maxInstances) {
    backend_ = backend;
    maxInstances_ = maxInstances;
    
    // 创建实例缓冲
    BufferDesc desc;
    desc.size = sizeof(InstanceData) * maxInstances;
    desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    desc.mapped = true;
    
    instanceBuffer_ = backend_->createBuffer(desc);
    if (!instanceBuffer_.valid()) {
        return false;
    }
    
    instances_.reserve(maxInstances);
    return true;
}

void InstanceBatch::shutdown() {
    if (backend_) {
        if (instanceBuffer_.valid()) {
            backend_->destroyBuffer(instanceBuffer_);
        }
    }
    instances_.clear();
    backend_ = nullptr;
}

u32 InstanceBatch::addInstance(const InstanceData& data) {
    if (instances_.size() >= maxInstances_) {
        return UINT32_MAX;
    }
    
    u32 index = static_cast<u32>(instances_.size());
    instances_.push_back(data);
    dirty_ = true;
    return index;
}

void InstanceBatch::updateInstance(u32 index, const InstanceData& data) {
    if (index < instances_.size()) {
        instances_[index] = data;
        dirty_ = true;
    }
}

void InstanceBatch::removeInstance(u32 index) {
    if (index < instances_.size()) {
        instances_.erase(instances_.begin() + index);
        dirty_ = true;
    }
}

void InstanceBatch::clear() {
    instances_.clear();
    dirty_ = true;
}

void InstanceBatch::updateBuffer() {
    if (!dirty_ || instances_.empty()) return;
    
    backend_->updateBuffer(instanceBuffer_, instances_.data(), 
                           instances_.size() * sizeof(InstanceData));
    dirty_ = false;
}

void InstanceBatch::draw(VkCommandBuffer cmd, const InstanceDrawParams& params) {
    if (instances_.empty()) return;
    
    updateBuffer();
    
    // 绑定管线
    backend_->setPipeline(params.pipeline);
    
    // 绑定顶点缓冲 (模型顶点 + 实例数据)
    VkBuffer vertexBuffers[] = {
        backend_->buffers_.get(params.vertexBuffer)->buffer,
        backend_->buffers_.get(instanceBuffer_)->buffer
    };
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
    
    // 绑定索引缓冲
    if (params.indexBuffer.valid()) {
        auto* indexBuffer = backend_->buffers_.get(params.indexBuffer);
        vkCmdBindIndexBuffer(cmd, indexBuffer->buffer, 0, VK_INDEX_TYPE_UINT16);
        
        // 索引绘制
        vkCmdDrawIndexed(cmd, params.indexCount, static_cast<u32>(instances_.size()),
                         params.firstIndex, 0, 0);
    } else {
        // 非索引绘制
        vkCmdDraw(cmd, params.vertexCount, static_cast<u32>(instances_.size()),
                  params.firstVertex, 0);
    }
}

// ============================================================================
// BatchManager Implementation
// ============================================================================

BatchManager::BatchManager() = default;

BatchManager::~BatchManager() {
    shutdown();
}

bool BatchManager::initialize(VulkanBackend* backend, u32 maxInstancesPerBatch) {
    backend_ = backend;
    maxInstancesPerBatch_ = maxInstancesPerBatch;
    return true;
}

void BatchManager::shutdown() {
    batches_.clear();
    backend_ = nullptr;
}

void BatchManager::beginFrame() {
    // 清除上一帧的批处理
    for (auto& [key, entry] : batches_) {
        entry.batch.clear();
    }
    drawCallCount_ = 0;
}

void BatchManager::endFrame() {
    // 不做任何事，等待 flush
}

BatchKey BatchManager::createKey(const InstanceDrawParams& params) const {
    BatchKey key;
    key.pipeline = params.pipeline;
    key.texture = params.texture;
    key.vertexBuffer = params.vertexBuffer;
    return key;
}

void BatchManager::drawInstanced(const InstanceDrawParams& params, const InstanceData& instance) {
    auto key = createKey(params);
    
    auto it = batches_.find(key);
    if (it == batches_.end()) {
        // 创建新批处理
        BatchEntry entry;
        entry.batch.initialize(backend_, maxInstancesPerBatch_);
        entry.params = params;
        
        it = batches_.emplace(key, std::move(entry)).first;
    }
    
    // 检查是否需要新批处理
    if (it->second.batch.getInstanceCount() >= maxInstancesPerBatch_) {
        // 创建溢出批处理
        BatchKey overflowKey = key;
        // 修改 key 以区分
        overflowKey.pipeline.index += batches_.size();
        
        BatchEntry entry;
        entry.batch.initialize(backend_, maxInstancesPerBatch_);
        entry.params = params;
        
        auto result = batches_.emplace(overflowKey, std::move(entry));
        it = result.first;
    }
    
    it->second.batch.addInstance(instance);
}

void BatchManager::drawInstancedBatch(const InstanceDrawParams& params, const std::vector<InstanceData>& instances) {
    for (const auto& instance : instances) {
        drawInstanced(params, instance);
    }
}

void BatchManager::flush(VkCommandBuffer cmd) {
    for (auto& [key, entry] : batches_) {
        if (entry.batch.getInstanceCount() > 0) {
            flushBatch(cmd, entry);
            drawCallCount_++;
        }
    }
}

void BatchManager::flushBatch(VkCommandBuffer cmd, BatchEntry& entry) {
    entry.batch.draw(cmd, entry.params);
}

u32 BatchManager::getTotalInstanceCount() const {
    u32 total = 0;
    for (const auto& [key, entry] : batches_) {
        total += entry.batch.getInstanceCount();
    }
    return total;
}

// ============================================================================
// DynamicInstanceBuffer Implementation
// ============================================================================

DynamicInstanceBuffer::DynamicInstanceBuffer() = default;

DynamicInstanceBuffer::~DynamicInstanceBuffer() {
    shutdown();
}

bool DynamicInstanceBuffer::initialize(VulkanBackend* backend, u32 initialSize, u32 maxSize) {
    backend_ = backend;
    capacity_ = initialSize;
    maxSize_ = maxSize;
    
    BufferDesc desc;
    desc.size = sizeof(InstanceData) * capacity_;
    desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    desc.mapped = true;
    
    buffer_ = backend_->createBuffer(desc);
    if (!buffer_.valid()) {
        return false;
    }
    
    mappedData_ = static_cast<InstanceData*>(backend_->mapBuffer(buffer_));
    return true;
}

void DynamicInstanceBuffer::shutdown() {
    if (backend_) {
        if (buffer_.valid()) {
            backend_->destroyBuffer(buffer_);
        }
    }
    mappedData_ = nullptr;
    backend_ = nullptr;
    capacity_ = 0;
    usedCount_ = 0;
}

u32 DynamicInstanceBuffer::allocate(u32 count) {
    if (usedCount_ + count > capacity_) {
        if (!grow(std::max(capacity_ * 2, usedCount_ + count))) {
            return UINT32_MAX;
        }
    }
    
    u32 offset = usedCount_;
    usedCount_ += count;
    return offset;
}

void DynamicInstanceBuffer::write(u32 offset, const InstanceData* data, u32 count) {
    if (mappedData_ && offset + count <= capacity_) {
        memcpy(mappedData_ + offset, data, count * sizeof(InstanceData));
    }
}

void DynamicInstanceBuffer::reset() {
    usedCount_ = 0;
}

bool DynamicInstanceBuffer::grow(u32 newCapacity) {
    if (newCapacity > maxSize_) {
        newCapacity = maxSize_;
    }
    
    if (newCapacity <= capacity_) {
        return true;
    }
    
    // 创建新缓冲
    BufferDesc desc;
    desc.size = sizeof(InstanceData) * newCapacity;
    desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    desc.mapped = true;
    
    auto newBuffer = backend_->createBuffer(desc);
    if (!newBuffer.valid()) {
        return false;
    }
    
    // 复制旧数据
    auto newMapped = static_cast<InstanceData*>(backend_->mapBuffer(newBuffer));
    if (mappedData_ && newMapped) {
        memcpy(newMapped, mappedData_, usedCount_ * sizeof(InstanceData));
    }
    
    // 销毁旧缓冲
    backend_->destroyBuffer(buffer_);
    
    buffer_ = newBuffer;
    mappedData_ = newMapped;
    capacity_ = newCapacity;
    
    return true;
}

// ============================================================================
// IndirectDrawManager Implementation
// ============================================================================

IndirectDrawManager::IndirectDrawManager() = default;

IndirectDrawManager::~IndirectDrawManager() {
    shutdown();
}

bool IndirectDrawManager::initialize(VulkanBackend* backend, u32 maxDraws) {
    backend_ = backend;
    maxDraws_ = maxDraws;
    
    BufferDesc desc;
    desc.size = sizeof(IndirectDrawIndexedCommand) * maxDraws;
    desc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    desc.mapped = true;
    
    drawCommandBuffer_ = backend_->createBuffer(desc);
    
    // 绘制计数缓冲
    desc.size = sizeof(u32) * 4;
    drawCountBuffer_ = backend_->createBuffer(desc);
    
    drawCommands_.reserve(maxDraws);
    drawIndexedCommands_.reserve(maxDraws);
    
    return true;
}

void IndirectDrawManager::shutdown() {
    if (backend_) {
        if (drawCommandBuffer_.valid()) backend_->destroyBuffer(drawCommandBuffer_);
        if (drawCountBuffer_.valid()) backend_->destroyBuffer(drawCountBuffer_);
    }
    drawCommands_.clear();
    drawIndexedCommands_.clear();
    backend_ = nullptr;
}

u32 IndirectDrawManager::addDrawCommand(const IndirectDrawCommand& cmd) {
    u32 index = static_cast<u32>(drawCommands_.size());
    drawCommands_.push_back(cmd);
    return index;
}

u32 IndirectDrawManager::addDrawIndexedCommand(const IndirectDrawIndexedCommand& cmd) {
    u32 index = static_cast<u32>(drawIndexedCommands_.size());
    drawIndexedCommands_.push_back(cmd);
    return index;
}

void IndirectDrawManager::updateDrawCommand(u32 index, const IndirectDrawCommand& cmd) {
    if (index < drawCommands_.size()) {
        drawCommands_[index] = cmd;
    }
}

void IndirectDrawManager::updateDrawIndexedCommand(u32 index, const IndirectDrawIndexedCommand& cmd) {
    if (index < drawIndexedCommands_.size()) {
        drawIndexedCommands_[index] = cmd;
    }
}

void IndirectDrawManager::executeDraw(VkCommandBuffer cmd, u32 firstDraw, u32 drawCount) {
    if (drawCommands_.empty()) return;
    
    // 更新缓冲
    backend_->updateBuffer(drawCommandBuffer_, drawCommands_.data(),
                           drawCommands_.size() * sizeof(IndirectDrawCommand));
    
    auto* buffer = backend_->buffers_.get(drawCommandBuffer_);
    
    u32 count = drawCount == UINT32_MAX ? 
                static_cast<u32>(drawCommands_.size()) - firstDraw : drawCount;
    
    vkCmdDrawIndirect(cmd, buffer->buffer, 
                      firstDraw * sizeof(IndirectDrawCommand),
                      count, sizeof(IndirectDrawCommand));
}

void IndirectDrawManager::executeDrawIndexed(VkCommandBuffer cmd, u32 firstDraw, u32 drawCount) {
    if (drawIndexedCommands_.empty()) return;
    
    // 更新缓冲
    backend_->updateBuffer(drawCommandBuffer_, drawIndexedCommands_.data(),
                           drawIndexedCommands_.size() * sizeof(IndirectDrawIndexedCommand));
    
    auto* buffer = backend_->buffers_.get(drawCommandBuffer_);
    
    u32 count = drawCount == UINT32_MAX ?
                static_cast<u32>(drawIndexedCommands_.size()) - firstDraw : drawCount;
    
    vkCmdDrawIndexedIndirect(cmd, buffer->buffer,
                             firstDraw * sizeof(IndirectDrawIndexedCommand),
                             count, sizeof(IndirectDrawIndexedCommand));
}

void IndirectDrawManager::reset() {
    drawCommands_.clear();
    drawIndexedCommands_.clear();
}

// ============================================================================
// GPUInstanceCuller Implementation
// ============================================================================

GPUInstanceCuller::GPUInstanceCuller() = default;

GPUInstanceCuller::~GPUInstanceCuller() {
    shutdown();
}

bool GPUInstanceCuller::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    // 创建剔除着色器
    const char* cullShader = R"(
#version 450

layout(local_size_x = 256) in;

struct InstanceData {
    mat4 transform;
    vec4 color;
    vec4 customData;
};

layout(std430, binding = 0) readonly buffer InputInstances {
    InstanceData instances[];
};

layout(std430, binding = 1) writeonly buffer OutputInstances {
    InstanceData outputInstances[];
};

layout(std430, binding = 2) buffer DrawCommands {
    uint drawCount;
    uint padding[3];
};

layout(push_constant) uniform CullingParams {
    vec4 frustumPlanes[6];
    uint instanceCount;
} params;

bool isVisible(vec3 center, float radius) {
    for (int i = 0; i < 6; i++) {
        if (dot(params.frustumPlanes[i].xyz, center) + params.frustumPlanes[i].w < -radius) {
            return false;
        }
    }
    return true;
}

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= params.instanceCount) return;
    
    InstanceData instance = instances[index];
    
    // 计算包围球中心
    vec3 center = instance.transform[3].xyz;
    float radius = length(instance.transform[0].xyz);
    
    if (isVisible(center, radius)) {
        uint outIndex = atomicAdd(drawCount, 1);
        outputInstances[outIndex] = instance;
    }
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = cullShader;
    cullShader_ = backend_->createShader(shaderDesc);
    
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = cullShader_;
    cullPipeline_ = backend_->createPipeline(pipelineDesc);
    
    return true;
}

void GPUInstanceCuller::shutdown() {
    if (backend_) {
        if (cullPipeline_.valid()) backend_->destroyPipeline(cullPipeline_);
        if (cullShader_.valid()) backend_->destroyShader(cullShader_);
        if (indirectBuffer_.valid()) backend_->destroyBuffer(indirectBuffer_);
        if (counterBuffer_.valid()) backend_->destroyBuffer(counterBuffer_);
    }
    backend_ = nullptr;
}

void GPUInstanceCuller::cull(VkCommandBuffer cmd,
                              BufferHandle instanceBuffer,
                              BufferHandle outputBuffer,
                              BufferHandle drawCommandBuffer,
                              u32 instanceCount,
                              const CullingParams& params) {
    backend_->setComputePipeline(cullPipeline_);
    
    // 推送常量
    // backend_->pushConstants(cullShader_, 0, sizeof(params), &params);
    
    u32 groupCount = (instanceCount + 255) / 256;
    backend_->dispatchCompute(groupCount, 1, 1);
}

void GPUInstanceCuller::selectLOD(VkCommandBuffer cmd,
                                   BufferHandle instanceBuffer,
                                   BufferHandle lodBuffer,
                                   u32 instanceCount,
                                   const CullingParams& params) {
    // LOD 选择逻辑
}

bool GPUInstanceCuller::loadCullingShader() {
    return true;
}

// ============================================================================
// ParticleInstancedRenderer Implementation
// ============================================================================

ParticleInstancedRenderer::ParticleInstancedRenderer() = default;

ParticleInstancedRenderer::~ParticleInstancedRenderer() {
    shutdown();
}

bool ParticleInstancedRenderer::initialize(VulkanBackend* backend, u32 maxParticles) {
    backend_ = backend;
    maxParticles_ = maxParticles;
    
    // 创建实例缓冲
    BufferDesc instanceDesc;
    instanceDesc.size = sizeof(ParticleInstance) * maxParticles;
    instanceDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    instanceDesc.mapped = true;
    
    instanceBuffer_ = backend_->createBuffer(instanceDesc);
    
    // 创建单位四边形顶点缓冲
    struct Vertex {
        f32 x, y, u, v;
    };
    
    Vertex quadVertices[] = {
        {-0.5f, -0.5f, 0.0f, 0.0f},
        { 0.5f, -0.5f, 1.0f, 0.0f},
        { 0.5f,  0.5f, 1.0f, 1.0f},
        {-0.5f,  0.5f, 0.0f, 1.0f}
    };
    
    BufferDesc vbDesc;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbDesc.mapped = true;
    
    vertexBuffer_ = backend_->createBuffer(vbDesc);
    backend_->updateBuffer(vertexBuffer_, quadVertices, sizeof(quadVertices));
    
    // 创建索引缓冲
    u16 indices[] = {0, 1, 2, 0, 2, 3};
    
    BufferDesc ibDesc;
    ibDesc.size = sizeof(indices);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibDesc.mapped = true;
    
    indexBuffer_ = backend_->createBuffer(ibDesc);
    backend_->updateBuffer(indexBuffer_, indices, sizeof(indices));
    
    // 创建采样器
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ = backend_->createSampler(samplerDesc);
    
    return true;
}

void ParticleInstancedRenderer::shutdown() {
    if (backend_) {
        if (instanceBuffer_.valid()) backend_->destroyBuffer(instanceBuffer_);
        if (vertexBuffer_.valid()) backend_->destroyBuffer(vertexBuffer_);
        if (indexBuffer_.valid()) backend_->destroyBuffer(indexBuffer_);
        if (pipeline_.valid()) backend_->destroyPipeline(pipeline_);
        if (shader_.valid()) backend_->destroyShader(shader_);
        if (sampler_.valid()) backend_->destroySampler(sampler_);
    }
    backend_ = nullptr;
}

void ParticleInstancedRenderer::setParticles(const std::vector<ParticleInstance>& particles) {
    particleCount_ = std::min(static_cast<u32>(particles.size()), maxParticles_);
    
    if (particleCount_ > 0) {
        backend_->updateBuffer(instanceBuffer_, particles.data(),
                               particleCount_ * sizeof(ParticleInstance));
    }
}

void ParticleInstancedRenderer::render(VkCommandBuffer cmd, TextureHandle particleTexture) {
    if (particleCount_ == 0) return;
    
    // 绑定顶点缓冲
    auto* vb = backend_->buffers_.get(vertexBuffer_);
    auto* ib = backend_->buffers_.get(instanceBuffer_);
    
    VkBuffer vertexBuffers[] = {vb->buffer, ib->buffer};
    VkDeviceSize offsets[] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);
    
    // 绑定索引缓冲
    auto* indexBuf = backend_->buffers_.get(indexBuffer_);
    vkCmdBindIndexBuffer(cmd, indexBuf->buffer, 0, VK_INDEX_TYPE_UINT16);
    
    // 绘制
    vkCmdDrawIndexed(cmd, 6, particleCount_, 0, 0, 0);
}

} // namespace Nova

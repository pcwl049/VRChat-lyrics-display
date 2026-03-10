/**
 * Nova Renderer - Render Graph Implementation
 */

#include "Nova/RenderGraph.h"
#include "Nova/VulkanBackend.h"
#include "Nova/Renderer.h"
#include <algorithm>
#include <iostream>

namespace Nova {

// ============================================================================
// RGPassDesc Methods
// ============================================================================

RGPassDesc& RGPassDesc::read(RGResourceHandle resource, VkPipelineStageFlags stage) {
    RGResourceAccess access;
    access.resource = resource;
    access.usage = ResourceUsage::ShaderRead;
    access.stageMask = stage;
    access.accessMask = VK_ACCESS_SHADER_READ_BIT;
    access.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputs.push_back(access);
    return *this;
}

RGPassDesc& RGPassDesc::write(RGResourceHandle resource, VkPipelineStageFlags stage) {
    RGResourceAccess access;
    access.resource = resource;
    access.usage = ResourceUsage::ColorAttachment;
    access.stageMask = stage;
    access.accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outputs.push_back(access);
    return *this;
}

RGPassDesc& RGPassDesc::readWrite(RGResourceHandle resource) {
    RGResourceAccess access;
    access.resource = resource;
    access.usage = ResourceUsage::Storage;
    access.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    access.accessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_GENERAL;
    inputs.push_back(access);
    outputs.push_back(access);
    return *this;
}

RGPassDesc& RGPassDesc::setColorOutput(RGResourceHandle resource, u32 attachmentIndex) {
    RGResourceAccess access;
    access.resource = resource;
    access.usage = ResourceUsage::ColorAttachment;
    access.stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    access.accessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    outputs.push_back(access);
    return *this;
}

RGPassDesc& RGPassDesc::setDepthOutput(RGResourceHandle resource) {
    RGResourceAccess access;
    access.resource = resource;
    access.usage = ResourceUsage::DepthStencilAttachment;
    access.stageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    access.accessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    access.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    outputs.push_back(access);
    return *this;
}

RGPassDesc& RGPassDesc::setExecute(PassExecuteCallback callback) {
    execute = std::move(callback);
    return *this;
}

// ============================================================================
// RenderGraph Implementation
// ============================================================================

RenderGraph::RenderGraph() = default;

RenderGraph::~RenderGraph() {
    shutdown();
}

bool RenderGraph::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void RenderGraph::shutdown() {
    resources_.clear();
    passes_.clear();
    executionOrder_.clear();
    backend_ = nullptr;
}

void RenderGraph::beginFrame() {
    // 重置 Pass 执行状态
    for (auto& pass : passes_) {
        pass.executed = false;
    }
    
    compiled_ = false;
}

void RenderGraph::endFrame() {
    // 销毁瞬时资源
    destroyTransientResources();
}

RGResourceHandle RenderGraph::createTexture(const RGTextureDesc& desc) {
    RGResourceHandle handle;
    handle.index = static_cast<u32>(resources_.size());
    handle.generation = 0;
    
    Resource resource;
    resource.name = desc.name;
    resource.type = ResourceType::Texture;
    resource.handle = handle;
    resource.textureDesc = desc;
    resource.transient = desc.transient;
    
    resources_.push_back(std::move(resource));
    return handle;
}

RGResourceHandle RenderGraph::createBuffer(const RGBufferDesc& desc) {
    RGResourceHandle handle;
    handle.index = static_cast<u32>(resources_.size());
    handle.generation = 0;
    
    Resource resource;
    resource.name = desc.name;
    resource.type = ResourceType::Buffer;
    resource.handle = handle;
    resource.bufferDesc = desc;
    resource.transient = desc.transient;
    
    resources_.push_back(std::move(resource));
    return handle;
}

RGResourceHandle RenderGraph::importTexture(TextureHandle texture, const std::string& name) {
    RGResourceHandle handle;
    handle.index = static_cast<u32>(resources_.size());
    handle.generation = 0;
    
    Resource resource;
    resource.name = name;
    resource.type = ResourceType::Texture;
    resource.handle = handle;
    resource.texture = texture;
    resource.imported = true;
    
    resources_.push_back(std::move(resource));
    return handle;
}

RGResourceHandle RenderGraph::importBuffer(BufferHandle buffer, const std::string& name) {
    RGResourceHandle handle;
    handle.index = static_cast<u32>(resources_.size());
    handle.generation = 0;
    
    Resource resource;
    resource.name = name;
    resource.type = ResourceType::Buffer;
    resource.handle = handle;
    resource.buffer = buffer;
    resource.imported = true;
    
    resources_.push_back(std::move(resource));
    return handle;
}

RGPassDesc& RenderGraph::addPass(const std::string& name, RGPassDesc::Type type) {
    Pass pass;
    pass.name = name;
    pass.type = type;
    pass.index = static_cast<u32>(passes_.size());
    
    passes_.push_back(std::move(pass));
    
    return passes_.back().desc;
}

TextureHandle RenderGraph::getTexture(RGResourceHandle handle) const {
    if (handle.index >= resources_.size()) return {};
    return resources_[handle.index].texture;
}

BufferHandle RenderGraph::getBuffer(RGResourceHandle handle) const {
    if (handle.index >= resources_.size()) return {};
    return resources_[handle.index].buffer;
}

void RenderGraph::compile() {
    // 清除旧的编译数据
    for (auto& pass : passes_) {
        pass.dependencies.clear();
        pass.dependents.clear();
        pass.reads.clear();
        pass.writes.clear();
    }
    
    // 计算依赖关系
    calculateDependencies();
    
    // 计算资源生命周期
    calculateResourceLifetimes();
    
    // 创建瞬时资源
    createTransientResources();
    
    // 计算执行顺序
    executionOrder_ = topologicalSort();
    
    compiled_ = true;
}

void RenderGraph::calculateDependencies() {
    // 跟踪每个资源的最后写入 Pass
    std::unordered_map<u32, u32> lastWritePass;
    
    for (auto& pass : passes_) {
        // 收集读取的资源
        for (const auto& access : pass.desc.inputs) {
            pass.reads.push_back(access);
            
            u32 resIdx = access.resource.index;
            
            // 更新资源生命周期
            resources_[resIdx].firstUsePass = std::min(resources_[resIdx].firstUsePass, pass.index);
            resources_[resIdx].lastUsePass = std::max(resources_[resIdx].lastUsePass, pass.index);
            
            // 如果之前有写入，添加依赖
            auto it = lastWritePass.find(resIdx);
            if (it != lastWritePass.end() && it->second != pass.index) {
                pass.dependencies.push_back(it->second);
                passes_[it->second].dependents.push_back(pass.index);
            }
        }
        
        // 收集写入的资源
        for (const auto& access : pass.desc.outputs) {
            pass.writes.push_back(access);
            
            u32 resIdx = access.resource.index;
            
            // 更新资源生命周期
            resources_[resIdx].firstUsePass = std::min(resources_[resIdx].firstUsePass, pass.index);
            resources_[resIdx].lastUsePass = std::max(resources_[resIdx].lastUsePass, pass.index);
            
            // 更新最后写入 Pass
            lastWritePass[resIdx] = pass.index;
        }
    }
}

void RenderGraph::calculateResourceLifetimes() {
    // 已经在上一步计算了 firstUsePass 和 lastUsePass
}

void RenderGraph::createTransientResources() {
    for (auto& resource : resources_) {
        if (resource.transient && !resource.imported) {
            if (resource.type == ResourceType::Texture) {
                TextureDesc desc;
                desc.width = resource.textureDesc.width;
                desc.height = resource.textureDesc.height;
                desc.format = static_cast<Format>(resource.textureDesc.format);
                desc.usage = resource.textureDesc.usage;
                
                resource.texture = backend_->createTexture(desc);
            } else if (resource.type == ResourceType::Buffer) {
                BufferDesc desc;
                desc.size = resource.bufferDesc.size;
                desc.usage = resource.bufferDesc.usage;
                
                resource.buffer = backend_->createBuffer(desc);
            }
        }
    }
}

void RenderGraph::destroyTransientResources() {
    for (auto& resource : resources_) {
        if (resource.transient && !resource.imported) {
            if (resource.texture.valid()) {
                backend_->destroyTexture(resource.texture);
                resource.texture = {};
            }
            if (resource.buffer.valid()) {
                backend_->destroyBuffer(resource.buffer);
                resource.buffer = {};
            }
        }
    }
}

void RenderGraph::execute(VkCommandBuffer cmd, const FrameContext& ctx) {
    if (!compiled_) {
        compile();
    }
    
    for (u32 passIdx : executionOrder_) {
        auto& pass = passes_[passIdx];
        
        // 插入资源屏障
        insertBarriers(cmd, pass);
        
        // 执行 Pass
        if (pass.desc.execute) {
            pass.desc.execute(cmd, ctx);
        }
        
        pass.executed = true;
    }
}

void RenderGraph::insertBarriers(VkCommandBuffer cmd, const Pass& pass) {
    std::vector<VkImageMemoryBarrier> imageBarriers;
    std::vector<VkBufferMemoryBarrier> bufferBarriers;
    
    // 处理输入资源 (需要等待之前的写入完成)
    for (const auto& access : pass.reads) {
        auto& resource = resources_[access.resource.index];
        if (resource.type != ResourceType::Texture) continue;
        
        if (resource.currentLayout != access.layout) {
            insertImageBarrier(cmd, resource, access.layout, 
                              access.stageMask, access.accessMask);
        }
    }
    
    // 处理输出资源
    for (const auto& access : pass.writes) {
        auto& resource = resources_[access.resource.index];
        if (resource.type != ResourceType::Texture) continue;
        
        if (resource.currentLayout != access.layout) {
            insertImageBarrier(cmd, resource, access.layout,
                              access.stageMask, access.accessMask);
        }
    }
}

void RenderGraph::insertImageBarrier(VkCommandBuffer cmd, Resource& resource,
                                      VkImageLayout newLayout,
                                      VkPipelineStageFlags dstStage,
                                      VkAccessFlags dstAccess) {
    auto* tex = backend_->textures_.get(resource.texture);
    if (!tex) return;
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = resource.currentLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = tex->mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = tex->arrayLayers;
    barrier.srcAccessMask = resource.lastAccess;
    barrier.dstAccessMask = dstAccess;
    
    VkPipelineStageFlags srcStage = resource.lastStage;
    if (srcStage == 0) {
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    
    resource.currentLayout = newLayout;
    resource.lastStage = dstStage;
    resource.lastAccess = dstAccess;
}

std::vector<u32> RenderGraph::topologicalSort() {
    std::vector<u32> order;
    order.reserve(passes_.size());
    
    // Kahn's algorithm
    std::vector<u32> inDegree(passes_.size(), 0);
    
    for (const auto& pass : passes_) {
        inDegree[pass.index] = static_cast<u32>(pass.dependencies.size());
    }
    
    std::queue<u32> queue;
    for (const auto& pass : passes_) {
        if (pass.dependencies.empty()) {
            queue.push(pass.index);
        }
    }
    
    while (!queue.empty()) {
        u32 idx = queue.front();
        queue.pop();
        
        order.push_back(idx);
        
        for (u32 dep : passes_[idx].dependents) {
            inDegree[dep]--;
            if (inDegree[dep] == 0) {
                queue.push(dep);
            }
        }
    }
    
    return order;
}

void RenderGraph::setBackbuffer(RGResourceHandle handle) {
    backbuffer_ = handle;
}

void RenderGraph::printGraph() const {
    std::cout << "=== Render Graph ===" << std::endl;
    std::cout << "Resources:" << std::endl;
    for (const auto& res : resources_) {
        std::cout << "  " << res.name << " (transient=" << res.transient << ")" << std::endl;
    }
    std::cout << "Passes:" << std::endl;
    for (const auto& pass : passes_) {
        std::cout << "  " << pass.name << std::endl;
        if (!pass.dependencies.empty()) {
            std::cout << "    Depends on: ";
            for (u32 dep : pass.dependencies) {
                std::cout << passes_[dep].name << " ";
            }
            std::cout << std::endl;
        }
    }
    std::cout << "Execution Order: ";
    for (u32 idx : executionOrder_) {
        std::cout << passes_[idx].name << " -> ";
    }
    std::cout << "END" << std::endl;
}

// ============================================================================
// RenderGraphBuilder Implementation
// ============================================================================

RenderGraphBuilder::RenderGraphBuilder(RenderGraph& graph) : graph_(graph) {}

RGPassDesc& RenderGraphBuilder::addGraphicsPass(const std::string& name) {
    return graph_.addPass(name, RGPassDesc::Type::Graphics);
}

RGPassDesc& RenderGraphBuilder::addComputePass(const std::string& name) {
    return graph_.addPass(name, RGPassDesc::Type::Compute);
}

RGPassDesc& RenderGraphBuilder::addTransferPass(const std::string& name) {
    return graph_.addPass(name, RGPassDesc::Type::Transfer);
}

RGResourceHandle RenderGraphBuilder::createColorAttachment(const std::string& name, 
                                                            u32 width, u32 height, 
                                                            VkFormat format) {
    RGTextureDesc desc;
    desc.name = name;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.transient = true;
    
    return graph_.createTexture(desc);
}

RGResourceHandle RenderGraphBuilder::createDepthAttachment(const std::string& name,
                                                            u32 width, u32 height) {
    RGTextureDesc desc;
    desc.name = name;
    desc.width = width;
    desc.height = height;
    desc.format = VK_FORMAT_D32_SFLOAT;
    desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    desc.transient = true;
    
    return graph_.createTexture(desc);
}

RGResourceHandle RenderGraphBuilder::createStorageTexture(const std::string& name,
                                                           u32 width, u32 height,
                                                           VkFormat format) {
    RGTextureDesc desc;
    desc.name = name;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.transient = true;
    
    return graph_.createTexture(desc);
}

// ============================================================================
// Pass Templates
// ============================================================================

namespace PassTemplates {

RGPassDesc createBlurPass(RenderGraph& graph, 
                           RGResourceHandle input,
                           RGResourceHandle output,
                           f32 blurRadius) {
    return graph.addPass("Blur", RGPassDesc::Type::Compute)
        .read(input, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write(output, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .setExecute([blurRadius](VkCommandBuffer cmd, const FrameContext& ctx) {
            // 模糊计算着色器执行
            // 实际需要绑定着色器和参数
        });
}

RGPassDesc createBloomPass(RenderGraph& graph,
                            RGResourceHandle input,
                            RGResourceHandle output,
                            f32 threshold,
                            f32 intensity) {
    return graph.addPass("Bloom", RGPassDesc::Type::Compute)
        .read(input, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write(output, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .setExecute([threshold, intensity](VkCommandBuffer cmd, const FrameContext& ctx) {
            // Bloom 计算着色器执行
        });
}

RGPassDesc createTonemapPass(RenderGraph& graph,
                              RGResourceHandle input,
                              RGResourceHandle output,
                              const std::string& method) {
    return graph.addPass("Tonemap", RGPassDesc::Type::Compute)
        .read(input, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .write(output, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
        .setExecute([method](VkCommandBuffer cmd, const FrameContext& ctx) {
            // 色调映射计算着色器执行
        });
}

RGPassDesc createUIPass(RenderGraph& graph,
                         RGResourceHandle colorTarget,
                         RGResourceHandle depthTarget) {
    return graph.addPass("UI", RGPassDesc::Type::Graphics)
        .setColorOutput(colorTarget)
        .setDepthOutput(depthTarget)
        .setExecute([](VkCommandBuffer cmd, const FrameContext& ctx) {
            // UI 渲染
        });
}

} // namespace PassTemplates

} // namespace Nova

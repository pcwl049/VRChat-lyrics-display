/**
 * Nova Renderer - Descriptor System
 * 描述符系统
 */

#pragma once

#include "Types.h"
#include "Shader.h"
#include <vulkan/vulkan.h>

// VMA 前向声明
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

#include <vector>
#include <unordered_map>

namespace Nova {

// 描述符绑定信息
struct DescriptorBindingInfo {
    u32 set;
    u32 binding;
    DescriptorType type;
    
    bool operator==(const DescriptorBindingInfo& o) const {
        return set == o.set && binding == o.binding && type == o.type;
    }
};

// 描述符写入
struct DescriptorWrite {
    u32 set;
    u32 binding;
    DescriptorType type;
    
    // 资源
    BufferHandle buffer;
    TextureHandle texture;
    SamplerHandle sampler;
    
    // 数组偏移和数量
    u32 arrayElement = 0;
    u32 count = 1;
    
    // 范围 (对于动态缓冲区)
    u64 offset = 0;
    u64 range = 0;
};

// 描述符集构建器
class DescriptorSetBuilder {
public:
    DescriptorSetBuilder& bindBuffer(u32 set, u32 binding, BufferHandle buffer, 
                                      DescriptorType type = DescriptorType::UniformBuffer) {
        DescriptorWrite write;
        write.set = set;
        write.binding = binding;
        write.type = type;
        write.buffer = buffer;
        writes_.push_back(write);
        return *this;
    }
    
    DescriptorSetBuilder& bindTexture(u32 set, u32 binding, TextureHandle texture,
                                       DescriptorType type = DescriptorType::SampledImage) {
        DescriptorWrite write;
        write.set = set;
        write.binding = binding;
        write.type = type;
        write.texture = texture;
        writes_.push_back(write);
        return *this;
    }
    
    DescriptorSetBuilder& bindSampler(u32 set, u32 binding, SamplerHandle sampler) {
        DescriptorWrite write;
        write.set = set;
        write.binding = binding;
        write.type = DescriptorType::Sampler;
        write.sampler = sampler;
        writes_.push_back(write);
        return *this;
    }
    
    DescriptorSetBuilder& bindCombinedImageSampler(u32 set, u32 binding, 
                                                    TextureHandle texture, SamplerHandle sampler) {
        DescriptorWrite write;
        write.set = set;
        write.binding = binding;
        write.type = DescriptorType::CombinedImageSampler;
        write.texture = texture;
        write.sampler = sampler;
        writes_.push_back(write);
        return *this;
    }
    
    DescriptorSetBuilder& bindStorageBuffer(u32 set, u32 binding, BufferHandle buffer) {
        return bindBuffer(set, binding, buffer, DescriptorType::StorageBuffer);
    }
    
    DescriptorSetBuilder& bindStorageImage(u32 set, u32 binding, TextureHandle texture) {
        return bindTexture(set, binding, texture, DescriptorType::StorageImage);
    }
    
    const std::vector<DescriptorWrite>& getWrites() const { return writes_; }
    void clear() { writes_.clear(); }
    
private:
    std::vector<DescriptorWrite> writes_;
};

// 描述符集布局缓存
class DescriptorSetLayoutCache {
public:
    DescriptorSetLayoutCache();
    ~DescriptorSetLayoutCache();
    
    bool initialize(VkDevice device);
    void shutdown();
    
    // 获取或创建布局
    VkDescriptorSetLayout getOrCreate(const std::vector<DescriptorBinding>& bindings);
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    
    struct LayoutKey {
        std::vector<DescriptorBinding> bindings;
        
        bool operator==(const LayoutKey& o) const {
            return bindings == o.bindings;
        }
    };
    
    struct LayoutKeyHash {
        size_t operator()(const LayoutKey& k) const {
            size_t h = 0;
            for (const auto& b : k.bindings) {
                h ^= std::hash<u32>{}(b.binding) + 0x9e3779b9 + (h << 6) + (h >> 2);
                h ^= std::hash<u32>{}(static_cast<u32>(b.type)) + 0x9e3779b9 + (h << 6) + (h >> 2);
            }
            return h;
        }
    };
    
    std::unordered_map<LayoutKey, VkDescriptorSetLayout, LayoutKeyHash> layouts_;
};

// 描述符分配器
class DescriptorAllocator {
public:
    DescriptorAllocator();
    ~DescriptorAllocator();
    
    bool initialize(VkDevice device, u32 maxSets = 1000);
    void shutdown();
    
    // 分配描述符集
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    void free(VkDescriptorSet set);
    
    // 重置池
    void reset();
    
    // 更新描述符集
    void update(VkDescriptorSet set, const std::vector<DescriptorWrite>& writes);
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    u32 maxSets_ = 0;
    u32 allocatedSets_ = 0;
};

// 帧描述符管理器
class FrameDescriptorManager {
public:
    FrameDescriptorManager();
    ~FrameDescriptorManager();
    
    bool initialize(VkDevice device, u32 framesInFlight, u32 maxSetsPerFrame = 100);
    void shutdown();
    
    // 每帧调用
    void beginFrame(u32 frameIndex);
    
    // 分配 (使用当前帧的池)
    VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    
    // 重置所有池
    void resetAll();
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> framePools_;
    u32 currentFrame_ = 0;
    u32 maxSetsPerFrame_ = 100;
};

// Uniform 缓冲区管理
template<typename T>
class UniformBuffer {
public:
    UniformBuffer() = default;
    
    bool initialize(VkDevice device, VmaAllocator allocator, u32 framesInFlight) {
        device_ = device;
        allocator_ = allocator;
        framesInFlight_ = framesInFlight;
        
        buffers_.resize(framesInFlight);
        allocations_.resize(framesInFlight);
        mapped_.resize(framesInFlight);
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(T);
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        
        for (u32 i = 0; i < framesInFlight; i++) {
            VmaAllocationInfo info;
            if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                               &buffers_[i], &allocations_[i], &info) != VK_SUCCESS) {
                return false;
            }
            mapped_[i] = info.pMappedData;
        }
        
        return true;
    }
    
    void shutdown() {
        for (u32 i = 0; i < buffers_.size(); i++) {
            if (buffers_[i]) {
                vmaDestroyBuffer(allocator_, buffers_[i], allocations_[i]);
            }
        }
    }
    
    void update(u32 frameIndex, const T& data) {
        if (frameIndex < mapped_.size() && mapped_[frameIndex]) {
            memcpy(mapped_[frameIndex], &data, sizeof(T));
        }
    }
    
    VkBuffer getBuffer(u32 frameIndex) const {
        return frameIndex < buffers_.size() ? buffers_[frameIndex] : VK_NULL_HANDLE;
    }
    
    VkDescriptorBufferInfo getDescriptorInfo(u32 frameIndex) const {
        VkDescriptorBufferInfo info{};
        info.buffer = getBuffer(frameIndex);
        info.offset = 0;
        info.range = sizeof(T);
        return info;
    }
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    u32 framesInFlight_ = 2;
    
    std::vector<VkBuffer> buffers_;
    std::vector<VmaAllocation> allocations_;
    std::vector<void*> mapped_;
};

} // namespace Nova

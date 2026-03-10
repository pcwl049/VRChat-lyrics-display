/**
 * Nova Renderer - Descriptor System Implementation
 */

#include "Nova/Descriptor.h"
#include "Nova/VulkanBackend.h"

#include <algorithm>

namespace Nova {

// ============================================================================
// DescriptorSetLayoutCache Implementation
// ============================================================================

DescriptorSetLayoutCache::DescriptorSetLayoutCache() = default;
DescriptorSetLayoutCache::~DescriptorSetLayoutCache() {
    shutdown();
}

bool DescriptorSetLayoutCache::initialize(VkDevice device) {
    device_ = device;
    return true;
}

void DescriptorSetLayoutCache::shutdown() {
    for (auto& [key, layout] : layouts_) {
        vkDestroyDescriptorSetLayout(device_, layout, nullptr);
    }
    layouts_.clear();
    device_ = VK_NULL_HANDLE;
}

VkDescriptorSetLayout DescriptorSetLayoutCache::getOrCreate(const std::vector<DescriptorBinding>& bindings) {
    LayoutKey key;
    key.bindings = bindings;
    
    auto it = layouts_.find(key);
    if (it != layouts_.end()) {
        return it->second;
    }
    
    // 转换为 Vulkan 描述符绑定
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(bindings.size());
    
    for (const auto& b : bindings) {
        VkDescriptorSetLayoutBinding vkBinding{};
        vkBinding.binding = b.binding;
        vkBinding.descriptorType = static_cast<VkDescriptorType>(b.type);
        vkBinding.descriptorCount = b.count;
        vkBinding.stageFlags = static_cast<VkShaderStageFlags>(b.stage);
        vkBinding.pImmutableSamplers = nullptr;
        vkBindings.push_back(vkBinding);
    }
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();
    
    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    layouts_[key] = layout;
    return layout;
}

// ============================================================================
// DescriptorAllocator Implementation
// ============================================================================

DescriptorAllocator::DescriptorAllocator() = default;
DescriptorAllocator::~DescriptorAllocator() {
    shutdown();
}

bool DescriptorAllocator::initialize(VkDevice device, u32 maxSets) {
    device_ = device;
    maxSets_ = maxSets;
    
    // 池大小配置
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets * 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets * 2},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSets * 4},
        {VK_DESCRIPTOR_TYPE_SAMPLER, maxSets * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSets * 8},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSets * 2}
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    return vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool_) == VK_SUCCESS;
}

void DescriptorAllocator::shutdown() {
    if (pool_) {
        vkDestroyDescriptorPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

VkDescriptorSet DescriptorAllocator::allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool_;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    allocatedSets_++;
    return set;
}

void DescriptorAllocator::free(VkDescriptorSet set) {
    vkFreeDescriptorSets(device_, pool_, 1, &set);
    allocatedSets_--;
}

void DescriptorAllocator::reset() {
    vkResetDescriptorPool(device_, pool_, 0);
    allocatedSets_ = 0;
}

void DescriptorAllocator::update(VkDescriptorSet set, const std::vector<DescriptorWrite>& writes) {
    std::vector<VkWriteDescriptorSet> vkWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    bufferInfos.reserve(writes.size());
    imageInfos.reserve(writes.size());
    
    // 需要从 VulkanBackend 获取实际资源，这里简化处理
    for (const auto& w : writes) {
        VkWriteDescriptorSet vkWrite{};
        vkWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vkWrite.dstSet = set;
        vkWrite.dstBinding = w.binding;
        vkWrite.dstArrayElement = w.arrayElement;
        vkWrite.descriptorCount = w.count;
        vkWrite.descriptorType = static_cast<VkDescriptorType>(w.type);
        
        // 这里需要实际的资源句柄转换
        // 简化版，实际需要从 VulkanBackend 获取
        
        vkWrites.push_back(vkWrite);
    }
    
    if (!vkWrites.empty()) {
        vkUpdateDescriptorSets(device_, static_cast<u32>(vkWrites.size()), 
                               vkWrites.data(), 0, nullptr);
    }
}

// ============================================================================
// FrameDescriptorManager Implementation
// ============================================================================

FrameDescriptorManager::FrameDescriptorManager() = default;
FrameDescriptorManager::~FrameDescriptorManager() {
    shutdown();
}

bool FrameDescriptorManager::initialize(VkDevice device, u32 framesInFlight, u32 maxSetsPerFrame) {
    device_ = device;
    maxSetsPerFrame_ = maxSetsPerFrame;
    
    framePools_.resize(framesInFlight);
    
    // 池大小配置
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSetsPerFrame * 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSetsPerFrame * 2},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSetsPerFrame * 4},
        {VK_DESCRIPTOR_TYPE_SAMPLER, maxSetsPerFrame * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxSetsPerFrame * 8},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxSetsPerFrame * 2}
    };
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = maxSetsPerFrame;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    
    for (u32 i = 0; i < framesInFlight; i++) {
        if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &framePools_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

void FrameDescriptorManager::shutdown() {
    for (auto pool : framePools_) {
        if (pool) {
            vkDestroyDescriptorPool(device_, pool, nullptr);
        }
    }
    framePools_.clear();
    device_ = VK_NULL_HANDLE;
}

void FrameDescriptorManager::beginFrame(u32 frameIndex) {
    currentFrame_ = frameIndex % framePools_.size();
}

VkDescriptorSet FrameDescriptorManager::allocate(VkDescriptorSetLayout layout) {
    if (currentFrame_ >= framePools_.size()) return VK_NULL_HANDLE;
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = framePools_[currentFrame_];
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    
    return set;
}

void FrameDescriptorManager::resetAll() {
    for (auto pool : framePools_) {
        if (pool) {
            vkResetDescriptorPool(device_, pool, 0);
        }
    }
}

} // namespace Nova

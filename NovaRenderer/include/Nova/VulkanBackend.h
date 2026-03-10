/**
 * Nova Renderer - Vulkan Backend
 * Vulkan 渲染后端
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>

// VMA 内存分配器
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

namespace Nova {

// Vulkan 后端配置
struct VulkanConfig {
    bool enableValidation = true;
    bool enableDebugUtils = true;
    bool preferDiscreteGPU = true;
    u32 maxFramesInFlight = 2;
    u32 desiredFramebufferCount = 3;
    bool vSync = true;
    const char* applicationName = "Nova App";
    u32 applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    void* windowHandle = nullptr;
};

// 队列族索引
struct QueueFamilyIndices {
    u32 graphics = UINT32_MAX;
    u32 present = UINT32_MAX;
    u32 compute = UINT32_MAX;
    u32 transfer = UINT32_MAX;
    
    bool isComplete() const {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }
    
    bool hasCompute() const { return compute != UINT32_MAX; }
    bool hasTransfer() const { return transfer != UINT32_MAX; }
};

// 交换链详情
struct SwapChainDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

// 帧数据
struct FrameData {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    
    // Uniform 缓冲区
    std::vector<VkBuffer> uniformBuffers;
    std::vector<VmaAllocation> uniformAllocations;
    void* uniformMapped = nullptr;
    
    bool initialized = false;
};

// Vulkan 资源池 (使用 Handle 和 generation)
template<typename T, typename Tag>
class VulkanResourcePool {
public:
    static constexpr u32 MAX_RESOURCES = 4096;
    
    VulkanResourcePool() {
        resources_.resize(MAX_RESOURCES);
        generations_.resize(MAX_RESOURCES, 0);
        freeList_.reserve(MAX_RESOURCES);
        for (u32 i = MAX_RESOURCES; i > 0; i--) {
            freeList_.push_back(i);
        }
    }
    
    Handle<Tag> allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freeList_.empty()) {
            return {};  // 池已满
        }
        
        u32 index = freeList_.back();
        freeList_.pop_back();
        u32 generation = generations_[index];
        
        return Handle<Tag>(index, generation);
    }
    
    void deallocate(Handle<Tag> handle) {
        if (!handle.valid()) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        generations_[handle.index]++;
        resources_[handle.index] = T{};
        freeList_.push_back(handle.index);
    }
    
    T* get(Handle<Tag> handle) {
        if (!handle.valid()) return nullptr;
        if (handle.generation != generations_[handle.index]) return nullptr;
        return &resources_[handle.index];
    }
    
    const T* get(Handle<Tag> handle) const {
        if (!handle.valid()) return nullptr;
        if (handle.generation != generations_[handle.index]) return nullptr;
        return &resources_[handle.index];
    }
    
    bool set(Handle<Tag> handle, const T& resource) {
        if (!handle.valid()) return false;
        if (handle.generation != generations_[handle.index]) return false;
        resources_[handle.index] = resource;
        return true;
    }
    
private:
    std::vector<T> resources_;
    std::vector<u32> generations_;
    std::vector<u32> freeList_;
    mutable std::mutex mutex_;
};

// Vulkan 资源结构
struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    u64 size = 0;
    u32 usage = 0;
};

struct VulkanTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VulkanShader {
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    VkShaderModule compute = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
};

struct VulkanPipeline {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool isCompute = false;
};

struct VulkanRenderPass {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkSubpassDescription> subpasses;
};

struct VulkanFramebuffer {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    u32 width = 0;
    u32 height = 0;
    std::vector<TextureHandle> attachments;
};

// 前向声明
class RenderGraph;
class InstancingManager;
class InstanceBatch;
class DynamicInstanceBuffer;
class IndirectDrawManager;
class GPUInstanceCuller;

// Vulkan 后端
class VulkanBackend {
    friend class RenderGraph;
    friend class InstancingManager;
    friend class InstanceBatch;
    friend class DynamicInstanceBuffer;
    friend class IndirectDrawManager;
    friend class GPUInstanceCuller;
    friend class ParticleInstancedRenderer;
public:
    VulkanBackend();
    ~VulkanBackend();
    
    // 初始化/清理
    Result<void> initialize(const VulkanConfig& config);
    void shutdown();
    bool isInitialized() const { return initialized_; }
    
    // 帧管理
    Result<u32> beginFrame();
    void endFrame();
    void submit();
    void present();
    
    // 窗口
    void onResize(u32 width, u32 height);
    void setVSync(bool enabled);
    bool getVSync() const { return config_.vSync; }
    
    // 资源创建
    BufferHandle createBuffer(const BufferDesc& desc);
    TextureHandle createTexture(const TextureDesc& desc);
    SamplerHandle createSampler(const SamplerDesc& desc);
    ShaderHandle createShader(const ShaderDesc& desc);
    PipelineHandle createPipeline(const PipelineDesc& desc);
    RenderPassHandle createRenderPass(const VulkanRenderPass& desc);
    FramebufferHandle createFramebuffer(const std::vector<TextureHandle>& attachments, u32 width, u32 height);
    
    // 资源销毁
    void destroyBuffer(BufferHandle handle);
    void destroyTexture(TextureHandle handle);
    void destroySampler(SamplerHandle handle);
    void destroyShader(ShaderHandle handle);
    void destroyPipeline(PipelineHandle handle);
    void destroyRenderPass(RenderPassHandle handle);
    void destroyFramebuffer(FramebufferHandle handle);
    
    // 资源更新
    void updateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset = 0);
    void updateTexture(TextureHandle handle, const void* data, u32 width, u32 height, u32 mipLevel = 0);
    void uploadTextureData(TextureHandle handle, const void* data, size_t size);
    
    // 映射
    void* mapBuffer(BufferHandle handle);
    void unmapBuffer(BufferHandle handle);
    
    // 绘制命令
    void beginRenderPass(RenderPassHandle pass, FramebufferHandle fb, const Vec4& clearColor);
    void endRenderPass();
    void setPipeline(PipelineHandle pipeline);
    void setViewport(f32 x, f32 y, f32 width, f32 height);
    void setScissor(i32 x, i32 y, u32 width, u32 height);
    void setVertexBuffer(BufferHandle buffer, u32 binding, u64 offset = 0);
    void setIndexBuffer(BufferHandle buffer, u64 offset = 0);
    void setDescriptorSet(DescriptorSetHandle set, u32 setIndex);
    void pushConstants(ShaderHandle shader, u32 offset, u32 size, const void* data);
    void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
    void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0);
    void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ);
    
    // 计算着色器
    void beginCompute();
    void endCompute();
    void setComputePipeline(PipelineHandle pipeline);
    void dispatchCompute(u32 groupCountX, u32 groupCountY, u32 groupCountZ);
    
    // 图像屏障
    void imageBarrier(TextureHandle texture, VkImageLayout newLayout, 
                      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess);
    
    // 复制
    void copyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0);
    void copyBufferToTexture(BufferHandle src, TextureHandle dst, u32 width, u32 height);
    void copyTextureToBuffer(TextureHandle src, BufferHandle dst, u32 width, u32 height);
    void blitTexture(TextureHandle src, TextureHandle dst, VkFilter filter);
    
    // 查询
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, 
                                  VkImageTiling tiling, VkFormatFeatureFlags features);
    u32 findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties);
    
    // GLSL 编译
    bool compileGLSLToSPIRV(const char* source, VkShaderStageFlagBits stage,
                            std::vector<u32>& spirv, std::string& error);
    
    // 获取原生对象
    VkInstance getInstance() const { return instance_; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice_; }
    VkDevice getDevice() const { return device_; }
    VkQueue getGraphicsQueue() const { return graphicsQueue_; }
    VkQueue getPresentQueue() const { return presentQueue_; }
    VkQueue getComputeQueue() const { return computeQueue_; }
    VkQueue getTransferQueue() const { return transferQueue_; }
    VkCommandBuffer getCurrentCommandBuffer() const;
    u32 getCurrentFrameIndex() const { return currentFrame_; }
    u32 getCurrentImageIndex() const { return currentImageIndex_; }
    
    // 信息
    const QueueFamilyIndices& getQueueFamilies() const { return queueFamilies_; }
    const SwapChainDetails& getSwapChainDetails() const { return swapChainDetails_; }
    VkPhysicalDeviceProperties getDeviceProperties() const { return deviceProperties_; }
    VkPhysicalDeviceFeatures getDeviceFeatures() const { return deviceFeatures_; }
    
private:
    // 初始化步骤
    bool createInstance();
    bool setupDebugMessenger();
    bool createSurface();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createVmaAllocator();
    bool createSwapChain();
    bool createImageViews();
    bool createRenderPass();
    bool createFramebuffers();
    bool createCommandPools();
    bool createCommandBuffers();
    bool createSyncObjects();
    bool createDescriptorPool();
    
    // 辅助函数
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainDetails querySwapChainDetails(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat();
    VkPresentModeKHR chooseSwapPresentMode();
    VkExtent2D chooseSwapExtent();
    
    // 重建交换链
    void cleanupSwapChain();
    void recreateSwapChain();
    
    // 成员变量
    bool initialized_ = false;
    VulkanConfig config_;
    
    // Vulkan 核心对象
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    
    // 队列
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilies_;
    
    // 内存分配器
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    // 交换链
    VkSwapchainKHR swapChain_ = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages_;
    std::vector<VkImageView> swapChainImageViews_;
    VkFormat swapChainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapChainExtent_ = {0, 0};
    SwapChainDetails swapChainDetails_;
    
    // 渲染过程
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    
    // 帧数据
    std::vector<FrameData> frames_;
    u32 currentFrame_ = 0;
    u32 currentImageIndex_ = 0;
    u32 framesInFlight_ = 2;
    
    // 资源池
    VulkanResourcePool<VulkanBuffer, BufferTag> buffers_;
    VulkanResourcePool<VulkanTexture, TextureTag> textures_;
    VulkanResourcePool<VulkanSampler, SamplerTag> samplers_;
    VulkanResourcePool<VulkanShader, ShaderTag> shaders_;
    VulkanResourcePool<VulkanPipeline, PipelineTag> pipelines_;
    VulkanResourcePool<VulkanRenderPass, RenderPassTag> renderPasses_;
    VulkanResourcePool<VulkanFramebuffer, FramebufferTag> framebuffers2_;
    
    // 描述符池
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    
    // 设备属性
    VkPhysicalDeviceProperties deviceProperties_{};
    VkPhysicalDeviceFeatures deviceFeatures_{};
    
    // 调试层
    std::vector<const char*> validationLayers_ = {
        "VK_LAYER_KHRONOS_validation"
    };
    
    std::vector<const char*> deviceExtensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
};

} // namespace Nova

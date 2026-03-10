/**
 * Nova Renderer - Command System
 * 命令缓冲系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

namespace Nova {

// 命令缓冲级别
enum class CommandBufferLevel {
    Primary,
    Secondary
};

// 命令缓冲使用标志
enum class CommandBufferUsage : u32 {
    OneTimeSubmit = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    RenderPassContinue = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
    SimultaneousUse = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
};

// 命令缓冲
class CommandBuffer {
public:
    CommandBuffer() = default;
    CommandBuffer(VkCommandBuffer cmd, VkCommandPool pool, VkDevice device);
    
    // 开始/结束录制
    void begin(u32 flags = 0);
    void end();
    
    // 渲染过程
    void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                         u32 width, u32 height, const Color& clearColor);
    void beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                         u32 width, u32 height, const std::vector<Color>& clearColors);
    void endRenderPass();
    
    // 绑定
    void bindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint);
    void bindPipeline(PipelineHandle pipeline);
    void bindComputePipeline(PipelineHandle pipeline);
    void bindDescriptorSets(VkPipelineLayout layout, u32 firstSet,
                           const std::vector<VkDescriptorSet>& sets,
                           VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS);
    void bindDescriptorSet(PipelineHandle pipeline, u32 set, VkDescriptorSet descriptorSet);
    void bindVertexBuffer(u32 binding, VkBuffer buffer, u64 offset = 0);
    void bindVertexBuffer(u32 binding, BufferHandle buffer, u64 offset = 0);
    void bindIndexBuffer(VkBuffer buffer, u64 offset = 0, VkIndexType indexType = VK_INDEX_TYPE_UINT16);
    void bindIndexBuffer(BufferHandle buffer, u64 offset = 0, VkIndexType indexType = VK_INDEX_TYPE_UINT16);
    
    // 动态状态
    void setViewport(f32 x, f32 y, f32 width, f32 height, f32 minDepth = 0.0f, f32 maxDepth = 1.0f);
    void setViewport(const VkViewport& viewport);
    void setScissor(i32 x, i32 y, u32 width, u32 height);
    void setScissor(const VkRect2D& scissor);
    void setLineWidth(f32 width);
    void setDepthBias(f32 constantFactor, f32 clamp, f32 slopeFactor);
    void setStencilCompareMask(VkStencilFaceFlags face, u32 compareMask);
    void setStencilWriteMask(VkStencilFaceFlags face, u32 writeMask);
    void setStencilReference(VkStencilFaceFlags face, u32 reference);
    void setBlendConstants(const f32 constants[4]);
    void setBlendConstants(const Vec4& constants);
    
    // 推送常量
    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stageFlags,
                      u32 offset, u32 size, const void* data);
    void pushConstants(PipelineHandle pipeline, u32 offset, u32 size, const void* data);
    
    template<typename T>
    void pushConstants(VkPipelineLayout layout, VkShaderStageFlags stageFlags,
                      u32 offset, const T& data) {
        pushConstants(layout, stageFlags, offset, sizeof(T), &data);
    }
    
    // 绘制
    void draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0);
    void drawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, 
                    i32 vertexOffset = 0, u32 firstInstance = 0);
    void drawIndirect(VkBuffer buffer, u64 offset, u32 drawCount, u32 stride);
    void drawIndexedIndirect(VkBuffer buffer, u64 offset, u32 drawCount, u32 stride);
    
    // 计算
    void dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ);
    void dispatchIndirect(VkBuffer buffer, u64 offset);
    
    // 图像屏障
    void imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                     VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                     VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
    void imageBarrier(TextureHandle texture, VkImageLayout newLayout,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                     VkAccessFlags srcAccess, VkAccessFlags dstAccess);
    
    // 缓冲区屏障
    void bufferBarrier(VkBuffer buffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                      u32 srcQueueFamily = VK_QUEUE_FAMILY_IGNORED,
                      u32 dstQueueFamily = VK_QUEUE_FAMILY_IGNORED);
    
    // 复制
    void copyBuffer(VkBuffer src, VkBuffer dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0);
    void copyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0);
    void copyBufferToImage(VkBuffer src, VkImage dst, u32 width, u32 height,
                          VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    void copyImageToBuffer(VkImage src, VkBuffer dst, u32 width, u32 height,
                          VkImageLayout layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    void blitImage(VkImage src, VkImage dst, u32 srcWidth, u32 srcHeight,
                  u32 dstWidth, u32 dstHeight,
                  VkFilter filter = VK_FILTER_LINEAR);
    
    // 清空
    void clearColorImage(VkImage image, VkImageLayout layout, const Color& color,
                        const VkImageSubresourceRange& range);
    void clearDepthStencilImage(VkImage image, VkImageLayout layout, f32 depth, u32 stencil,
                                const VkImageSubresourceRange& range);
    
    // 原生访问
    VkCommandBuffer getHandle() const { return cmd_; }
    operator VkCommandBuffer() const { return cmd_; }
    
private:
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    bool recording_ = false;
};

// 命令池
class CommandPool {
public:
    CommandPool() = default;
    
    bool initialize(VkDevice device, u32 queueFamilyIndex, bool resetable = true);
    void shutdown();
    
    // 分配命令缓冲
    CommandBuffer allocate(CommandBufferLevel level = CommandBufferLevel::Primary);
    std::vector<CommandBuffer> allocate(u32 count, CommandBufferLevel level = CommandBufferLevel::Primary);
    
    // 释放
    void free(CommandBuffer& cmd);
    void free(std::vector<CommandBuffer>& cmds);
    
    // 重置
    void reset(bool releaseResources = false);
    
    VkCommandPool getHandle() const { return pool_; }
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
};

// 命令缓冲录制作用域
class ScopedCommandBuffer {
public:
    ScopedCommandBuffer(CommandBuffer& cmd, u32 flags = 0);
    ~ScopedCommandBuffer();
    
    CommandBuffer& get() { return cmd_; }
    
private:
    CommandBuffer& cmd_;
};

// 一次性命令
class OneTimeCommand {
public:
    OneTimeCommand(VkDevice device, VkCommandPool pool, VkQueue queue);
    ~OneTimeCommand();
    
    CommandBuffer& get() { return cmd_; }
    void submit();
    
    // 便捷方法
    void copyBuffer(VkBuffer src, VkBuffer dst, u64 size);
    void copyBufferToImage(VkBuffer src, VkImage image, u32 width, u32 height);
    void imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    
private:
    VkDevice device_;
    VkCommandPool pool_;
    VkQueue queue_;
    CommandBuffer cmd_;
    bool submitted_ = false;
};

// 命令缓冲池 (用于多线程)
class CommandBufferPool {
public:
    bool initialize(VkDevice device, u32 queueFamilyIndex, u32 threadCount);
    void shutdown();
    
    CommandBuffer allocate(u32 threadIndex);
    void resetAll();
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<VkCommandPool> pools_;
    std::vector<std::vector<VkCommandBuffer>> buffers_;
    std::vector<u32> nextBuffer_;
};

} // namespace Nova

/**
 * Nova Renderer - Command System Implementation
 */

#include "Nova/Command.h"
#include "Nova/Pipeline.h"

#include <cassert>

namespace Nova {

// ============================================================================
// CommandBuffer Implementation
// ============================================================================

CommandBuffer::CommandBuffer(VkCommandBuffer cmd, VkCommandPool pool, VkDevice device)
    : cmd_(cmd), pool_(pool), device_(device) {}

void CommandBuffer::begin(u32 flags) {
    if (recording_) return;
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = flags;
    
    vkBeginCommandBuffer(cmd_, &beginInfo);
    recording_ = true;
}

void CommandBuffer::end() {
    if (!recording_) return;
    
    vkEndCommandBuffer(cmd_);
    recording_ = false;
}

void CommandBuffer::beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                                    u32 width, u32 height, const Color& clearColor) {
    std::vector<Color> colors = {clearColor};
    beginRenderPass(renderPass, framebuffer, width, height, colors);
}

void CommandBuffer::beginRenderPass(VkRenderPass renderPass, VkFramebuffer framebuffer,
                                    u32 width, u32 height, const std::vector<Color>& clearColors) {
    std::vector<VkClearValue> clearValues(clearColors.size());
    for (size_t i = 0; i < clearColors.size(); i++) {
        clearValues[i].color = {{clearColors[i].r, clearColors[i].g, 
                                 clearColors[i].b, clearColors[i].a}};
    }
    
    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = renderPass;
    beginInfo.framebuffer = framebuffer;
    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = {width, height};
    beginInfo.clearValueCount = static_cast<u32>(clearValues.size());
    beginInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(cmd_, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void CommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(cmd_);
}

void CommandBuffer::bindPipeline(VkPipeline pipeline, VkPipelineBindPoint bindPoint) {
    vkCmdBindPipeline(cmd_, bindPoint, pipeline);
}

void CommandBuffer::bindPipeline(PipelineHandle pipeline) {
    VkPipeline vkPipeline = PipelineManager::sGetVkPipeline(pipeline);
    bindPipeline(vkPipeline, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

void CommandBuffer::bindComputePipeline(PipelineHandle pipeline) {
    VkPipeline vkPipeline = PipelineManager::sGetVkPipeline(pipeline);
    bindPipeline(vkPipeline, VK_PIPELINE_BIND_POINT_COMPUTE);
}

void CommandBuffer::bindDescriptorSets(VkPipelineLayout layout, u32 firstSet,
                                       const std::vector<VkDescriptorSet>& sets,
                                       VkPipelineBindPoint bindPoint) {
    vkCmdBindDescriptorSets(cmd_, bindPoint, layout, firstSet, 
                           static_cast<u32>(sets.size()), sets.data(), 0, nullptr);
}

void CommandBuffer::bindDescriptorSet(PipelineHandle pipeline, u32 set, VkDescriptorSet descriptorSet) {
    VkPipelineLayout layout = PipelineManager::sGetVkPipelineLayout(pipeline);
    bindDescriptorSets(layout, set, {descriptorSet});
}

void CommandBuffer::bindVertexBuffer(u32 binding, VkBuffer buffer, u64 offset) {
    vkCmdBindVertexBuffers(cmd_, binding, 1, &buffer, &offset);
}

void CommandBuffer::bindVertexBuffer(u32 binding, BufferHandle buffer, u64 offset) {
    // 需要从 VulkanBackend 获取实际的 VkBuffer
    // 简化处理
}

void CommandBuffer::bindIndexBuffer(VkBuffer buffer, u64 offset, VkIndexType indexType) {
    vkCmdBindIndexBuffer(cmd_, buffer, offset, indexType);
}

void CommandBuffer::bindIndexBuffer(BufferHandle buffer, u64 offset, VkIndexType indexType) {
    // 需要从 VulkanBackend 获取实际的 VkBuffer
}

void CommandBuffer::setViewport(f32 x, f32 y, f32 width, f32 height, f32 minDepth, f32 maxDepth) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
}

void CommandBuffer::setViewport(const VkViewport& viewport) {
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
}

void CommandBuffer::setScissor(i32 x, i32 y, u32 width, u32 height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
}

void CommandBuffer::setScissor(const VkRect2D& scissor) {
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
}

void CommandBuffer::setLineWidth(f32 width) {
    vkCmdSetLineWidth(cmd_, width);
}

void CommandBuffer::setDepthBias(f32 constantFactor, f32 clamp, f32 slopeFactor) {
    vkCmdSetDepthBias(cmd_, constantFactor, clamp, slopeFactor);
}

void CommandBuffer::setStencilCompareMask(VkStencilFaceFlags face, u32 compareMask) {
    vkCmdSetStencilCompareMask(cmd_, face, compareMask);
}

void CommandBuffer::setStencilWriteMask(VkStencilFaceFlags face, u32 writeMask) {
    vkCmdSetStencilWriteMask(cmd_, face, writeMask);
}

void CommandBuffer::setStencilReference(VkStencilFaceFlags face, u32 reference) {
    vkCmdSetStencilReference(cmd_, face, reference);
}

void CommandBuffer::setBlendConstants(const f32 constants[4]) {
    vkCmdSetBlendConstants(cmd_, constants);
}

void CommandBuffer::setBlendConstants(const Vec4& constants) {
    vkCmdSetBlendConstants(cmd_, &constants.x);
}

void CommandBuffer::pushConstants(VkPipelineLayout layout, VkShaderStageFlags stageFlags,
                                  u32 offset, u32 size, const void* data) {
    vkCmdPushConstants(cmd_, layout, stageFlags, offset, size, data);
}

void CommandBuffer::pushConstants(PipelineHandle pipeline, u32 offset, u32 size, const void* data) {
    VkPipelineLayout layout = PipelineManager::sGetVkPipelineLayout(pipeline);
    // 假设所有阶段
    pushConstants(layout, VK_SHADER_STAGE_ALL, offset, size, data);
}

void CommandBuffer::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    vkCmdDraw(cmd_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                               i32 vertexOffset, u32 firstInstance) {
    vkCmdDrawIndexed(cmd_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::drawIndirect(VkBuffer buffer, u64 offset, u32 drawCount, u32 stride) {
    vkCmdDrawIndirect(cmd_, buffer, offset, drawCount, stride);
}

void CommandBuffer::drawIndexedIndirect(VkBuffer buffer, u64 offset, u32 drawCount, u32 stride) {
    vkCmdDrawIndexedIndirect(cmd_, buffer, offset, drawCount, stride);
}

void CommandBuffer::dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
    vkCmdDispatch(cmd_, groupCountX, groupCountY, groupCountZ);
}

void CommandBuffer::dispatchIndirect(VkBuffer buffer, u64 offset) {
    vkCmdDispatchIndirect(cmd_, buffer, offset);
}

void CommandBuffer::imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                VkImageAspectFlags aspectMask) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    
    vkCmdPipelineBarrier(cmd_, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void CommandBuffer::imageBarrier(TextureHandle texture, VkImageLayout newLayout,
                                VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    // 需要从 TextureManager 获取实际的 VkImage 和当前布局
}

void CommandBuffer::bufferBarrier(VkBuffer buffer, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                 VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                 u32 srcQueueFamily, u32 dstQueueFamily) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = srcQueueFamily;
    barrier.dstQueueFamilyIndex = dstQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd_, srcStage, dstStage, 0, 0, nullptr, 1, &barrier, 0, nullptr);
}

void CommandBuffer::copyBuffer(VkBuffer src, VkBuffer dst, u64 size, u64 srcOffset, u64 dstOffset) {
    VkBufferCopy copy{};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.size = size;
    vkCmdCopyBuffer(cmd_, src, dst, 1, &copy);
}

void CommandBuffer::copyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset, u64 dstOffset) {
    // 需要从 VulkanBackend 获取实际的 VkBuffer
}

void CommandBuffer::copyBufferToImage(VkBuffer src, VkImage dst, u32 width, u32 height, VkImageLayout layout) {
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(cmd_, src, dst, layout, 1, &copy);
}

void CommandBuffer::copyImageToBuffer(VkImage src, VkBuffer dst, u32 width, u32 height, VkImageLayout layout) {
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {width, height, 1};
    
    vkCmdCopyImageToBuffer(cmd_, src, layout, dst, 1, &copy);
}

void CommandBuffer::blitImage(VkImage src, VkImage dst, u32 srcWidth, u32 srcHeight,
                             u32 dstWidth, u32 dstHeight, VkFilter filter) {
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = 0;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<i32>(srcWidth), static_cast<i32>(srcHeight), 1};
    
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = 0;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<i32>(dstWidth), static_cast<i32>(dstHeight), 1};
    
    vkCmdBlitImage(cmd_, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, filter);
}

void CommandBuffer::clearColorImage(VkImage image, VkImageLayout layout, const Color& color,
                                   const VkImageSubresourceRange& range) {
    VkClearColorValue clear{};
    clear.float32[0] = color.r;
    clear.float32[1] = color.g;
    clear.float32[2] = color.b;
    clear.float32[3] = color.a;
    
    vkCmdClearColorImage(cmd_, image, layout, &clear, 1, &range);
}

void CommandBuffer::clearDepthStencilImage(VkImage image, VkImageLayout layout, f32 depth, u32 stencil,
                                          const VkImageSubresourceRange& range) {
    VkClearDepthStencilValue clear{};
    clear.depth = depth;
    clear.stencil = stencil;
    
    vkCmdClearDepthStencilImage(cmd_, image, layout, &clear, 1, &range);
}

// ============================================================================
// CommandPool Implementation
// ============================================================================

bool CommandPool::initialize(VkDevice device, u32 queueFamilyIndex, bool resetable) {
    device_ = device;
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = resetable ? VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT : 0;
    
    return vkCreateCommandPool(device_, &poolInfo, nullptr, &pool_) == VK_SUCCESS;
}

void CommandPool::shutdown() {
    if (pool_) {
        vkDestroyCommandPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

CommandBuffer CommandPool::allocate(CommandBufferLevel level) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool_;
    allocInfo.level = static_cast<VkCommandBufferLevel>(level);
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS) {
        return CommandBuffer();
    }
    
    return CommandBuffer(cmd, pool_, device_);
}

std::vector<CommandBuffer> CommandPool::allocate(u32 count, CommandBufferLevel level) {
    std::vector<CommandBuffer> buffers;
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool_;
    allocInfo.level = static_cast<VkCommandBufferLevel>(level);
    allocInfo.commandBufferCount = count;
    
    std::vector<VkCommandBuffer> cmds(count);
    if (vkAllocateCommandBuffers(device_, &allocInfo, cmds.data()) != VK_SUCCESS) {
        return buffers;
    }
    
    buffers.reserve(count);
    for (auto cmd : cmds) {
        buffers.emplace_back(cmd, pool_, device_);
    }
    
    return buffers;
}

void CommandPool::free(CommandBuffer& cmd) {
    VkCommandBuffer buffer = cmd.getHandle();
    vkFreeCommandBuffers(device_, pool_, 1, &buffer);
}

void CommandPool::free(std::vector<CommandBuffer>& cmds) {
    std::vector<VkCommandBuffer> buffers;
    buffers.reserve(cmds.size());
    for (auto& cmd : cmds) {
        buffers.push_back(cmd.getHandle());
    }
    vkFreeCommandBuffers(device_, pool_, static_cast<u32>(buffers.size()), buffers.data());
}

void CommandPool::reset(bool releaseResources) {
    vkResetCommandPool(device_, pool_, releaseResources ? VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT : 0);
}

// ============================================================================
// ScopedCommandBuffer Implementation
// ============================================================================

ScopedCommandBuffer::ScopedCommandBuffer(CommandBuffer& cmd, u32 flags)
    : cmd_(cmd) {
    cmd_.begin(flags);
}

ScopedCommandBuffer::~ScopedCommandBuffer() {
    cmd_.end();
}

// ============================================================================
// OneTimeCommand Implementation
// ============================================================================

OneTimeCommand::OneTimeCommand(VkDevice device, VkCommandPool pool, VkQueue queue)
    : device_(device), pool_(pool), queue_(queue) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &allocInfo, &cmd);
    cmd_ = CommandBuffer(cmd, pool_, device_);
    cmd_.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
}

OneTimeCommand::~OneTimeCommand() {
    if (!submitted_) {
        submit();
    }
}

void OneTimeCommand::submit() {
    if (submitted_) return;
    
    cmd_.end();
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = cmd_.getHandle();
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);
    
    vkFreeCommandBuffers(device_, pool_, 1, &cmd);
    submitted_ = true;
}

void OneTimeCommand::copyBuffer(VkBuffer src, VkBuffer dst, u64 size) {
    cmd_.copyBuffer(src, dst, size);
}

void OneTimeCommand::copyBufferToImage(VkBuffer src, VkImage image, u32 width, u32 height) {
    cmd_.copyBufferToImage(src, image, width, height);
}

void OneTimeCommand::imageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkAccessFlags srcAccess, dstAccess;
    VkPipelineStageFlags srcStage, dstStage;
    
    // 根据布局转换设置访问标志
    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            srcAccess = 0;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            srcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        default:
            srcAccess = VK_ACCESS_MEMORY_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }
    
    switch (newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            dstAccess = VK_ACCESS_TRANSFER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            dstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            dstAccess = VK_ACCESS_SHADER_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        default:
            dstAccess = VK_ACCESS_MEMORY_READ_BIT;
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            break;
    }
    
    cmd_.imageBarrier(image, oldLayout, newLayout, srcStage, dstStage, srcAccess, dstAccess);
}

// ============================================================================
// CommandBufferPool Implementation
// ============================================================================

bool CommandBufferPool::initialize(VkDevice device, u32 queueFamilyIndex, u32 threadCount) {
    device_ = device;
    pools_.resize(threadCount);
    buffers_.resize(threadCount);
    nextBuffer_.resize(threadCount, 0);
    
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    for (u32 i = 0; i < threadCount; i++) {
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &pools_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

void CommandBufferPool::shutdown() {
    for (u32 i = 0; i < pools_.size(); i++) {
        // 命令缓冲会随着池销毁
        if (pools_[i]) {
            vkDestroyCommandPool(device_, pools_[i], nullptr);
        }
    }
    pools_.clear();
    buffers_.clear();
    nextBuffer_.clear();
    device_ = VK_NULL_HANDLE;
}

CommandBuffer CommandBufferPool::allocate(u32 threadIndex) {
    if (threadIndex >= pools_.size()) return CommandBuffer();
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = pools_[threadIndex];
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &cmd) != VK_SUCCESS) {
        return CommandBuffer();
    }
    
    return CommandBuffer(cmd, pools_[threadIndex], device_);
}

void CommandBufferPool::resetAll() {
    for (auto pool : pools_) {
        vkResetCommandPool(device_, pool, 0);
    }
}

} // namespace Nova

/**
 * Nova Renderer - Async Compute Queue Implementation
 */

#include "Nova/AsyncCompute.h"
#include "Nova/VulkanBackend.h"
#include <algorithm>

namespace Nova {

// ============================================================================
// AsyncComputeQueue Implementation
// ============================================================================

AsyncComputeQueue::AsyncComputeQueue() = default;

AsyncComputeQueue::~AsyncComputeQueue() {
    shutdown();
}

bool AsyncComputeQueue::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    auto device = backend->getDevice();
    auto& queueFamilies = backend->getQueueFamilies();
    
    // 检查是否有专用计算队列
    if (queueFamilies.hasCompute() && queueFamilies.compute != queueFamilies.graphics) {
        computeQueueFamily_ = queueFamilies.compute;
        computeQueue_ = backend->getComputeQueue();
    } else {
        // 使用图形队列
        computeQueueFamily_ = queueFamilies.graphics;
        computeQueue_ = backend->getGraphicsQueue();
    }
    
    // 创建命令池
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = computeQueueFamily_;
    
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        return false;
    }
    
    // 创建命令缓冲
    if (!createCommandBuffers()) {
        return false;
    }
    
    // 创建同步对象
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    if (vkCreateSemaphore(device, &semInfo, nullptr, &computeFinishedSemaphore_) != VK_SUCCESS) {
        return false;
    }
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    if (vkCreateFence(device, &fenceInfo, nullptr, &computeFence_) != VK_SUCCESS) {
        return false;
    }
    
    running_ = true;
    return true;
}

void AsyncComputeQueue::shutdown() {
    if (!backend_) return;
    
    waitAll();
    
    auto device = backend_->getDevice();
    
    destroyCommandBuffers();
    
    if (commandPool_) {
        vkDestroyCommandPool(device, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    
    if (computeFinishedSemaphore_) {
        vkDestroySemaphore(device, computeFinishedSemaphore_, nullptr);
        computeFinishedSemaphore_ = VK_NULL_HANDLE;
    }
    
    if (computeFence_) {
        vkDestroyFence(device, computeFence_, nullptr);
        computeFence_ = VK_NULL_HANDLE;
    }
    
    taskPromises_.clear();
    backend_ = nullptr;
    running_ = false;
}

bool AsyncComputeQueue::createCommandBuffers() {
    commandBuffers_.resize(4);  // 预分配 4 个命令缓冲
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<u32>(commandBuffers_.size());
    
    if (vkAllocateCommandBuffers(backend_->getDevice(), &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void AsyncComputeQueue::destroyCommandBuffers() {
    if (!commandBuffers_.empty() && commandPool_) {
        vkFreeCommandBuffers(backend_->getDevice(), commandPool_,
                             static_cast<u32>(commandBuffers_.size()), commandBuffers_.data());
        commandBuffers_.clear();
    }
}

ComputeTaskHandle AsyncComputeQueue::submit(ComputeTask task) {
    std::lock_guard<std::mutex> lock(queueMutex_);
    
    u64 taskId = nextTaskId_++;
    task.taskId = taskId;
    
    // 创建 promise
    taskPromises_[taskId] = std::promise<void>();
    
    pendingTasks_.push(std::move(task));
    
    return ComputeTaskHandle{taskId};
}

ComputeTaskHandle AsyncComputeQueue::submit(std::function<void(VkCommandBuffer)> execute, i32 priority) {
    ComputeTask task;
    task.execute = std::move(execute);
    task.priority = priority;
    return submit(std::move(task));
}

std::vector<ComputeTaskHandle> AsyncComputeQueue::submitBatch(std::vector<ComputeTask> tasks) {
    std::vector<ComputeTaskHandle> handles;
    handles.reserve(tasks.size());
    
    for (auto& task : tasks) {
        handles.push_back(submit(std::move(task)));
    }
    
    return handles;
}

void AsyncComputeQueue::wait(ComputeTaskHandle handle) {
    if (!handle.valid()) return;
    
    std::future<void> future;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto it = taskPromises_.find(handle.taskId);
        if (it != taskPromises_.end()) {
            future = it->second.get_future();
        }
    }
    
    if (future.valid()) {
        future.wait();
    }
}

void AsyncComputeQueue::waitAll() {
    vkWaitForFences(backend_->getDevice(), 1, &computeFence_, VK_TRUE, UINT64_MAX);
}

void AsyncComputeQueue::beginFrame() {
    // 重置命令缓冲索引
    currentBufferIndex_ = 0;
}

void AsyncComputeQueue::endFrame() {
    processQueue();
}

void AsyncComputeQueue::processQueue() {
    if (pendingTasks_.empty()) return;
    
    auto device = backend_->getDevice();
    
    // 等待上一帧完成
    vkWaitForFences(device, 1, &computeFence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &computeFence_);
    
    // 获取命令缓冲
    VkCommandBuffer cmd = commandBuffers_[currentBufferIndex_ % commandBuffers_.size()];
    currentBufferIndex_++;
    
    // 开始录制
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    // 执行所有任务
    std::vector<std::function<void()>> callbacks;
    
    while (!pendingTasks_.empty()) {
        ComputeTask task = pendingTasks_.top();
        pendingTasks_.pop();
        
        if (task.execute) {
            task.execute(cmd);
        }
        
        if (task.onComplete) {
            callbacks.push_back(std::move(task.onComplete));
        }
        
        // 设置 promise
        {
            auto it = taskPromises_.find(task.taskId);
            if (it != taskPromises_.end()) {
                it->second.set_value();
                taskPromises_.erase(it);
            }
        }
        
        completedTaskCount_++;
    }
    
    vkEndCommandBuffer(cmd);
    
    // 提交到计算队列
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &computeFinishedSemaphore_;
    
    vkQueueSubmit(computeQueue_, 1, &submitInfo, computeFence_);
    
    // 执行回调
    for (auto& callback : callbacks) {
        if (callback) callback();
    }
}

void AsyncComputeQueue::waitOnGraphics(VkCommandBuffer graphicsCmd) {
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    
    vkCmdWaitEvents(graphicsCmd, 0, nullptr, 
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, nullptr, 0, nullptr, 0, nullptr);
}

u32 AsyncComputeQueue::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<u32>(pendingTasks_.size());
}

// ============================================================================
// ComputeGraph Implementation
// ============================================================================

ComputeGraph::ComputeGraph() = default;

ComputeGraph::~ComputeGraph() = default;

u32 ComputeGraph::addNode(const std::string& name,
                           std::function<void(VkCommandBuffer)> execute,
                           const std::vector<u32>& dependencies) {
    u32 index = static_cast<u32>(nodes_.size());
    
    ComputeNode node;
    node.name = name;
    node.execute = std::move(execute);
    node.dependencies = dependencies;
    node.promise = std::make_shared<std::promise<void>>();
    
    nodes_.push_back(std::move(node));
    compiled_ = false;
    
    return index;
}

void ComputeGraph::clear() {
    nodes_.clear();
    levels_.clear();
    compiled_ = false;
}

void ComputeGraph::compile() {
    // 计算每个节点的层级
    for (auto& node : nodes_) {
        node.level = 0;
        node.scheduled = false;
        
        for (u32 dep : node.dependencies) {
            if (dep < nodes_.size()) {
                node.level = std::max(node.level, nodes_[dep].level + 1);
            }
        }
    }
    
    // 按层级分组
    u32 maxLevel = 0;
    for (const auto& node : nodes_) {
        maxLevel = std::max(maxLevel, node.level);
    }
    
    levels_.resize(maxLevel + 1);
    for (u32 i = 0; i < levels_.size(); i++) {
        levels_[i].clear();
    }
    
    for (u32 i = 0; i < nodes_.size(); i++) {
        levels_[nodes_[i].level].push_back(i);
    }
    
    compiled_ = true;
}

void ComputeGraph::execute(AsyncComputeQueue& queue) {
    if (!compiled_) {
        compile();
    }
    
    // 按层级顺序执行
    for (const auto& level : levels_) {
        for (u32 nodeIndex : level) {
            auto& node = nodes_[nodeIndex];
            
            queue.submit([&node](VkCommandBuffer cmd) {
                if (node.execute) {
                    node.execute(cmd);
                }
                node.promise->set_value();
            }, -static_cast<i32>(node.level));
        }
    }
}

void ComputeGraph::waitAll() {
    for (auto& node : nodes_) {
        node.promise->get_future().wait();
    }
}

// ============================================================================
// ComputeGraphicsSync Implementation
// ============================================================================

bool ComputeGraphicsSync::createSyncResources(VkDevice device,
                                               VkSemaphore& computeFinished,
                                               VkSemaphore& graphicsFinished,
                                               VkFence& computeFence) {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    if (vkCreateSemaphore(device, &semInfo, nullptr, &computeFinished) != VK_SUCCESS) {
        return false;
    }
    
    if (vkCreateSemaphore(device, &semInfo, nullptr, &graphicsFinished) != VK_SUCCESS) {
        vkDestroySemaphore(device, computeFinished, nullptr);
        return false;
    }
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    if (vkCreateFence(device, &fenceInfo, nullptr, &computeFence) != VK_SUCCESS) {
        vkDestroySemaphore(device, computeFinished, nullptr);
        vkDestroySemaphore(device, graphicsFinished, nullptr);
        return false;
    }
    
    return true;
}

void ComputeGraphicsSync::graphicsWaitForCompute(VkCommandBuffer graphicsCmd,
                                                  VkSemaphore computeFinished,
                                                  VkPipelineStageFlags waitStage) {
    // 这个操作需要在 vkQueueSubmit 中进行
    // 这里只是占位，实际使用时需要在 submit info 中设置 waitSemaphores
}

void ComputeGraphicsSync::computeWaitForGraphics(VkCommandBuffer computeCmd,
                                                  VkSemaphore graphicsFinished,
                                                  VkPipelineStageFlags waitStage) {
    // 同上
}

void ComputeGraphicsSync::insertBufferBarrier(VkCommandBuffer cmd,
                                               VkBuffer buffer,
                                               VkAccessFlags srcAccess,
                                               VkAccessFlags dstAccess,
                                               VkPipelineStageFlags srcStage,
                                               VkPipelineStageFlags dstStage) {
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 1, &barrier, 0, nullptr);
}

void ComputeGraphicsSync::insertImageBarrier(VkCommandBuffer cmd,
                                              VkImage image,
                                              VkImageLayout oldLayout,
                                              VkImageLayout newLayout,
                                              VkAccessFlags srcAccess,
                                              VkAccessFlags dstAccess,
                                              VkPipelineStageFlags srcStage,
                                              VkPipelineStageFlags dstStage,
                                              VkImageAspectFlags aspect) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

// ============================================================================
// AsyncTransferQueue Implementation
// ============================================================================

AsyncTransferQueue::AsyncTransferQueue() = default;

AsyncTransferQueue::~AsyncTransferQueue() {
    shutdown();
}

bool AsyncTransferQueue::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    auto device = backend->getDevice();
    auto& queueFamilies = backend->getQueueFamilies();
    
    // 检查是否有专用传输队列
    if (queueFamilies.hasTransfer()) {
        transferQueueFamily_ = queueFamilies.transfer;
        transferQueue_ = backend->getTransferQueue();
    } else {
        transferQueueFamily_ = queueFamilies.graphics;
        transferQueue_ = backend->getGraphicsQueue();
    }
    
    // 创建命令池
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = transferQueueFamily_;
    
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        return false;
    }
    
    // 创建同步对象
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    if (vkCreateSemaphore(device, &semInfo, nullptr, &transferFinishedSemaphore_) != VK_SUCCESS) {
        return false;
    }
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    
    if (vkCreateFence(device, &fenceInfo, nullptr, &transferFence_) != VK_SUCCESS) {
        return false;
    }
    
    return true;
}

void AsyncTransferQueue::shutdown() {
    if (!backend_) return;
    
    waitIdle();
    
    auto device = backend_->getDevice();
    
    // 清理暂存缓冲
    for (auto& buffer : stagingBuffers_) {
        if (buffer.valid()) {
            backend_->destroyBuffer(buffer);
        }
    }
    stagingBuffers_.clear();
    
    if (commandPool_) {
        vkDestroyCommandPool(device, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }
    
    if (transferFinishedSemaphore_) {
        vkDestroySemaphore(device, transferFinishedSemaphore_, nullptr);
        transferFinishedSemaphore_ = VK_NULL_HANDLE;
    }
    
    if (transferFence_) {
        vkDestroyFence(device, transferFence_, nullptr);
        transferFence_ = VK_NULL_HANDLE;
    }
    
    backend_ = nullptr;
}

void AsyncTransferQueue::submit(const TransferTask& task) {
    auto device = backend_->getDevice();
    
    // 分配命令缓冲
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);
    
    // 开始录制
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    switch (task.type) {
        case TransferTask::Type::BufferCopy:
            // vkCmdCopyBuffer(cmd, ...);
            break;
        case TransferTask::Type::BufferToImage:
            // vkCmdCopyBufferToImage(cmd, ...);
            break;
        case TransferTask::Type::ImageToBuffer:
            // vkCmdCopyImageToBuffer(cmd, ...);
            break;
        case TransferTask::Type::ImageCopy:
            // vkCmdCopyImage(cmd, ...);
            break;
    }
    
    vkEndCommandBuffer(cmd);
    
    // 提交
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &transferFinishedSemaphore_;
    
    vkQueueSubmit(transferQueue_, 1, &submitInfo, transferFence_);
    
    // 等待完成
    vkWaitForFences(device, 1, &transferFence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &transferFence_);
    
    // 释放命令缓冲
    vkFreeCommandBuffers(device, commandPool_, 1, &cmd);
    
    if (task.onComplete) {
        task.onComplete();
    }
}

void AsyncTransferQueue::submitBufferCopy(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset, u64 dstOffset) {
    TransferTask task;
    task.type = TransferTask::Type::BufferCopy;
    task.srcBuffer = src;
    task.dstBuffer = dst;
    task.bufferRegion.srcOffset = srcOffset;
    task.bufferRegion.dstOffset = dstOffset;
    task.bufferRegion.size = size;
    
    submit(task);
}

void AsyncTransferQueue::submitBufferToImage(BufferHandle src, TextureHandle dst, u32 width, u32 height) {
    TransferTask task;
    task.type = TransferTask::Type::BufferToImage;
    task.srcBuffer = src;
    task.dstTexture = dst;
    task.bufferImageRegion.imageExtent = {width, height, 1};
    
    submit(task);
}

void AsyncTransferQueue::waitIdle() {
    vkWaitForFences(backend_->getDevice(), 1, &transferFence_, VK_TRUE, UINT64_MAX);
}

} // namespace Nova

/**
 * Nova Renderer - Async Compute Queue
 * 异步计算队列 - 独立于图形队列的计算任务执行
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 计算任务
// ============================================================================

struct ComputeTask {
    std::string name;
    std::function<void(VkCommandBuffer)> execute;
    
    // 依赖
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkSemaphore> signalSemaphores;
    
    // 完成回调
    std::function<void()> onComplete;
    
    // 优先级 (数值越小越优先)
    i32 priority = 0;
    
    // 任务 ID
    u64 taskId = 0;
    
    // 比较运算符 (用于 priority_queue，优先级高的在前)
    bool operator<(const ComputeTask& o) const {
        return priority > o.priority;  // 反转，使数值小的优先
    }
};

// 计算任务句柄
struct ComputeTaskHandle {
    u64 taskId;
    bool valid() const { return taskId != 0; }
};

// ============================================================================
// 异步计算队列
// ============================================================================

class AsyncComputeQueue {
public:
    AsyncComputeQueue();
    ~AsyncComputeQueue();
    
    // 初始化
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 提交计算任务
    ComputeTaskHandle submit(ComputeTask task);
    ComputeTaskHandle submit(std::function<void(VkCommandBuffer)> execute, 
                              i32 priority = 0);
    
    // 批量提交
    std::vector<ComputeTaskHandle> submitBatch(std::vector<ComputeTask> tasks);
    
    // 等待任务完成
    void wait(ComputeTaskHandle handle);
    void waitAll();
    
    // 每帧处理
    void beginFrame();
    void endFrame();
    
    // 与图形队列同步
    VkSemaphore getComputeFinishedSemaphore() const { return computeFinishedSemaphore_; }
    void waitOnGraphics(VkCommandBuffer graphicsCmd);
    
    // 统计
    u32 getPendingTaskCount() const;
    u32 getCompletedTaskCount() const { return completedTaskCount_; }
    bool isIdle() const { return getPendingTaskCount() == 0; }
    
private:
    void processQueue();
    bool createCommandBuffers();
    void destroyCommandBuffers();
    
    VulkanBackend* backend_ = nullptr;
    
    // 计算队列
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    u32 computeQueueFamily_ = UINT32_MAX;
    
    // 命令池和缓冲
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    u32 currentBufferIndex_ = 0;
    
    // 同步
    VkSemaphore computeFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence computeFence_ = VK_NULL_HANDLE;
    
    // 任务队列
    std::priority_queue<ComputeTask> pendingTasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable queueCondition_;
    
    // 任务跟踪
    std::unordered_map<u64, std::promise<void>> taskPromises_;
    u64 nextTaskId_ = 1;
    u32 completedTaskCount_ = 0;
    
    // 运行状态
    bool running_ = false;
};

// ============================================================================
// 计算图调度器
// ============================================================================

// 计算任务节点
struct ComputeNode {
    std::string name;
    std::function<void(VkCommandBuffer)> execute;
    std::vector<u32> dependencies;  // 依赖的节点索引
    
    // 调度信息
    u32 level = 0;  // 拓扑层级
    bool scheduled = false;
    std::shared_ptr<std::promise<void>> promise;
};

// 计算图
class ComputeGraph {
public:
    ComputeGraph();
    ~ComputeGraph();
    
    // 构建图
    u32 addNode(const std::string& name,
                std::function<void(VkCommandBuffer)> execute,
                const std::vector<u32>& dependencies = {});
    
    void clear();
    
    // 编译 (计算拓扑排序和层级)
    void compile();
    
    // 执行 (调度到异步计算队列)
    void execute(AsyncComputeQueue& queue);
    
    // 等待完成
    void waitAll();
    
private:
    std::vector<ComputeNode> nodes_;
    std::vector<std::vector<u32>> levels_;  // 按层级分组的节点
    bool compiled_ = false;
};

// ============================================================================
// 计算/图形协作
// ============================================================================

class ComputeGraphicsSync {
public:
    // 创建同步资源
    static bool createSyncResources(VkDevice device,
                                     VkSemaphore& computeFinished,
                                     VkSemaphore& graphicsFinished,
                                     VkFence& computeFence);
    
    // 图形队列等待计算完成
    static void graphicsWaitForCompute(VkCommandBuffer graphicsCmd,
                                        VkSemaphore computeFinished,
                                        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    
    // 计算队列等待图形完成
    static void computeWaitForGraphics(VkCommandBuffer computeCmd,
                                        VkSemaphore graphicsFinished,
                                        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    
    // 插入屏障
    static void insertBufferBarrier(VkCommandBuffer cmd,
                                     VkBuffer buffer,
                                     VkAccessFlags srcAccess,
                                     VkAccessFlags dstAccess,
                                     VkPipelineStageFlags srcStage,
                                     VkPipelineStageFlags dstStage);
    
    static void insertImageBarrier(VkCommandBuffer cmd,
                                    VkImage image,
                                    VkImageLayout oldLayout,
                                    VkImageLayout newLayout,
                                    VkAccessFlags srcAccess,
                                    VkAccessFlags dstAccess,
                                    VkPipelineStageFlags srcStage,
                                    VkPipelineStageFlags dstStage,
                                    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
};

// ============================================================================
// 异步数据传输
// ============================================================================

struct TransferTask {
    enum class Type : u32 {
        BufferCopy,
        BufferToImage,
        ImageToBuffer,
        ImageCopy
    };
    
    Type type;
    
    // 源和目标
    BufferHandle srcBuffer;
    BufferHandle dstBuffer;
    TextureHandle srcTexture;
    TextureHandle dstTexture;
    
    // 区域
    VkBufferCopy bufferRegion;
    VkBufferImageCopy bufferImageRegion;
    VkImageCopy imageRegion;
    
    // 完成回调
    std::function<void()> onComplete;
};

class AsyncTransferQueue {
public:
    AsyncTransferQueue();
    ~AsyncTransferQueue();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    // 提交传输任务
    void submit(const TransferTask& task);
    void submitBufferCopy(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset = 0, u64 dstOffset = 0);
    void submitBufferToImage(BufferHandle src, TextureHandle dst, u32 width, u32 height);
    
    // 等待完成
    void waitIdle();
    
    // 与图形/计算队列同步
    VkSemaphore getTransferFinishedSemaphore() const { return transferFinishedSemaphore_; }
    
private:
    VulkanBackend* backend_ = nullptr;
    
    VkQueue transferQueue_ = VK_NULL_HANDLE;
    u32 transferQueueFamily_ = UINT32_MAX;
    
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandPool stagingCommandPool_ = VK_NULL_HANDLE;
    
    VkSemaphore transferFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence transferFence_ = VK_NULL_HANDLE;
    
    // 暂存缓冲池
    std::vector<BufferHandle> stagingBuffers_;
    u64 stagingBufferSize_ = 64 * 1024 * 1024;  // 64 MB
};

} // namespace Nova

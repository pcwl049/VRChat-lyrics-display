/**
 * Nova Renderer - Thread Pool Implementation
 */

#include "Nova/ThreadPool.h"
#include <algorithm>
#include <chrono>

namespace Nova {

// ============================================================================
// ThreadPool Implementation
// ============================================================================

ThreadPool::ThreadPool() = default;

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::initialize(u32 threadCount) {
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
    }
    
    running_ = true;
    
    threads_.reserve(threadCount);
    for (u32 i = 0; i < threadCount; i++) {
        threads_.emplace_back(&ThreadPool::workerThread, this, i);
    }
    
    return true;
}

void ThreadPool::shutdown() {
    if (!running_) return;
    
    running_ = false;
    condition_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threads_.clear();
    
    // 清空任务队列
    std::priority_queue<Task> empty;
    std::swap(taskQueue_, empty);
}

std::future<void> ThreadPool::submit(std::function<void()> task, TaskPriority priority) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    
    Task t;
    t.func = [task = std::move(task), promise]() {
        try {
            task();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    t.priority = priority;
    t.submitTime = taskCounter_++;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        taskQueue_.push(t);
    }
    
    condition_.notify_one();
    return future;
}

void ThreadPool::submitBatch(std::vector<std::function<void()>> tasks, TaskPriority priority) {
    std::vector<std::future<void>> futures;
    futures.reserve(tasks.size());
    
    for (auto& task : tasks) {
        futures.push_back(submit(std::move(task), priority));
    }
    
    for (auto& f : futures) {
        f.wait();
    }
}

void ThreadPool::waitAll() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    finishedCondition_.wait(lock, [this]() {
        return taskQueue_.empty() && activeTasks_.load() == 0;
    });
}

u32 ThreadPool::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<u32>(taskQueue_.size());
}

void ThreadPool::workerThread(u32 threadIndex) {
    while (running_) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            condition_.wait(lock, [this]() {
                return !taskQueue_.empty() || !running_;
            });
            
            if (!running_) break;
            
            if (taskQueue_.empty()) continue;
            
            task = taskQueue_.top();
            taskQueue_.pop();
        }
        
        activeTasks_++;
        
        // 执行任务
        if (task.func) {
            task.func();
        }
        
        activeTasks_--;
        
        // 通知等待的线程
        finishedCondition_.notify_all();
    }
}

// ============================================================================
// Global Thread Pool
// ============================================================================

namespace {
    std::unique_ptr<ThreadPool> g_threadPool;
}

ThreadPool& ParallelExecutor::getGlobal() {
    if (!g_threadPool) {
        g_threadPool = std::make_unique<ThreadPool>();
        g_threadPool->initialize();
    }
    return *g_threadPool;
}

// ============================================================================
// ThreadedCommandRecorder Implementation
// ============================================================================

ThreadedCommandRecorder::ThreadedCommandRecorder() = default;

ThreadedCommandRecorder::~ThreadedCommandRecorder() {
    shutdown();
}

bool ThreadedCommandRecorder::initialize(VkDevice device, u32 queueFamilyIndex, u32 threadCount) {
    device_ = device;
    
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 4;
    }
    
    // 每个线程一个命令池
    commandPools_.resize(threadCount);
    commandBuffers_.resize(threadCount);
    
    for (u32 i = 0; i < threadCount; i++) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPools_[i]) != VK_SUCCESS) {
            return false;
        }
        
        // 预分配命令缓冲
        commandBuffers_[i].resize(16);  // 预分配 16 个命令缓冲
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPools_[i];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        allocInfo.commandBufferCount = 16;
        
        if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_[i].data()) != VK_SUCCESS) {
            return false;
        }
    }
    
    threadPool_ = &ParallelExecutor::getGlobal();
    return true;
}

void ThreadedCommandRecorder::shutdown() {
    for (auto& pool : commandPools_) {
        if (pool) {
            vkDestroyCommandPool(device_, pool, nullptr);
        }
    }
    commandPools_.clear();
    commandBuffers_.clear();
    device_ = VK_NULL_HANDLE;
}

VkCommandBuffer ThreadedCommandRecorder::allocateSecondaryCommandBuffer(u32 threadIndex) {
    if (threadIndex >= commandBuffers_.size()) return VK_NULL_HANDLE;
    
    auto& buffers = commandBuffers_[threadIndex];
    
    // 查找空闲的命令缓冲
    for (auto& buf : buffers) {
        // 简化版：直接使用预分配的缓冲
        // 实际应该跟踪使用状态
        return buf;
    }
    
    // 需要分配更多
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPools_[threadIndex];
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer newBuffer;
    if (vkAllocateCommandBuffers(device_, &allocInfo, &newBuffer) == VK_SUCCESS) {
        buffers.push_back(newBuffer);
        return newBuffer;
    }
    
    return VK_NULL_HANDLE;
}

std::future<VkCommandBuffer> ThreadedCommandRecorder::submitRecordTask(
    std::function<void(VkCommandBuffer)> recordFunc) {
    
    u32 threadIndex = currentBufferIndex_ % static_cast<u32>(commandBuffers_.size());
    currentBufferIndex_++;
    
    auto promise = std::make_shared<std::promise<VkCommandBuffer>>();
    auto future = promise->get_future();
    
    auto task = [this, threadIndex, recordFunc = std::move(recordFunc), promise]() {
        VkCommandBuffer cmd = allocateSecondaryCommandBuffer(threadIndex);
        
        if (cmd) {
            // 开始命令缓冲
            VkCommandBufferInheritanceInfo inheritanceInfo{};
            inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            // 实际应该设置 render pass 和 framebuffer
            
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            beginInfo.pInheritanceInfo = &inheritanceInfo;
            
            vkBeginCommandBuffer(cmd, &beginInfo);
            
            // 录制命令
            recordFunc(cmd);
            
            vkEndCommandBuffer(cmd);
        }
        
        promise->set_value(cmd);
    };
    
    pendingTasks_.push_back(threadPool_->submit(std::move(task)));
    
    return future;
}

std::vector<VkCommandBuffer> ThreadedCommandRecorder::executeAndWait() {
    std::vector<VkCommandBuffer> results;
    results.reserve(pendingTasks_.size());
    
    // 等待所有任务完成
    for (auto& future : pendingTasks_) {
        future.wait();
        future.get();  // 获取 void 结果以捕获任何异常
    }
    
    // 注意：实际的 VkCommandBuffer 结果通过 submitRecordTask 返回的 future 获取
    // 这个方法现在只等待所有任务完成
    pendingTasks_.clear();
    return results;
}

void ThreadedCommandRecorder::reset() {
    // 重置所有命令缓冲
    for (size_t i = 0; i < commandPools_.size(); i++) {
        vkResetCommandPool(device_, commandPools_[i], 0);
    }
    
    currentBufferIndex_ = 0;
    pendingTasks_.clear();
}

// ============================================================================
// RenderTaskGraph Implementation
// ============================================================================

RenderTaskGraph::RenderTaskGraph() = default;

RenderTaskGraph::~RenderTaskGraph() = default;

bool RenderTaskGraph::initialize(u32 maxTasks) {
    tasks_.reserve(maxTasks);
    return true;
}

void RenderTaskGraph::shutdown() {
    clear();
}

u32 RenderTaskGraph::addTask(const std::string& name,
                              std::function<void(VkCommandBuffer)> execute,
                              const std::vector<u32>& dependencies) {
    u32 index = static_cast<u32>(tasks_.size());
    
    RenderTaskNode node;
    node.name = name;
    node.execute = std::move(execute);
    node.dependencies = dependencies;
    node.completed = false;
    node.pendingDependencies = static_cast<u32>(dependencies.size());
    
    tasks_.push_back(std::move(node));
    compiled_ = false;
    
    return index;
}

void RenderTaskGraph::clear() {
    tasks_.clear();
    executionOrder_.clear();
    compiled_ = false;
}

void RenderTaskGraph::compile() {
    topologicalSort();
    compiled_ = true;
}

void RenderTaskGraph::execute(ThreadPool& pool, ThreadedCommandRecorder& recorder) {
    if (!compiled_) {
        compile();
    }
    
    // 重置依赖计数
    for (auto& task : tasks_) {
        task.completed = false;
        task.pendingDependencies = static_cast<u32>(task.dependencies.size());
    }
    
    // 并行执行任务
    std::atomic<u32> nextTaskIndex{0};
    std::mutex completeMutex;
    std::condition_variable completeCondition;
    
    u32 totalTasks = static_cast<u32>(tasks_.size());
    
    // 提交所有任务
    for (u32 taskIdx : executionOrder_) {
        auto& task = tasks_[taskIdx];
        
        // 等待依赖完成
        for (u32 dep : task.dependencies) {
            // 简化版：忙等待
            // 实际应该使用条件变量
            while (!tasks_[dep].completed) {
                std::this_thread::yield();
            }
        }
        
        // 提交任务
        pool.submit([this, taskIdx]() {
            auto& task = tasks_[taskIdx];
            
            // 获取命令缓冲并执行
            VkCommandBuffer cmd = VK_NULL_HANDLE;  // 实际应该从 recorder 获取
            if (task.execute) {
                task.execute(cmd);
            }
            
            task.completed = true;
        });
    }
    
    // 等待所有任务完成
    pool.waitAll();
}

void RenderTaskGraph::topologicalSort() {
    executionOrder_.clear();
    
    // Kahn's algorithm
    std::vector<u32> inDegree(tasks_.size(), 0);
    
    for (const auto& task : tasks_) {
        for (u32 dep : task.dependencies) {
            // 任务 'dep' 必须在当前任务之前完成
            // 这里我们计算入度
        }
        inDegree[&task - tasks_.data()] = static_cast<u32>(task.dependencies.size());
    }
    
    // 找到所有入度为 0 的任务
    std::queue<u32> queue;
    for (size_t i = 0; i < tasks_.size(); i++) {
        if (tasks_[i].dependencies.empty()) {
            queue.push(static_cast<u32>(i));
        }
    }
    
    while (!queue.empty()) {
        u32 idx = queue.front();
        queue.pop();
        
        executionOrder_.push_back(idx);
        
        // 更新依赖此任务的其他任务
        for (size_t i = 0; i < tasks_.size(); i++) {
            auto& deps = tasks_[i].dependencies;
            for (u32 dep : deps) {
                if (dep == idx) {
                    inDegree[i]--;
                    if (inDegree[i] == 0) {
                        queue.push(static_cast<u32>(i));
                    }
                }
            }
        }
    }
    
    // 如果有循环依赖，executionOrder_ 会少于 tasks_.size()
}

} // namespace Nova

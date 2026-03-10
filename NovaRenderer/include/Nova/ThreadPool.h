/**
 * Nova Renderer - Thread Pool
 * 线程池和任务调度系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <atomic>

namespace Nova {

// 任务优先级
enum class TaskPriority : u32 {
    High = 0,
    Normal = 1,
    Low = 2
};

// 任务结构
struct Task {
    std::function<void()> func;
    TaskPriority priority = TaskPriority::Normal;
    u64 submitTime = 0;
    
    bool operator<(const Task& other) const {
        if (priority != other.priority) {
            return static_cast<u32>(priority) > static_cast<u32>(other.priority);
        }
        return submitTime > other.submitTime;
    }
};

// 线程池
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();
    
    // 初始化
    bool initialize(u32 threadCount = 0);  // 0 = 自动检测核心数
    void shutdown();
    
    // 提交任务
    std::future<void> submit(std::function<void()> task, TaskPriority priority = TaskPriority::Normal);
    
    // 批量提交
    void submitBatch(std::vector<std::function<void()>> tasks, TaskPriority priority = TaskPriority::Normal);
    
    // 等待所有任务完成
    void waitAll();
    
    // 获取信息
    u32 getThreadCount() const { return static_cast<u32>(threads_.size()); }
    u32 getActiveTaskCount() const { return activeTasks_.load(); }
    u32 getPendingTaskCount() const;
    bool isIdle() const { return getPendingTaskCount() == 0 && activeTasks_.load() == 0; }
    
private:
    void workerThread(u32 threadIndex);
    
    std::vector<std::thread> threads_;
    std::priority_queue<Task> taskQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::condition_variable finishedCondition_;
    
    std::atomic<bool> running_{false};
    std::atomic<u32> activeTasks_{0};
    u64 taskCounter_ = 0;
};

// 并行执行辅助
class ParallelExecutor {
public:
    static ThreadPool& getGlobal();
    
    // 并行 for 循环
    template<typename Func>
    static void parallelFor(u32 begin, u32 end, u32 step, Func&& func) {
        auto& pool = getGlobal();
        u32 range = end - begin;
        u32 chunkSize = std::max(step, range / pool.getThreadCount());
        
        std::vector<std::future<void>> futures;
        
        for (u32 i = begin; i < end; i += chunkSize) {
            u32 chunkEnd = std::min(i + chunkSize, end);
            futures.push_back(pool.submit([i, chunkEnd, step, &func]() {
                for (u32 j = i; j < chunkEnd; j += step) {
                    func(j);
                }
            }));
        }
        
        for (auto& f : futures) {
            f.wait();
        }
    }
    
    // 并行执行多个任务
    template<typename... Tasks>
    static void parallelExecute(Tasks&&... tasks) {
        auto& pool = getGlobal();
        std::vector<std::future<void>> futures;
        
        (futures.push_back(pool.submit(std::forward<Tasks>(tasks))), ...);
        
        for (auto& f : futures) {
            f.wait();
        }
    }
};

// 命令缓冲录制任务
struct CommandBufferTask {
    u32 taskId;
    u32 commandBufferIndex;
    std::function<void(VkCommandBuffer)> recordFunc;
    std::promise<VkCommandBuffer> promise;
};

// 多线程命令缓冲录制器
class ThreadedCommandRecorder {
public:
    ThreadedCommandRecorder();
    ~ThreadedCommandRecorder();
    
    bool initialize(VkDevice device, u32 queueFamilyIndex, u32 threadCount = 0);
    void shutdown();
    
    // 创建次级命令缓冲
    VkCommandBuffer allocateSecondaryCommandBuffer(u32 threadIndex);
    
    // 提交录制任务
    std::future<VkCommandBuffer> submitRecordTask(std::function<void(VkCommandBuffer)> recordFunc);
    
    // 执行所有任务并收集命令缓冲
    std::vector<VkCommandBuffer> executeAndWait();
    
    // 重置 (每帧开始时调用)
    void reset();
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    std::vector<VkCommandPool> commandPools_;  // 每线程一个池
    std::vector<std::vector<VkCommandBuffer>> commandBuffers_;  // 预分配的命令缓冲
    
    ThreadPool* threadPool_ = nullptr;
    std::vector<std::future<void>> pendingTasks_;
    u32 currentBufferIndex_ = 0;
};

// 渲染任务图节点
struct RenderTaskNode {
    std::string name;
    std::function<void(VkCommandBuffer)> execute;
    std::vector<u32> dependencies;  // 依赖的任务索引
    
    // 状态
    bool completed = false;
    u32 pendingDependencies = 0;
    
    RenderTaskNode() = default;
    RenderTaskNode(const RenderTaskNode& o) 
        : name(o.name), execute(o.execute), dependencies(o.dependencies),
          completed(o.completed), pendingDependencies(o.pendingDependencies) {}
    RenderTaskNode(RenderTaskNode&&) = default;
    RenderTaskNode& operator=(const RenderTaskNode& o) {
        name = o.name;
        execute = o.execute;
        dependencies = o.dependencies;
        completed = o.completed;
        pendingDependencies = o.pendingDependencies;
        return *this;
    }
    RenderTaskNode& operator=(RenderTaskNode&&) = default;
};

// 渲染任务图 (DAG 调度)
class RenderTaskGraph {
public:
    RenderTaskGraph();
    ~RenderTaskGraph();
    
    bool initialize(u32 maxTasks = 64);
    void shutdown();
    
    // 添加任务
    u32 addTask(const std::string& name, 
                std::function<void(VkCommandBuffer)> execute,
                const std::vector<u32>& dependencies = {});
    
    // 清除所有任务
    void clear();
    
    // 编译任务图 (计算依赖关系)
    void compile();
    
    // 执行任务图 (并行)
    void execute(ThreadPool& pool, ThreadedCommandRecorder& recorder);
    
    // 获取执行顺序 (拓扑排序结果)
    const std::vector<u32>& getExecutionOrder() const { return executionOrder_; }
    
private:
    void topologicalSort();
    
    std::vector<RenderTaskNode> tasks_;
    std::vector<u32> executionOrder_;
    bool compiled_ = false;
};

} // namespace Nova

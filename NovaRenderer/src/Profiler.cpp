/**
 * Nova Renderer - GPU Profiler Implementation
 */

#include "Nova/Profiler.h"
#include "Nova/VulkanBackend.h"
#include <vk_mem_alloc.h>

#include <iostream>
#include <iomanip>
#include <algorithm>

namespace Nova {

// ============================================================================
// TimestampQueryPool Implementation
// ============================================================================

TimestampQueryPool::TimestampQueryPool() = default;

TimestampQueryPool::~TimestampQueryPool() {
    shutdown();
}

bool TimestampQueryPool::initialize(VkDevice device, u32 queueFamilyIndex, u32 maxQueries) {
    device_ = device;
    maxQueries_ = maxQueries;
    queryCount_ = 0;
    
    VkQueryPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    createInfo.queryCount = maxQueries_;
    
    if (vkCreateQueryPool(device_, &createInfo, nullptr, &queryPool_) != VK_SUCCESS) {
        return false;
    }
    
    // 获取时间戳周期
    VkPhysicalDeviceProperties props;
    // 实际需要从 VulkanBackend 获取
    timestampPeriod_ = 1.0;
    
    return true;
}

void TimestampQueryPool::shutdown() {
    if (queryPool_) {
        vkDestroyQueryPool(device_, queryPool_, nullptr);
        queryPool_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

u32 TimestampQueryPool::allocateQueryPair() {
    if (queryCount_ + 2 > maxQueries_) {
        return UINT32_MAX;
    }
    
    u32 start = queryCount_;
    queryCount_ += 2;
    return start;
}

void TimestampQueryPool::writeTimestamp(VkCommandBuffer cmd, u32 queryIndex, VkPipelineStageFlagBits stage) {
    vkCmdWriteTimestamp(cmd, stage, queryPool_, queryIndex);
}

f64 TimestampQueryPool::getResult(u32 startQuery, u32 endQuery) {
    u64 timestamps[2];
    
    VkResult result = vkGetQueryPoolResults(
        device_, queryPool_, startQuery, 2,
        sizeof(timestamps), timestamps, sizeof(u64),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
    );
    
    if (result != VK_SUCCESS) {
        return 0.0;
    }
    
    // 转换为毫秒
    f64 delta = static_cast<f64>(timestamps[1] - timestamps[0]) * timestampPeriod_ / 1000000.0;
    return delta;
}

void TimestampQueryPool::reset(VkCommandBuffer cmd) {
    vkCmdResetQueryPool(cmd, queryPool_, 0, queryCount_);
    queryCount_ = 0;
}

// ============================================================================
// GPUProfiler Implementation
// ============================================================================

GPUProfiler::GPUProfiler() {
    gpuTimeHistory_.fill(0.0);
    cpuTimeHistory_.fill(0.0);
}

GPUProfiler::~GPUProfiler() {
    shutdown();
}

bool GPUProfiler::initialize(VulkanBackend* backend, u32 framesInFlight) {
    backend_ = backend;
    framesInFlight_ = framesInFlight;
    
    auto device = backend->getDevice();
    auto queueFamily = backend->getQueueFamilies().graphics;
    
    queryPools_.resize(framesInFlight);
    frameData_.resize(framesInFlight);
    
    for (auto& pool : queryPools_) {
        if (!pool.initialize(device, queueFamily)) {
            return false;
        }
    }
    
    return true;
}

void GPUProfiler::shutdown() {
    for (auto& pool : queryPools_) {
        pool.shutdown();
    }
    queryPools_.clear();
    frameData_.clear();
    backend_ = nullptr;
}

void GPUProfiler::beginFrame(VkCommandBuffer cmd) {
    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
    
    auto& pool = queryPools_[currentFrame_];
    pool.reset(cmd);
    
    auto& data = frameData_[currentFrame_];
    data.reset();
    data.frameIndex = currentFrame_;
    
    scopeStack_.clear();
    
    // 开始帧范围
    beginScope(cmd, "Frame");
}

void GPUProfiler::endFrame(VkCommandBuffer cmd) {
    // 结束帧范围
    endScope(cmd);
    
    auto& pool = queryPools_[currentFrame_];
    auto& data = frameData_[currentFrame_];
    
    // 收集结果
    for (auto& scope : data.scopes) {
        scope.gpuTime = pool.getResult(scope.startQuery, scope.endQuery);
        data.totalGpuTime += scope.gpuTime;
    }
    
    // 记录历史
    gpuTimeHistory_[historyIndex_] = data.totalGpuTime;
    cpuTimeHistory_[historyIndex_] = data.totalCpuTime;
    historyIndex_ = (historyIndex_ + 1) % FRAME_HISTORY_SIZE;
}

void GPUProfiler::beginScope(VkCommandBuffer cmd, const std::string& name) {
    auto& pool = queryPools_[currentFrame_];
    auto& data = frameData_[currentFrame_];
    
    u32 queryStart = pool.allocateQueryPair();
    if (queryStart == UINT32_MAX) return;
    
    ProfileScope scope;
    scope.name = name;
    scope.depth = static_cast<u32>(scopeStack_.size());
    scope.startQuery = queryStart;
    scope.endQuery = queryStart + 1;
    
    u32 scopeIndex = static_cast<u32>(data.scopes.size());
    data.scopes.push_back(scope);
    
    // 添加到父范围
    if (!scopeStack_.empty()) {
        data.scopes[scopeStack_.back()].children.push_back(scopeIndex);
    } else {
        data.rootScopes.push_back(scopeIndex);
    }
    
    scopeStack_.push_back(scopeIndex);
    
    // 记录开始时间戳
    pool.writeTimestamp(cmd, queryStart, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

void GPUProfiler::endScope(VkCommandBuffer cmd) {
    if (scopeStack_.empty()) return;
    
    auto& pool = queryPools_[currentFrame_];
    
    u32 scopeIndex = scopeStack_.back();
    scopeStack_.pop_back();
    
    auto& scope = frameData_[currentFrame_].scopes[scopeIndex];
    
    // 记录结束时间戳
    pool.writeTimestamp(cmd, scope.endQuery, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

const FrameProfileData& GPUProfiler::getLastFrameData() const {
    return frameData_[(currentFrame_ + framesInFlight_ - 1) % framesInFlight_];
}

f64 GPUProfiler::getAverageGpuTime() const {
    f64 sum = 0.0;
    for (f64 t : gpuTimeHistory_) {
        sum += t;
    }
    return sum / FRAME_HISTORY_SIZE;
}

f64 GPUProfiler::getAverageCpuTime() const {
    f64 sum = 0.0;
    for (f64 t : cpuTimeHistory_) {
        sum += t;
    }
    return sum / FRAME_HISTORY_SIZE;
}

f64 GPUProfiler::getAverageFPS() const {
    f64 avgFrameTime = getAverageGpuTime() + getAverageCpuTime();
    if (avgFrameTime > 0) {
        return 1000.0 / avgFrameTime;
    }
    return 0.0;
}

void GPUProfiler::printStats() const {
    const auto& data = getLastFrameData();
    
    std::cout << "=== GPU Profiler Stats ===" << std::endl;
    std::cout << "Frame " << data.frameIndex << std::endl;
    std::cout << "GPU Time: " << std::fixed << std::setprecision(2) 
              << data.totalGpuTime << " ms" << std::endl;
    std::cout << "CPU Time: " << data.totalCpuTime << " ms" << std::endl;
    std::cout << "FPS: " << std::setprecision(1) << getAverageFPS() << std::endl;
    
    // 打印范围树
    std::cout << "\n--- Scope Breakdown ---" << std::endl;
    
    std::function<void(u32, const std::string&)> printScope = 
        [&](u32 index, const std::string& indent) {
            const auto& scope = data.scopes[index];
            
            std::cout << indent << scope.name 
                      << " - GPU: " << std::setprecision(2) << scope.gpuTime << " ms"
                      << " CPU: " << scope.cpuTime << " ms"
                      << std::endl;
            
            for (u32 child : scope.children) {
                printScope(child, indent + "  ");
            }
        };
    
    for (u32 root : data.rootScopes) {
        printScope(root, "");
    }
}

const ProfileScope* GPUProfiler::findScope(const std::string& name) const {
    const auto& data = getLastFrameData();
    for (const auto& scope : data.scopes) {
        if (scope.name == name) {
            return &scope;
        }
    }
    return nullptr;
}

f64 GPUProfiler::getScopeGpuTime(const std::string& name) const {
    auto* scope = findScope(name);
    return scope ? scope->gpuTime : 0.0;
}

f64 GPUProfiler::getScopeCpuTime(const std::string& name) const {
    auto* scope = findScope(name);
    return scope ? scope->cpuTime : 0.0;
}

// ============================================================================
// ScopedProfile Implementation
// ============================================================================

ScopedProfile::ScopedProfile(GPUProfiler* profiler, VkCommandBuffer cmd, const std::string& name)
    : profiler_(profiler), cmd_(cmd) {
    if (profiler_) {
        profiler_->beginScope(cmd_, name);
    }
}

ScopedProfile::~ScopedProfile() {
    if (profiler_) {
        profiler_->endScope(cmd_);
    }
}

// ============================================================================
// ProfilerHUD Implementation
// ============================================================================

ProfilerHUD::ProfilerHUD() = default;

ProfilerHUD::~ProfilerHUD() = default;

void ProfilerHUD::initialize(GPUProfiler* profiler) {
    profiler_ = profiler;
}

void ProfilerHUD::shutdown() {
    profiler_ = nullptr;
}

void ProfilerHUD::setConfig(const ProfilerHUDConfig& config) {
    config_ = config;
}

void ProfilerHUD::render(VkCommandBuffer cmd) {
    if (!visible_ || !profiler_) return;
    
    // 实际渲染需要使用文本渲染系统
    // 这里只是打印到控制台
    
    static u32 frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        profiler_->printStats();
    }
}

// ============================================================================
// MemoryStats Implementation
// ============================================================================

void MemoryStats::print() const {
    std::cout << "=== Memory Stats ===" << std::endl;
    std::cout << "Total Allocated: " << (totalAllocated / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Total Used: " << (totalUsed / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Total Free: " << (totalFree / 1024 / 1024) << " MB" << std::endl;
    std::cout << "Allocations: " << allocationCount << std::endl;
    std::cout << "Blocks: " << blockCount << std::endl;
    
    for (size_t i = 0; i < heaps.size(); i++) {
        const auto& heap = heaps[i];
        std::cout << "Heap " << i << ": " 
                  << (heap.size / 1024 / 1024) << " MB, "
                  << "Used: " << (heap.used / 1024 / 1024) << " MB, "
                  << "Allocs: " << heap.allocCount
                  << std::endl;
    }
}

// ============================================================================
// MemoryProfiler Implementation
// ============================================================================

MemoryStats MemoryProfiler::getStats(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator) {
    MemoryStats stats;
    
    // VMA 统计 API 在某些版本中可能不可用
    // 使用简化的实现
    
    // 获取内存堆信息
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    
    for (u32 i = 0; i < memProps.memoryHeapCount; i++) {
        MemoryStats::HeapStats heap;
        heap.size = memProps.memoryHeaps[i].size;
        heap.used = 0;
        heap.allocCount = 0;
        heap.flags = memProps.memoryHeaps[i].flags;
        stats.heaps.push_back(heap);
    }
    
    return stats;
}

void MemoryProfiler::printMemoryBudget(VmaAllocator allocator) {
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
    vmaGetHeapBudgets(allocator, budgets);
    
    std::cout << "=== Memory Budget ===" << std::endl;
    
    VkPhysicalDeviceMemoryProperties props;
    // 需要 physicalDevice
    // vmaGetMemoryProperties(allocator, &props);
    
    for (u32 i = 0; i < VK_MAX_MEMORY_HEAPS; i++) {
        if (budgets[i].budget > 0) {
            std::cout << "Heap " << i << ": "
                      << "Budget: " << (budgets[i].budget / 1024 / 1024) << " MB, "
                      << "Usage: " << (budgets[i].usage / 1024 / 1024) << " MB"
                      << std::endl;
        }
    }
}

// ============================================================================
// PipelineStats Implementation
// ============================================================================

void PipelineStats::print() const {
    std::cout << "=== Pipeline Stats ===" << std::endl;
    std::cout << "Draw Calls: " << drawCalls << std::endl;
    std::cout << "Triangles: " << triangleCount << std::endl;
    std::cout << "Vertices: " << vertexCount << std::endl;
    std::cout << "Instances: " << instanceCount << std::endl;
    std::cout << "Pipeline Binds: " << pipelineBinds << std::endl;
    std::cout << "Descriptor Binds: " << descriptorSetBinds << std::endl;
    std::cout << "Texture Binds: " << textureBinds << std::endl;
    std::cout << "Dispatch Calls: " << dispatchCalls << std::endl;
    
    f64 bufferMB = static_cast<f64>(bufferMemory) / 1024.0 / 1024.0;
    f64 imageMB = static_cast<f64>(imageMemory) / 1024.0 / 1024.0;
    
    std::cout << "Buffer Memory: " << std::fixed << std::setprecision(2) << bufferMB << " MB" << std::endl;
    std::cout << "Image Memory: " << imageMB << " MB" << std::endl;
    std::cout << "Buffers: " << bufferCount << std::endl;
    std::cout << "Images: " << imageCount << std::endl;
}

} // namespace Nova

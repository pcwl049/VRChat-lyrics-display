/**
 * Nova Renderer - GPU Profiler
 * GPU 性能分析系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <array>

// VMA 前向声明
typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace Nova {

class VulkanBackend;

// ============================================================================
// 时间查询
// ============================================================================

// 单个时间查询
struct TimestampQuery {
    std::string name;
    u32 startQueryIndex;
    u32 endQueryIndex;
    f64 gpuTimeMs;
    f64 cpuTimeMs;
    std::chrono::high_resolution_clock::time_point cpuStart;
    std::chrono::high_resolution_clock::time_point cpuEnd;
};

// 查询池
class TimestampQueryPool {
public:
    TimestampQueryPool();
    ~TimestampQueryPool();
    
    bool initialize(VkDevice device, u32 queueFamilyIndex, u32 maxQueries = 4096);
    void shutdown();
    
    // 分配查询索引对
    u32 allocateQueryPair();
    
    // 记录时间戳
    void writeTimestamp(VkCommandBuffer cmd, u32 queryIndex, VkPipelineStageFlagBits stage);
    
    // 获取结果
    f64 getResult(u32 startQuery, u32 endQuery);
    
    // 重置
    void reset(VkCommandBuffer cmd);
    
    VkQueryPool getPool() const { return queryPool_; }
    u32 getQueryCount() const { return queryCount_; }
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    u32 maxQueries_ = 4096;
    u32 queryCount_ = 0;
    f64 timestampPeriod_ = 1.0;  // 纳秒
};

// ============================================================================
// GPU 分析器
// ============================================================================

// 分析范围
struct ProfileScope {
    std::string name;
    u32 depth;
    u32 startQuery;
    u32 endQuery;
    f64 gpuTime;
    f64 cpuTime;
    std::vector<u32> children;
};

// 帧分析数据
struct FrameProfileData {
    u32 frameIndex;
    f64 totalGpuTime;
    f64 totalCpuTime;
    std::vector<ProfileScope> scopes;
    std::vector<u32> rootScopes;
    
    void reset() {
        scopes.clear();
        rootScopes.clear();
        totalGpuTime = 0;
        totalCpuTime = 0;
    }
};

// GPU 分析器
class GPUProfiler {
public:
    GPUProfiler();
    ~GPUProfiler();
    
    bool initialize(VulkanBackend* backend, u32 framesInFlight = 3);
    void shutdown();
    
    // 每帧开始/结束
    void beginFrame(VkCommandBuffer cmd);
    void endFrame(VkCommandBuffer cmd);
    
    // 范围分析
    void beginScope(VkCommandBuffer cmd, const std::string& name);
    void endScope(VkCommandBuffer cmd);
    
    // 获取结果
    const FrameProfileData& getLastFrameData() const;
    f64 getAverageGpuTime() const;
    f64 getAverageCpuTime() const;
    f64 getAverageFPS() const;
    
    // 输出到控制台
    void printStats() const;
    
    // 获取详细数据
    const ProfileScope* findScope(const std::string& name) const;
    f64 getScopeGpuTime(const std::string& name) const;
    f64 getScopeCpuTime(const std::string& name) const;
    
private:
    VulkanBackend* backend_ = nullptr;
    
    std::vector<TimestampQueryPool> queryPools_;
    std::vector<FrameProfileData> frameData_;
    
    u32 currentFrame_ = 0;
    u32 framesInFlight_ = 3;
    
    std::vector<u32> scopeStack_;  // 当前活动范围栈
    
    // 统计
    static constexpr u32 FRAME_HISTORY_SIZE = 60;
    std::array<f64, FRAME_HISTORY_SIZE> gpuTimeHistory_;
    std::array<f64, FRAME_HISTORY_SIZE> cpuTimeHistory_;
    u32 historyIndex_ = 0;
};

// ============================================================================
// 性能统计 HUD
// ============================================================================

struct ProfilerHUDConfig {
    bool showFPS = true;
    bool showFrameTime = true;
    bool showGpuTime = true;
    bool showCpuTime = true;
    bool showMemoryUsage = true;
    bool showPipelineStats = true;
    bool showDetailedBreakdown = false;
    
    f32 positionX = 10.0f;
    f32 positionY = 10.0f;
    f32 fontSize = 14.0f;
    f32 lineHeight = 18.0f;
    Color backgroundColor = Color::fromHex(0x000000AA);
    Color textColor = Color::white();
};

class ProfilerHUD {
public:
    ProfilerHUD();
    ~ProfilerHUD();
    
    void initialize(GPUProfiler* profiler);
    void shutdown();
    
    void render(VkCommandBuffer cmd);
    
    void setConfig(const ProfilerHUDConfig& config);
    ProfilerHUDConfig& getConfig() { return config_; }
    
    void toggle() { visible_ = !visible_; }
    bool isVisible() const { return visible_; }
    
private:
    GPUProfiler* profiler_ = nullptr;
    ProfilerHUDConfig config_;
    bool visible_ = true;
};

// ============================================================================
// 自动范围宏
// ============================================================================

class ScopedProfile {
public:
    ScopedProfile(GPUProfiler* profiler, VkCommandBuffer cmd, const std::string& name);
    ~ScopedProfile();
    
private:
    GPUProfiler* profiler_;
    VkCommandBuffer cmd_;
};

// 便捷宏
#define NOVA_PROFILE_SCOPE(profiler, cmd, name) \
    ScopedProfile _profile_scope_##__LINE__(profiler, cmd, name)

// ============================================================================
// 内存统计
// ============================================================================

struct MemoryStats {
    u64 totalAllocated = 0;
    u64 totalUsed = 0;
    u64 totalFree = 0;
    u32 allocationCount = 0;
    u32 blockCount = 0;
    
    struct HeapStats {
        u64 size;
        u64 used;
        u32 allocCount;
        VkMemoryPropertyFlags flags;
    };
    
    std::vector<HeapStats> heaps;
    
    void print() const;
};

class MemoryProfiler {
public:
    static MemoryStats getStats(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator);
    static void printMemoryBudget(VmaAllocator allocator);
};

// ============================================================================
// 管线统计
// ============================================================================

struct PipelineStats {
    u32 drawCalls = 0;
    u32 pipelineBinds = 0;
    u32 descriptorSetBinds = 0;
    u32 vertexBufferBinds = 0;
    u32 indexBufferBinds = 0;
    u32 textureBinds = 0;
    u32 vertexCount = 0;
    u32 triangleCount = 0;
    u32 instanceCount = 0;
    
    // Compute
    u32 dispatchCalls = 0;
    u32 workgroupCount = 0;
    
    // Memory
    u64 bufferMemory = 0;
    u64 imageMemory = 0;
    u32 bufferCount = 0;
    u32 imageCount = 0;
    
    void reset() {
        drawCalls = 0;
        pipelineBinds = 0;
        descriptorSetBinds = 0;
        vertexBufferBinds = 0;
        indexBufferBinds = 0;
        textureBinds = 0;
        vertexCount = 0;
        triangleCount = 0;
        instanceCount = 0;
        dispatchCalls = 0;
        workgroupCount = 0;
    }
    
    void print() const;
};

} // namespace Nova

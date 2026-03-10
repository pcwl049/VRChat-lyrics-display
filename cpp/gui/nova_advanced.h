/**
 * Nova Renderer - Advanced Optimization Modules
 * 极致优化模块集合 v1.0
 * 
 * 包含:
 * 1. GPU 驱动布局引擎 (Flexbox/Grid)
 * 2. 路径渲染优化 (Loop-Blinn)
 * 3. 多线程命令录制
 * 4. 渲染图系统
 * 5. 纹理压缩与流式加载
 * 6. 实例化渲染
 * 7. GPU 粒子系统
 * 8. 异步计算队列
 * 9. 内存管理优化
 * 10. 无锁内存分配器
 * 11. GPU 性能分析
 * 12. 延迟渲染 UI
 */

#pragma once

#include "vulkan_renderer.h"
#include <atomic>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <functional>
#include <array>
#include <bitset>

namespace Nova {
namespace Advanced {

// ============================================================================
// 1. GPU 驱动布局引擎
// ============================================================================

namespace Layout {

// Flexbox 属性
enum class FlexDirection : u32 {
    Row = 0,
    RowReverse = 1,
    Column = 2,
    ColumnReverse = 3
};

enum class JustifyContent : u32 {
    FlexStart = 0,
    FlexEnd = 1,
    Center = 2,
    SpaceBetween = 3,
    SpaceAround = 4,
    SpaceEvenly = 5
};

enum class AlignItems : u32 {
    Stretch = 0,
    FlexStart = 1,
    FlexEnd = 2,
    Center = 3,
    Baseline = 4
};

enum class FlexWrap : u32 {
    NoWrap = 0,
    Wrap = 1,
    WrapReverse = 2
};

// 布局节点 (GPU 可读格式)
struct LayoutNodeGPU {
    // 输入属性 (32 bytes)
    Vec2 position;         // 8
    Vec2 size;             // 8
    Vec2 minSize;          // 8
    Vec2 maxSize;          // 8
    
    // Flex 属性 (32 bytes)
    f32 flexGrow;          // 4
    f32 flexShrink;        // 4
    f32 flexBasis;         // 4
    f32 aspectRatio;       // 4
    Vec4 margin;           // 16 (top, right, bottom, left)
    
    // 对齐属性 (16 bytes)
    FlexDirection direction;      // 4
    JustifyContent justifyContent; // 4
    AlignItems alignItems;        // 4
    FlexWrap wrap;                // 4
    
    // 其他 (16 bytes)
    Vec4 padding;          // 16 (top, right, bottom, left)
    
    // 索引 (8 bytes)
    u32 firstChild;        // 4
    u32 childCount;        // 4
    
    // 输出 (16 bytes)
    Vec2 computedPosition; // 8
    Vec2 computedSize;     // 8
    
    u32 padding2[2];       // 8 (对齐到 128 bytes)
};

static_assert(sizeof(LayoutNodeGPU) == 128, "LayoutNodeGPU must be 128 bytes");

// Grid 布局属性
struct GridTrack {
    f32 size;              // 绝对尺寸或比例
    f32 minSize;
    f32 maxSize;
    u32 isFractional;      // 1 = fr 单位, 0 = px
};

struct GridItemGPU {
    // 输入
    u32 columnStart;
    u32 columnEnd;
    u32 rowStart;
    u32 rowEnd;
    
    // 输出
    Vec2 computedPosition;
    Vec2 computedSize;
};

// GPU 布局引擎
class GPULayoutEngine {
public:
    GPULayoutEngine(VulkanBackend* backend);
    ~GPULayoutEngine();
    
    // 创建布局树
    BufferHandle createLayoutTree(u32 maxNodes);
    void destroyLayoutTree();
    
    // 更新节点 (CPU -> GPU)
    void updateNode(u32 index, const LayoutNodeGPU& node);
    void updateNodes(u32 startIndex, u32 count, const LayoutNodeGPU* nodes);
    
    // 执行布局计算 (GPU)
    void computeLayout();
    
    // 读取结果 (GPU -> CPU)
    void readResults(std::vector<LayoutNodeGPU>& outNodes);
    
    // 获取缓冲区
    BufferHandle getNodeBuffer() const { return nodeBuffer_; }
    
private:
    VulkanBackend* backend_;
    BufferHandle nodeBuffer_;       // 节点数据
    BufferHandle resultBuffer_;     // 计算结果
    PipelineHandle flexPipeline_;   // Flexbox 计算着色器
    PipelineHandle gridPipeline_;   // Grid 计算着色器
    
    u32 maxNodes_;
    
    bool createPipelines();
};

} // namespace Layout

// ============================================================================
// 2. 路径渲染优化 (Loop-Blinn GPU 曲线)
// ============================================================================

namespace Path {

// 曲线类型
enum class CurveType : u32 {
    Line = 0,
    QuadraticBezier = 1,
    CubicBezier = 2,
    EllipticArc = 3
};

// 路径命令
struct PathCommand {
    CurveType type;
    Vec2 p0;           // 起点
    Vec2 p1;           // 控制点1 (或终点 for Line)
    Vec2 p2;           // 控制点2 (Cubic)
    Vec2 p3;           // 终点 (Cubic)
    f32 strokeWidth;
    Color fillColor;
    Color strokeColor;
};

// 路径顶点 (GPU 格式)
struct PathVertex {
    Vec2 position;
    Vec2 uv;           // 用于曲线参数化
    u32 pathIndex;
    u32 flags;         // 位标志
    Color fillColor;
};

// GPU 路径渲染器
class GPUPathRenderer {
public:
    GPUPathRenderer(VulkanBackend* backend);
    ~GPUPathRenderer();
    
    // 创建路径缓冲区
    void beginPath(u32 maxCommands);
    void addCommand(const PathCommand& cmd);
    void endPath();
    
    // 渲染
    void render(CommandBuffer* cmd, const Mat3& transform);
    
    // 填充模式
    enum class FillMode {
        NonZero,    // 非零环绕
        EvenOdd     // 奇偶规则
    };
    void setFillMode(FillMode mode) { fillMode_ = mode; }
    
    // 抗锯齿
    void setAntiAliasing(bool enabled) { antiAlias_ = enabled; }
    
private:
    VulkanBackend* backend_;
    
    BufferHandle vertexBuffer_;
    BufferHandle indexBuffer_;
    PipelineHandle fillPipeline_;
    PipelineHandle strokePipeline_;
    
    std::vector<PathCommand> commands_;
    std::vector<PathVertex> vertices_;
    std::vector<u32> indices_;
    
    FillMode fillMode_ = FillMode::NonZero;
    bool antiAlias_ = true;
    
    bool createPipelines();
    void tesselatePath(const PathCommand& cmd, u32 pathIndex);
};

// SVG 路径解析器
class SVGPathParser {
public:
    static std::vector<PathCommand> parse(const char* svgPath);
    
private:
    static Vec2 parseCoord(const char*& ptr);
    static void parseMoveTo(const char*& ptr, std::vector<PathCommand>& cmds, Vec2& cursor);
    static void parseLineTo(const char*& ptr, std::vector<PathCommand>& cmds, Vec2& cursor);
    static void parseCurveTo(const char*& ptr, std::vector<PathCommand>& cmds, Vec2& cursor);
    static void parseArcTo(const char*& ptr, std::vector<PathCommand>& cmds, Vec2& cursor);
};

} // namespace Path

// ============================================================================
// 3. 多线程命令录制
// ============================================================================

namespace Threading {

// 命令缓冲池
class CommandBufferPool {
public:
    CommandBufferPool(VulkanBackend* backend, u32 poolSize, u32 threads);
    ~CommandBufferPool();
    
    // 获取/归还命令缓冲
    VkCommandBuffer acquire(u32 threadIndex);
    void release(VkCommandBuffer cmd);
    
    // 重置所有
    void resetAll();
    
private:
    VulkanBackend* backend_;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers_;
    std::vector<std::vector<VkCommandBuffer>> threadBuckets_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

// 工作线程
struct RenderWork {
    std::function<void(VkCommandBuffer)> task;
    u32 layerStart;
    u32 layerEnd;
    std::atomic<bool> done{false};
};

// 渲染线程池
class RenderThreadPool {
public:
    RenderThreadPool(VulkanBackend* backend, u32 threadCount);
    ~RenderThreadPool();
    
    // 提交工作
    void submit(std::vector<RenderWork>& work);
    
    // 等待完成
    void waitAll();
    
    // 获取次级命令缓冲 (用于 vkCmdExecuteCommands)
    std::vector<VkCommandBuffer> collectCommandBuffers();
    
private:
    void workerThread(u32 index);
    
    VulkanBackend* backend_;
    CommandBufferPool* cmdPool_;
    
    std::vector<std::thread> threads_;
    std::vector<VkCommandBuffer> threadCmds_;
    
    std::queue<RenderWork*> workQueue_;
    std::mutex queueMutex_;
    std::condition_variable workCv_;
    std::condition_variable doneCv_;
    
    std::atomic<bool> running_{true};
    std::atomic<u32> completedCount_{0};
    u32 totalWork_ = 0;
};

// 图层录制器接口
class LayerRecorder {
public:
    virtual ~LayerRecorder() = default;
    virtual void record(VkCommandBuffer cmd, u32 layerIndex) = 0;
    virtual Rect bounds() const = 0;
};

// 批量图层录制
class BatchLayerRecorder {
public:
    BatchLayerRecorder(RenderThreadPool* pool);
    
    // 添加图层
    void addLayer(LayerRecorder* recorder, u32 zOrder);
    
    // 按图层分组录制
    void recordAll();
    
    // 获取命令缓冲
    std::vector<VkCommandBuffer> getCommandBuffers();
    
private:
    RenderThreadPool* pool_;
    std::vector<std::pair<LayerRecorder*, u32>> layers_;
    std::vector<VkCommandBuffer> collectedCmds_;
};

} // namespace Threading

// ============================================================================
// 4. 渲染图系统
// ============================================================================

namespace RenderGraph {

// 资源类型
enum class ResourceType : u32 {
    Texture1D,
    Texture2D,
    Texture3D,
    Buffer
};

// 资源描述
struct ResourceDesc {
    std::string name;
    ResourceType type;
    u32 width, height, depth;
    VkFormat format;
    VkImageUsageFlags usage;
    bool transient = false;  // 是否为临时资源
};

// Pass 描述
struct PassDesc {
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::function<void(VkCommandBuffer)> execute;
    
    // 同步依赖
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkAccessFlags srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
    VkAccessFlags dstAccess = VK_ACCESS_MEMORY_READ_BIT;
};

// 资源别名
struct AliasRange {
    u64 offset;
    u64 size;
};

// 渲染图
class RenderGraph {
public:
    RenderGraph(VulkanBackend* backend);
    ~RenderGraph();
    
    // 构建图
    void beginFrame();
    void addPass(const PassDesc& pass);
    void addTransientResource(const ResourceDesc& desc);
    void endFrame();
    
    // 执行
    void execute(VkCommandBuffer cmd);
    
    // 获取资源
    TextureHandle getResource(const std::string& name);
    
    // 自动优化
    void enableAliasing(bool enable) { aliasingEnabled_ = enable; }
    void enableCulling(bool enable) { cullingEnabled_ = enable; }
    
private:
    VulkanBackend* backend_;
    
    struct Pass {
        PassDesc desc;
        std::vector<ResourceID> inputResources;
        std::vector<ResourceID> outputResources;
        bool culled = false;
    };
    
    struct Resource {
        ResourceDesc desc;
        ResourceID id;
        TextureHandle texture;
        VkImageLayout layout;
        VkPipelineStageFlags lastStage;
        VkAccessFlags lastAccess;
        bool firstUse = true;
    };
    
    std::unordered_map<std::string, Resource> resources_;
    std::vector<Pass> passes_;
    
    bool aliasingEnabled_ = true;
    bool cullingEnabled_ = true;
    
    // 拓扑排序
    void topologicalSort();
    
    // 资源别名分析
    void analyzeAliasing();
    
    // 无用 Pass 裁剪
    void cullPasses();
    
    // 插入同步屏障
    void insertBarriers(VkCommandBuffer cmd, Pass& pass);
    
    // 计算资源生命周期
    std::pair<u32, u32> getResourceLifetime(ResourceID id);
};

// 常用 Pass 模板
namespace Passes {
    PassDesc createClearPass(const std::string& output, const Color& color);
    PassDesc createBlitPass(const std::string& input, const std::string& output);
    PassDesc createBlurPass(const std::string& input, const std::string& temp, const std::string& output, f32 radius);
    PassDesc createBloomPass(const std::string& input, const std::string& bright, const std::string& output, f32 threshold, f32 intensity);
    PassDesc createCompositePass(const std::vector<std::string>& inputs, const std::string& output);
}

} // namespace RenderGraph

// ============================================================================
// 5. 纹理压缩与流式加载 (虚拟纹理)
// ============================================================================

namespace TextureStreaming {

// 虚拟纹理页面
struct Page {
    u32 x, y;              // 页面坐标
    u32 mipLevel;          // Mip 级别
    u64 lastAccessTime;    // LRU 时间戳
    bool resident;         // 是否驻留
    bool dirty;            // 是否需要更新
};

// 页面表 (GPU)
struct PageTableEntry {
    u32 cacheX : 10;       // 缓存纹理中的位置
    u32 cacheY : 10;
    u32 cacheMip : 4;      // 缓存中的 mip 级别
    u32 resident : 1;      // 是否驻留
    u32 padding : 7;
};

// 虚拟纹理
class VirtualTexture {
public:
    VirtualTexture(VulkanBackend* backend, u32 virtualSize, u32 pageSize, u32 cacheSize);
    ~VirtualTexture();
    
    // 请求页面
    void requestPage(u32 x, u32 y, u32 mipLevel);
    
    // 更新缓存
    void updateCache();
    
    // 获取页面表纹理
    TextureHandle getPageTableTexture() const { return pageTableTexture_; }
    
    // 获取缓存纹理
    TextureHandle getCacheTexture() const { return cacheTexture_; }
    
    // 绑定到着色器
    void bind(DescriptorSet* set, u32 binding);
    
    // 统计
    struct Stats {
        u32 totalPages;
        u32 residentPages;
        u32 cacheMisses;
        u32 cacheHits;
        f32 memoryUsageMB;
    };
    Stats getStats() const;
    
private:
    VulkanBackend* backend_;
    
    u32 virtualSize_;      // 虚拟纹理大小 (如 16384)
    u32 pageSize_;         // 页面大小 (如 128)
    u32 cacheSize_;        // 缓存大小 (如 2048)
    
    TextureHandle pageTableTexture_;   // 页面表纹理
    TextureHandle cacheTexture_;       // 物理缓存纹理
    
    std::vector<Page> pages_;
    std::list<Page*> lruList_;         // LRU 列表
    
    BufferHandle pageTableBuffer_;     // CPU 页面表
    
    void loadPage(Page* page);
    void evictPage(Page* page);
    Page* findPageToEvict();
};

// 纹理压缩器
class TextureCompressor {
public:
    enum class Format {
        BC7,      // 高质量
        BC6H,     // HDR
        BC1,      // DXT1
        BC3,      // DXT5
        ASTC_4x4,
        ASTC_8x8
    };
    
    TextureCompressor(VulkanBackend* backend);
    ~TextureCompressor();
    
    // 压缩纹理
    void compress(TextureHandle srcTexture, TextureHandle dstTexture, Format format);
    
    // CPU 压缩 (用于预计算)
    static std::vector<u8> compressBC7(const u8* rgba, u32 width, u32 height);
    static std::vector<u8> compressBC6H(const f32* rgb, u32 width, u32 height);
    static std::vector<u8> compressASTC(const u8* rgba, u32 width, u32 height, u32 blockX, u32 blockY);
    
private:
    VulkanBackend* backend_;
    PipelineHandle bc7Pipeline_;
    PipelineHandle bc6hPipeline_;
};

// 异步纹理加载器
class AsyncTextureLoader {
public:
    AsyncTextureLoader(VulkanBackend* backend, u32 maxInFlight = 4);
    ~AsyncTextureLoader();
    
    // 请求加载
    using Callback = std::function<void(TextureHandle)>;
    void requestLoad(const std::string& path, TextureHandle texture, Callback callback);
    
    // 更新 (主线程调用)
    void update();
    
    // 等待所有完成
    void waitAll();
    
private:
    void loaderThread();
    
    struct LoadRequest {
        std::string path;
        TextureHandle texture;
        Callback callback;
        std::vector<u8> data;
        std::atomic<bool> done{false};
    };
    
    VulkanBackend* backend_;
    std::thread loaderThread_;
    std::queue<LoadRequest*> pendingQueue_;
    std::vector<LoadRequest*> completed_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
};

} // namespace TextureStreaming

// ============================================================================
// 6. 实例化渲染 (超大规模 UI 批处理)
// ============================================================================

namespace Instancing {

// 实例数据 (64 bytes, 对齐 GPU)
struct InstanceData {
    Vec2 position;         // 8
    Vec2 size;             // 8
    Vec4 uvRect;           // 16 (x, y, width, height)
    Color color;           // 16 (r, g, b, a)
    f32 cornerRadius;      // 4
    f32 blurAmount;        // 4
    f32 strokeWidth;       // 4
    u32 flags;             // 4
};

static_assert(sizeof(InstanceData) == 64, "InstanceData must be 64 bytes");

// 批处理
struct InstanceBatch {
    TextureHandle texture;
    PipelineHandle pipeline;
    u32 startIndex;
    u32 count;
    Rect bounds;
};

// 实例化渲染器
class InstancedRenderer {
public:
    InstancedRenderer(VulkanBackend* backend, u32 maxInstances = 100000);
    ~InstancedRenderer();
    
    // 开始收集
    void begin();
    
    // 添加实例
    void addRect(const Rect& rect, const Color& color);
    void addRoundRect(const Rect& rect, f32 radius, const Color& color);
    void addTextureRect(const Rect& rect, TextureHandle texture, const Color& tint = Color::white());
    void addTexturedRect(const Rect& rect, TextureHandle texture, const Rect& uvRect, const Color& color);
    
    // 结束收集，生成批次
    void end();
    
    // 渲染
    void render(VkCommandBuffer cmd);
    
    // 统计
    u32 getInstanceCount() const { return instanceCount_; }
    u32 getBatchCount() const { return batches_.size(); }
    u32 getDrawCallCount() const;
    
    // 动态批处理设置
    void setMaxBatchSize(u32 size) { maxBatchSize_ = size; }
    void setSorting(bool enable) { sortingEnabled_ = enable; }
    
private:
    VulkanBackend* backend_;
    
    BufferHandle vertexBuffer_;      // 单位四边形顶点
    BufferHandle instanceBuffer_;    // 实例数据
    BufferHandle indexBuffer_;       // 索引
    
    std::vector<InstanceData> instances_;
    std::vector<InstanceBatch> batches_;
    
    PipelineHandle rectPipeline_;
    PipelineHandle roundRectPipeline_;
    PipelineHandle texturedPipeline_;
    
    u32 maxInstances_;
    u32 instanceCount_ = 0;
    u32 maxBatchSize_ = 1000;
    bool sortingEnabled_ = true;
    
    bool createBuffers();
    bool createPipelines();
    void sortInstances();
    void createBatches();
};

// 图集管理器 (减少纹理切换)
class TextureAtlas {
public:
    TextureAtlas(VulkanBackend* backend, u32 size = 4096);
    ~TextureAtlas();
    
    // 添加纹理到图集
    struct Allocation {
        Vec4 uvRect;          // 在图集中的位置
        bool success;
    };
    Allocation allocate(u32 width, u32 height);
    void upload(u32 x, u32 y, u32 width, u32 height, const u8* data);
    
    // 获取图集纹理
    TextureHandle getTexture() const { return atlasTexture_; }
    SamplerHandle getSampler() const { return atlasSampler_; }
    
    // 清空图集
    void clear();
    
private:
    VulkanBackend* backend_;
    TextureHandle atlasTexture_;
    SamplerHandle atlasSampler_;
    
    u32 size_;
    u32 currentX_ = 0;
    u32 currentY_ = 0;
    u32 rowHeight_ = 0;
    
    // 空闲区域列表 (用于碎片整理)
    std::vector<Rect> freeRegions_;
};

} // namespace Instancing

// ============================================================================
// 7. GPU 粒子系统
// ============================================================================

namespace Particles {

// 粒子数据 (GPU 格式)
struct ParticleData {
    Vec3 position;         // 12
    f32 life;              // 4
    Vec3 velocity;         // 12
    f32 size;              // 4
    Vec4 color;            // 16
    Vec3 acceleration;     // 12
    f32 rotation;          // 4
    f32 angularVelocity;   // 4
    f32 drag;              // 4
    u32 flags;             // 4
    u32 padding[2];        // 8
};

static_assert(sizeof(ParticleData) == 80, "ParticleData must be 80 bytes");

// 发射器配置
struct EmitterConfig {
    Vec3 position;
    Vec3 direction;
    f32 spread;
    
    u32 emissionRate;      // 每秒发射数量
    u32 maxParticles;
    
    f32 minLife;
    f32 maxLife;
    
    f32 minSize;
    f32 maxSize;
    
    f32 minSpeed;
    f32 maxSpeed;
    
    Color startColor;
    Color endColor;
    
    f32 gravity;
    f32 drag;
    
    bool worldSpace;
    bool inheritVelocity;
};

// GPU 粒子系统
class GPUParticleSystem {
public:
    GPUParticleSystem(VulkanBackend* backend, u32 maxParticles = 100000);
    ~GPUParticleSystem();
    
    // 创建发射器
    u32 createEmitter(const EmitterConfig& config);
    void destroyEmitter(u32 emitterId);
    
    // 更新发射器
    void setEmitterPosition(u32 emitterId, const Vec3& position);
    void setEmitterDirection(u32 emitterId, const Vec3& direction);
    void setEmitterActive(u32 emitterId, bool active);
    
    // 模拟 (GPU Compute)
    void simulate(f32 deltaTime);
    
    // 渲染
    void render(VkCommandBuffer cmd, const Mat4& viewProj);
    
    // 纹理
    void setParticleTexture(TextureHandle texture) { particleTexture_ = texture; }
    
    // 统计
    u32 getActiveParticleCount() const;
    
private:
    VulkanBackend* backend_;
    
    BufferHandle particleBuffer_;       // 双缓冲
    BufferHandle particleBuffer2_;
    BufferHandle emitterBuffer_;
    BufferHandle counterBuffer_;        // 原子计数器
    
    PipelineHandle updatePipeline_;     // 粒子更新计算着色器
    PipelineHandle emitPipeline_;       // 粒子发射计算着色器
    PipelineHandle renderPipeline_;     // 粒子渲染管线
    
    TextureHandle particleTexture_;
    
    std::vector<EmitterConfig> emitters_;
    std::vector<bool> emitterActive_;
    
    u32 maxParticles_;
    u32 currentBuffer_ = 0;
    
    bool createBuffers();
    bool createPipelines();
};

// 预设粒子效果
namespace Effects {
    EmitterConfig createFire(const Vec3& position);
    EmitterConfig createSmoke(const Vec3& position);
    EmitterConfig createExplosion(const Vec3& position);
    EmitterConfig createRain(const Vec3& position, f32 areaSize);
    EmitterConfig createSnow(const Vec3& position, f32 areaSize);
    EmitterConfig createSparkle(const Vec3& position, const Color& color);
}

} // namespace Particles

// ============================================================================
// 8. 异步计算队列
// ============================================================================

namespace AsyncCompute {

// 计算任务
struct ComputeTask {
    PipelineHandle pipeline;
    std::vector<std::pair<u32, BufferHandle>> buffers;
    std::vector<std::pair<u32, TextureHandle>> textures;
    u32 groupCountX, groupCountY, groupCountZ;
    std::function<void()> onComplete;
    
    // 同步
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
};

// 异步计算管理器
class AsyncComputeManager {
public:
    AsyncComputeManager(VulkanBackend* backend);
    ~AsyncComputeManager();
    
    // 提交计算任务
    void submit(ComputeTask&& task);
    
    // 执行队列 (与图形队列并行)
    void execute();
    
    // 同步点
    void syncToGraphics();   // 等待计算完成
    void syncFromGraphics(); // 等待图形完成
    
    // 获取队列
    VkQueue getComputeQueue() const { return computeQueue_; }
    VkCommandPool getCommandPool() const { return commandPool_; }
    
private:
    VulkanBackend* backend_;
    
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;
    
    VkSemaphore computeFinished_ = VK_NULL_HANDLE;
    VkSemaphore graphicsFinished_ = VK_NULL_HANDLE;
    
    std::queue<ComputeTask> taskQueue_;
    std::mutex mutex_;
    
    u32 currentCmdIndex_ = 0;
};

// 混合渲染示例 (图形 + 计算)
class HybridRenderer {
public:
    HybridRenderer(VulkanBackend* backend, AsyncComputeManager* compute);
    
    // 帧流程
    void beginFrame();
    void renderGeometry(VkCommandBuffer cmd);   // 图形队列
    void computeEffects();                       // 计算队列 (并行)
    void composite(VkCommandBuffer cmd);        // 合成
    void endFrame();
    
private:
    VulkanBackend* backend_;
    AsyncComputeManager* compute_;
    
    // 中间资源
    TextureHandle geometryBuffer_;
    TextureHandle bloomBuffer_;
    TextureHandle ssaoBuffer_;
    
    VkSemaphore geometryFinished_ = VK_NULL_HANDLE;
    VkSemaphore computeFinished_ = VK_NULL_HANDLE;
};

} // namespace AsyncCompute

// ============================================================================
// 9. 内存管理优化 (VMA 高级配置)
// ============================================================================

namespace Memory {

// 内存池类型
enum class PoolType : u32 {
    DeviceLocal,        // GPU 专用
    HostVisible,        // CPU 可访问
    DeviceLocalHostVisible,  // 集成显卡优化
    Staging,            // 上传缓冲
    Readback,           // 回读缓冲
    Dynamic,            // 高频更新
    Persistent,         // 持久映射
};

// 内存池配置
struct PoolConfig {
    PoolType type;
    u64 blockSize;       // 块大小
    u32 minBlockCount;
    u32 maxBlockCount;
    f32 preferredLargeHeapPercentage;  // 大堆百分比
    bool linearAllocation;  // 线性分配 (更快)
};

// 高级内存管理器
class MemoryManager {
public:
    MemoryManager(VulkanBackend* backend);
    ~MemoryManager();
    
    // 创建内存池
    u32 createPool(const PoolConfig& config);
    
    // 分配
    BufferHandle allocateBuffer(u32 poolId, const BufferDesc& desc);
    TextureHandle allocateTexture(u32 poolId, const TextureDesc& desc);
    
    // 释放
    void deallocate(BufferHandle handle);
    void deallocate(TextureHandle handle);
    
    // 碎片整理
    void defragment(u32 poolId);
    void defragmentAll();
    
    // 统计
    struct PoolStats {
        u64 usedBytes;
        u64 allocatedBytes;
        u32 blockCount;
        u32 allocationCount;
        f32 fragmentation;
    };
    PoolStats getPoolStats(u32 poolId);
    
    // 全局统计
    struct GlobalStats {
        u64 totalDeviceMemory;
        u64 usedDeviceMemory;
        u64 totalHostMemory;
        u64 usedHostMemory;
        u32 bufferCount;
        u32 textureCount;
    };
    GlobalStats getGlobalStats();
    
    // 内存预算
    void setBudget(u64 bytes);
    bool isOverBudget();
    
private:
    VulkanBackend* backend_;
    VmaAllocator allocator_;
    
    struct Pool {
        PoolConfig config;
        VmaPool pool = VK_NULL_HANDLE;
        std::vector<VmaAllocation> allocations;
    };
    
    std::unordered_map<u32, Pool> pools_;
    u32 nextPoolId_ = 1;
    
    // 资源追踪
    std::unordered_map<ResourceID, VmaAllocation> allocations_;
    
    // 驻留管理
    struct ResidencyInfo {
        u64 lastAccessTime;
        u64 size;
        bool resident;
    };
    std::unordered_map<ResourceID, ResidencyInfo> residency_;
    u64 budget_ = UINT64_MAX;
};

// 线性分配器 (快速临时分配)
class LinearAllocator {
public:
    LinearAllocator(VulkanBackend* backend, u64 size, PoolType type);
    ~LinearAllocator();
    
    // 分配 (无锁，O(1))
    u64 allocate(u64 size, u64 alignment = 256);
    
    // 重置 (一次性释放所有)
    void reset();
    
    // 获取缓冲区
    BufferHandle getBuffer() const { return buffer_; }
    void* getMappedPtr() const { return mapped_; }
    
private:
    VulkanBackend* backend_;
    BufferHandle buffer_;
    void* mapped_ = nullptr;
    
    std::atomic<u64> offset_{0};
    u64 capacity_;
};

// 环形缓冲区 (Uniform Buffer 专用)
class RingBuffer {
public:
    RingBuffer(VulkanBackend* backend, u64 size, u32 framesInFlight);
    ~RingBuffer();
    
    // 分配 (每帧重置)
    u64 allocate(u64 size, u64 alignment = 256);
    
    // 帧边界
    void nextFrame();
    
    // 获取
    BufferHandle getBuffer() const { return buffer_; }
    u64 getCurrentOffset() const { return frameStart_[currentFrame_]; }
    
private:
    VulkanBackend* backend_;
    BufferHandle buffer_;
    void* mapped_ = nullptr;
    
    u64 capacity_;
    u64 frameStart_[3];  // 最多 3 帧
    u64 currentOffset_ = 0;
    u32 currentFrame_ = 0;
    u32 framesInFlight_;
};

} // namespace Memory

// ============================================================================
// 10. 无锁内存分配器
// ============================================================================

namespace LockFree {

// 无锁池分配器
template<typename T, u32 BlockSize = 4096>
class LockFreePool {
public:
    LockFreePool() = default;
    ~LockFreePool();
    
    // 分配 (无锁)
    T* allocate();
    
    // 释放 (无锁)
    void deallocate(T* ptr);
    
    // 统计
    u32 size() const { return count_.load(); }
    u32 capacity() const { return capacity_.load(); }
    
private:
    struct Node {
        T data;
        std::atomic<Node*> next;
    };
    
    struct Block {
        Node nodes[BlockSize];
        Block* next;
    };
    
    std::atomic<Node*> freeList_{nullptr};
    std::atomic<u32> count_{0};
    std::atomic<u32> capacity_{0};
    
    std::vector<Block*> blocks_;
    std::mutex blockMutex_;  // 仅用于分配新块
    
    void allocateBlock();
};

// 无锁队列 (MPSC - 多生产者单消费者)
template<typename T>
class MPSCQueue {
public:
    MPSCQueue() {
        Node* dummy = new Node{};
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~MPSCQueue();
    
    // 入队 (多线程安全)
    void push(const T& value) {
        Node* node = new Node{value, nullptr};
        Node* prevHead = head_.exchange(node);
        prevHead->next.store(node);
    }
    
    // 出队 (单消费者)
    bool pop(T& out) {
        Node* tail = tail_.load();
        Node* next = tail->next.load();
        
        if (next == nullptr) return false;
        
        out = next->data;
        tail_.store(next);
        delete tail;
        return true;
    }
    
    bool empty() const {
        return tail_.load()->next.load() == nullptr;
    }
    
private:
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
};

// 无锁栈 (ABA 问题使用版本号解决)
template<typename T>
class LockFreeStack {
public:
    struct Node {
        T data;
        Node* next;
    };
    
    struct Head {
        Node* ptr;
        u64 version;
    };
    
    LockFreeStack() {
        static_assert(sizeof(Head) == 16, "Head must be 16 bytes");
    }
    
    void push(Node* node) {
        Head oldHead = head_.load();
        Head newHead;
        do {
            node->next = oldHead.ptr;
            newHead.ptr = node;
            newHead.version = oldHead.version + 1;
        } while (!head_.compare_exchange_weak(oldHead, newHead));
    }
    
    Node* pop() {
        Head oldHead = head_.load();
        Head newHead;
        do {
            if (oldHead.ptr == nullptr) return nullptr;
            newHead.ptr = oldHead.ptr->next;
            newHead.version = oldHead.version + 1;
        } while (!head_.compare_exchange_weak(oldHead, newHead));
        return oldHead.ptr;
    }
    
private:
    std::atomic<Head> head_{Head{nullptr, 0}};
};

} // namespace LockFree

// ============================================================================
// 11. GPU 性能分析
// ============================================================================

namespace Profiling {

// 时间戳查询
class GPUProfiler {
public:
    GPUProfiler(VulkanBackend* backend, u32 maxQueries = 1024);
    ~GPUProfiler();
    
    // 开始帧
    void beginFrame();
    
    // 时间戳
    void timestamp(VkCommandBuffer cmd, const char* name);
    
    // 范围查询
    void beginRange(VkCommandBuffer cmd, const char* name);
    void endRange(VkCommandBuffer cmd);
    
    // 结束帧
    void endFrame();
    
    // 获取结果
    struct RangeResult {
        std::string name;
        f64 gpuTimeMs;
        u64 startQuery;
        u64 endQuery;
    };
    std::vector<RangeResult> getResults();
    
    // 统计
    struct FrameStats {
        f64 totalGpuTime;
        f64 cpuTime;
        u32 drawCalls;
        u32 computeCalls;
        u32 memoryBarriers;
    };
    FrameStats getFrameStats();
    
    // 热力图
    void generateHeatmap(std::vector<u8>& outPixels, u32 width, u32 height);
    
private:
    VulkanBackend* backend_;
    VkQueryPool queryPool_ = VK_NULL_HANDLE;
    
    struct QueryEntry {
        std::string name;
        u32 startQuery;
        u32 endQuery;
        bool isRange;
    };
    
    std::vector<QueryEntry> pendingQueries_;
    std::vector<RangeResult> results_;
    
    u32 currentQuery_ = 0;
    u32 maxQueries_;
    
    // 统计
    u64 frameStartTime_ = 0;
    u32 drawCallCount_ = 0;
    u32 computeCallCount_ = 0;
};

// 自动范围计时
class ScopedGPUTimer {
public:
    ScopedGPUTimer(GPUProfiler* profiler, VkCommandBuffer cmd, const char* name);
    ~ScopedGPUTimer();
    
private:
    GPUProfiler* profiler_;
    VkCommandBuffer cmd_;
};

#define GPU_TIMESTAMP(cmd, profiler, name) \
    (profiler)->timestamp(cmd, name)

#define GPU_SCOPED_TIMER(cmd, profiler, name) \
    Nova::Advanced::Profiling::ScopedGPUTimer _gpuTimer_##__LINE__(profiler, cmd, name)

// CPU/GPU 同步分析
class SyncAnalyzer {
public:
    SyncAnalyzer(GPUProfiler* profiler);
    
    // 记录同步点
    void recordSync(const char* name);
    
    // 分析等待时间
    struct SyncPoint {
        std::string name;
        f64 cpuWaitTimeMs;
        f64 gpuIdleTimeMs;
    };
    std::vector<SyncPoint> analyzeSyncPoints();
    
private:
    GPUProfiler* profiler_;
    std::vector<std::pair<std::string, u64>> syncPoints_;
};

// 性能计数器
class PerformanceCounters {
public:
    void addCounter(const std::string& name);
    void increment(const std::string& name, u64 delta = 1);
    void set(const std::string& name, u64 value);
    u64 get(const std::string& name);
    
    void frame();
    
    struct CounterStats {
        u64 current;
        u64 average;
        u64 min;
        u64 max;
        u64 total;
    };
    CounterStats getStats(const std::string& name);
    
private:
    std::unordered_map<std::string, u64> counters_;
    std::unordered_map<std::string, CounterStats> stats_;
    std::vector<std::unordered_map<std::string, u64>> history_;
    u32 historyIndex_ = 0;
    static constexpr u32 HISTORY_SIZE = 60;
};

} // namespace Profiling

// ============================================================================
// 12. 延迟渲染 UI (G-Buffer)
// ============================================================================

namespace DeferredUI {

// G-Buffer 布局
struct GBuffer {
    TextureHandle albedoAlpha;    // RGB = Albedo, A = Alpha
    TextureHandle normalMaterial; // RG = Normal, BA = Material params
    TextureHandle depthStencil;   // R = Depth, G = Stencil
    
    // 清空
    void clear(VkCommandBuffer cmd, const Color& clearColor);
    
    // 判断是否有效
    bool valid() const {
        return albedoAlpha.valid() && normalMaterial.valid() && depthStencil.valid();
    }
};

// UI 光源
struct UILight {
    Vec2 position;
    f32 radius;
    Color color;
    f32 intensity;
};

// 延迟渲染器
class DeferredUIRenderer {
public:
    DeferredUIRenderer(VulkanBackend* backend, u32 width, u32 height);
    ~DeferredUIRenderer();
    
    // 调整大小
    void resize(u32 width, u32 height);
    
    // 几何阶段
    void beginGeometry(VkCommandBuffer cmd);
    void endGeometry(VkCommandBuffer cmd);
    
    // 添加 UI 元素
    void drawRect(const Rect& rect, const Color& color, const Vec2& normal = {0, 0});
    void drawRoundRect(const Rect& rect, f32 radius, const Color& color);
    void drawTexturedRect(const Rect& rect, TextureHandle texture, const Color& color);
    
    // 光照阶段
    void beginLighting(VkCommandBuffer cmd);
    void addLight(const UILight& light);
    void endLighting(VkCommandBuffer cmd);
    
    // 后处理
    void applyBloom(f32 threshold, f32 intensity);
    void applySSAO(f32 radius, f32 intensity);
    void applyColorCorrection(const Vec3& brightnessContrastSaturation);
    
    // 输出
    void composite(VkCommandBuffer cmd, TextureHandle output);
    
    // 获取 G-Buffer
    const GBuffer& getGBuffer() const { return gbuffer_; }
    
private:
    VulkanBackend* backend_;
    GBuffer gbuffer_;
    u32 width_, height_;
    
    // 几何阶段
    PipelineHandle geometryPipeline_;
    BufferHandle vertexBuffer_;
    
    // 光照阶段
    PipelineHandle lightingPipeline_;
    BufferHandle lightBuffer_;
    std::vector<UILight> lights_;
    
    // 后处理
    PipelineHandle bloomPipeline_;
    PipelineHandle ssaoPipeline_;
    PipelineHandle compositePipeline_;
    
    TextureHandle bloomTexture_;
    TextureHandle ssaoTexture_;
    
    bool createGBuffer();
    bool createPipelines();
};

// SDF 法线生成 (用于圆角等)
class SDFNormalGenerator {
public:
    // 圆角矩形的 SDF
    static f32 roundedRectSDF(const Vec2& p, const Vec2& halfSize, f32 radius);
    
    // SDF 法线
    static Vec2 sdfNormal(const Vec2& p, f32 (*sdf)(Vec2, Vec2, f32), 
                          const Vec2& halfSize, f32 radius);
    
    // 用于着色器
    static const char* getShaderCode();
};

} // namespace DeferredUI

} // namespace Advanced
} // namespace Nova

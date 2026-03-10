/**
 * Nova Renderer - Texture Streaming
 * 纹理流式加载系统 - 支持虚拟纹理、按需加载、LOD 管理
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>
#include <filesystem>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 虚拟纹理
// ============================================================================

// 虚拟纹理页
struct VirtualTexturePage {
    u32 x, y;           // 页面坐标
    u32 mipLevel;       // mip 级别
    u32 layer;          // 纹理层数组
    bool loaded;        // 是否已加载
    u32 lastAccessFrame; // 最后访问帧
    
    // GPU 内存
    TextureHandle physicalTexture;
    u32 physicalX, physicalY; // 在物理纹理中的位置
};

// 页表项
struct PageTableEntry {
    u32 physicalX : 12;
    u32 physicalY : 12;
    u32 valid : 1;
    u32 reserved : 7;
};

// 虚拟纹理配置
struct VirtualTextureDesc {
    u32 virtualWidth = 16384;       // 虚拟纹理宽度
    u32 virtualHeight = 16384;      // 虚拟纹理高度
    u32 pageSize = 256;             // 页面大小 (必须是 2 的幂)
    u32 mipLevels = 8;              // mip 级别数
    u32 physicalCacheSize = 4096;   // 物理缓存大小
    Format format = Format::RGBA8_UNORM;
};

// 虚拟纹理
class VirtualTexture {
public:
    VirtualTexture();
    ~VirtualTexture();
    
    bool initialize(VulkanBackend* backend, const VirtualTextureDesc& desc);
    void shutdown();
    
    // 请求页面
    void requestPage(u32 x, u32 y, u32 mipLevel, u32 layer = 0);
    
    // 更新
    void update(u32 currentFrame);
    
    // 获取页表纹理
    TextureHandle getPageTableTexture() const { return pageTableTexture_; }
    
    // 获取物理缓存纹理
    TextureHandle getPhysicalCacheTexture() const { return physicalCacheTexture_; }
    
    // 计算 UV 偏移
    Vec4 getPageTableUV(const Vec2& uv, u32 mipLevel) const;
    
private:
    u32 calculatePageCount() const;
    void loadPage(VirtualTexturePage& page);
    void evictPage(VirtualTexturePage& page);
    void updatePageTable();
    
    VulkanBackend* backend_ = nullptr;
    VirtualTextureDesc desc_;
    
    // 页面存储
    std::vector<std::vector<VirtualTexturePage>> pages_; // 按 mip 级别存储
    
    // 物理缓存
    TextureHandle physicalCacheTexture_;
    u32 physicalCachePagesX_ = 0;
    u32 physicalCachePagesY_ = 0;
    std::vector<bool> physicalCacheUsed_;
    
    // 页表
    TextureHandle pageTableTexture_;
    BufferHandle pageTableBuffer_;
    
    // LRU 队列
    std::queue<VirtualTexturePage*> lruQueue_;
    
    // 加载队列
    std::vector<VirtualTexturePage*> loadQueue_;
};

// ============================================================================
// 纹理流式加载器
// ============================================================================

// 加载优先级
enum class LoadPriority : u32 {
    Low = 0,
    Normal = 1,
    High = 2,
    Immediate = 3
};

// 加载请求
struct TextureLoadRequest {
    std::string path;
    TextureHandle texture;
    u32 mipLevel = 0;
    LoadPriority priority = LoadPriority::Normal;
    
    // 完成回调
    std::function<void(TextureHandle, bool success)> onComplete;
};

// 纹理信息
struct TextureInfo {
    std::string path;
    u32 width = 0;
    u32 height = 0;
    u32 mipLevels = 0;
    Format format = Format::RGBA8_UNORM;
    u32 currentMipLevel = 0;    // 当前加载的 mip 级别
    bool fullyLoaded = false;
    u32 lastAccessFrame = 0;
    u64 memorySize = 0;
};

// 纹理流式加载器
class TextureStreamer {
public:
    TextureStreamer();
    ~TextureStreamer();
    
    bool initialize(VulkanBackend* backend, u64 maxMemoryMB = 512);
    void shutdown();
    
    // 每帧更新
    void update(u32 currentFrame);
    
    // 加载纹理
    TextureHandle loadTexture(const std::string& path, 
                              LoadPriority priority = LoadPriority::Normal,
                              bool generateMips = true);
    
    // 卸载纹理
    void unloadTexture(TextureHandle texture);
    
    // 升级 mip 级别 (提高质量)
    void upgradeMipLevel(TextureHandle texture);
    
    // 降级 mip 级别 (节省内存)
    void downgradeMipLevel(TextureHandle texture);
    
    // 请求纹理
    void requestTexture(TextureHandle texture, LoadPriority priority = LoadPriority::Normal);
    
    // 预加载
    void preload(const std::vector<std::string>& paths);
    
    // 统计
    u64 getUsedMemory() const { return usedMemory_; }
    u64 getMaxMemory() const { return maxMemory_; }
    u32 getLoadedTextureCount() const { return static_cast<u32>(textureInfos_.size()); }
    u32 getPendingRequestCount() const;
    
    // 设置内存限制
    void setMaxMemory(u64 maxMemoryMB) { maxMemory_ = maxMemoryMB * 1024 * 1024; }
    
    // 获取纹理信息
    const TextureInfo* getTextureInfo(TextureHandle texture) const;
    
private:
    bool loadMipLevel(TextureHandle texture, u32 mipLevel);
    void processLoadQueue();
    void checkMemoryPressure();
    void evictLRU();
    
    VulkanBackend* backend_ = nullptr;
    
    // 纹理信息
    std::unordered_map<u32, TextureInfo> textureInfos_;
    mutable std::mutex infoMutex_;
    
    // 加载队列 (优先级队列)
    std::vector<TextureLoadRequest> loadQueue_;
    mutable std::mutex queueMutex_;
    
    // 内存管理
    u64 maxMemory_ = 512 * 1024 * 1024;
    u64 usedMemory_ = 0;
    
    // LRU 列表
    std::vector<TextureHandle> lruList_;
    
    // 后台加载线程
    bool loading_ = false;
};

// ============================================================================
// 纹理缓存
// ============================================================================

// 缓存项
struct CacheEntry {
    TextureHandle texture;
    u32 width;
    u32 height;
    Format format;
    u32 lastAccessFrame;
    u32 accessCount;
};

// 纹理缓存
class TextureCache {
public:
    TextureCache();
    ~TextureCache();
    
    bool initialize(VulkanBackend* backend, u32 maxEntries = 256);
    void shutdown();
    
    // 获取或加载纹理
    TextureHandle getOrLoad(const std::string& path, 
                             LoadPriority priority = LoadPriority::Normal);
    
    // 预加载
    void preload(const std::string& path);
    
    // 清除
    void clear();
    void clearUnused();
    
    // 统计
    u32 getEntryCount() const { return static_cast<u32>(entries_.size()); }
    u64 getTotalMemory() const;
    
private:
    VulkanBackend* backend_ = nullptr;
    u32 maxEntries_ = 256;
    
    std::unordered_map<std::string, CacheEntry> entries_;
    std::unordered_map<u32, std::string> textureToPath_;
};

// ============================================================================
// KTX 纹理加载器
// ============================================================================

// KTX 文件头
#pragma pack(push, 1)
struct KTXHeader {
    u8 identifier[12];
    VkFormat glFormat;
    u32 pixelWidth;
    u32 pixelHeight;
    u32 pixelDepth;
    u32 numberOfArrayElements;
    u32 numberOfFaces;
    u32 numberOfMipmapLevels;
    u32 bytesOfKeyValueData;
};
#pragma pack(pop)

// KTX 加载器
class KTXLoader {
public:
    static bool load(VulkanBackend* backend, const std::string& path,
                     TextureHandle& outTexture, TextureInfo& outInfo);
    static bool loadMipLevel(VulkanBackend* backend, const std::string& path,
                             TextureHandle texture, u32 mipLevel);
    static bool save(const std::string& path, TextureHandle texture,
                     VulkanBackend* backend);
};

// ============================================================================
// DDS 纹理加载器
// ============================================================================

// DDS 文件头
#pragma pack(push, 1)
struct DDSHeader {
    u32 magic;
    u32 size;
    u32 flags;
    u32 height;
    u32 width;
    u32 pitchOrLinearSize;
    u32 depth;
    u32 mipMapCount;
    u32 reserved1[11];
    struct {
        u32 size;
        u32 flags;
        u32 fourCC;
        u32 rgbBitCount;
        u32 rBitMask;
        u32 gBitMask;
        u32 bBitMask;
        u32 aBitMask;
    } pixelFormat;
    u32 caps;
    u32 caps2;
    u32 caps3;
    u32 caps4;
    u32 reserved2;
};
#pragma pack(pop)

// DDS 加载器
class DDSLoader {
public:
    static bool load(VulkanBackend* backend, const std::string& path,
                     TextureHandle& outTexture, TextureInfo& outInfo);
    static bool isCompressed(const DDSHeader& header);
    static Format detectFormat(const DDSHeader& header);
};

// ============================================================================
// 纹理生成器
// ============================================================================

class TextureGenerator {
public:
    // 生成纯色纹理
    static TextureHandle createSolidColor(VulkanBackend* backend,
                                          const Vec4& color,
                                          u32 width = 1, u32 height = 1);
    
    // 生成渐变纹理
    static TextureHandle createGradient(VulkanBackend* backend,
                                         const Vec4& color1,
                                         const Vec4& color2,
                                         u32 width = 256, u32 height = 256,
                                         bool horizontal = true);
    
    // 生成法线贴图
    static TextureHandle createNormalMap(VulkanBackend* backend,
                                          TextureHandle heightMap,
                                          f32 strength = 1.0f);
    
    // 生成噪声纹理
    static TextureHandle createNoise(VulkanBackend* backend,
                                      u32 width = 256, u32 height = 256,
                                      u32 seed = 0);
    
    // 生成棋盘格纹理
    static TextureHandle createCheckerboard(VulkanBackend* backend,
                                             const Vec4& color1,
                                             const Vec4& color2,
                                             u32 width = 256, u32 height = 256,
                                             u32 cellSize = 32);
    
    // 生成圆角矩形纹理
    static TextureHandle createRoundedRect(VulkanBackend* backend,
                                            u32 width = 256, u32 height = 256,
                                            f32 radius = 16.0f,
                                            const Vec4& fillColor = Vec4(1.0f),
                                            const Vec4& borderColor = Vec4(0.5f),
                                            f32 borderWidth = 2.0f);
    
    // 生成阴影纹理
    static TextureHandle createShadow(VulkanBackend* backend,
                                       u32 size = 256,
                                       f32 blurRadius = 16.0f);
};

} // namespace Nova

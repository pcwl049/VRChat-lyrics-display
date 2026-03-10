/**
 * Nova Renderer - Texture Streaming Implementation
 */

#include "Nova/TextureStreaming.h"
#include "Nova/VulkanBackend.h"
#include <algorithm>
#include <fstream>
#include <cmath>

namespace Nova {

// ============================================================================
// VirtualTexture Implementation
// ============================================================================

VirtualTexture::VirtualTexture() = default;

VirtualTexture::~VirtualTexture() {
    shutdown();
}

bool VirtualTexture::initialize(VulkanBackend* backend, const VirtualTextureDesc& desc) {
    backend_ = backend;
    desc_ = desc;
    
    // 计算页面数量
    u32 pageCount = calculatePageCount();
    pages_.resize(desc_.mipLevels);
    
    for (u32 mip = 0; mip < desc_.mipLevels; mip++) {
        u32 mipWidth = std::max(1u, desc_.virtualWidth >> mip);
        u32 mipHeight = std::max(1u, desc_.virtualHeight >> mip);
        u32 pagesX = (mipWidth + desc_.pageSize - 1) / desc_.pageSize;
        u32 pagesY = (mipHeight + desc_.pageSize - 1) / desc_.pageSize;
        
        pages_[mip].reserve(pagesX * pagesY);
        
        for (u32 y = 0; y < pagesY; y++) {
            for (u32 x = 0; x < pagesX; x++) {
                VirtualTexturePage page;
                page.x = x;
                page.y = y;
                page.mipLevel = mip;
                page.layer = 0;
                page.loaded = false;
                page.lastAccessFrame = 0;
                pages_[mip].push_back(page);
            }
        }
    }
    
    // 创建物理缓存纹理
    TextureDesc cacheDesc;
    cacheDesc.width = desc_.physicalCacheSize;
    cacheDesc.height = desc_.physicalCacheSize;
    cacheDesc.format = desc_.format;
    cacheDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    cacheDesc.mipLevels = 1;
    
    physicalCacheTexture_ = backend_->createTexture(cacheDesc);
    
    physicalCachePagesX_ = desc_.physicalCacheSize / desc_.pageSize;
    physicalCachePagesY_ = desc_.physicalCacheSize / desc_.pageSize;
    physicalCacheUsed_.resize(physicalCachePagesX_ * physicalCachePagesY_, false);
    
    // 创建页表纹理
    TextureDesc ptDesc;
    ptDesc.width = (desc_.virtualWidth / desc_.pageSize) * 2;  // 每个 page 2x2 页表项
    ptDesc.height = (desc_.virtualHeight / desc_.pageSize) * 2;
    ptDesc.format = Format::RGBA16_UINT;
    ptDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    pageTableTexture_ = backend_->createTexture(ptDesc);
    
    return true;
}

void VirtualTexture::shutdown() {
    if (backend_) {
        if (physicalCacheTexture_.valid()) backend_->destroyTexture(physicalCacheTexture_);
        if (pageTableTexture_.valid()) backend_->destroyTexture(pageTableTexture_);
        if (pageTableBuffer_.valid()) backend_->destroyBuffer(pageTableBuffer_);
    }
    pages_.clear();
    physicalCacheUsed_.clear();
    backend_ = nullptr;
}

u32 VirtualTexture::calculatePageCount() const {
    u32 count = 0;
    for (u32 mip = 0; mip < desc_.mipLevels; mip++) {
        u32 mipWidth = std::max(1u, desc_.virtualWidth >> mip);
        u32 mipHeight = std::max(1u, desc_.virtualHeight >> mip);
        u32 pagesX = (mipWidth + desc_.pageSize - 1) / desc_.pageSize;
        u32 pagesY = (mipHeight + desc_.pageSize - 1) / desc_.pageSize;
        count += pagesX * pagesY;
    }
    return count;
}

void VirtualTexture::requestPage(u32 x, u32 y, u32 mipLevel, u32 layer) {
    if (mipLevel >= pages_.size()) return;
    
    u32 mipWidth = std::max(1u, desc_.virtualWidth >> mipLevel);
    u32 mipHeight = std::max(1u, desc_.virtualHeight >> mipLevel);
    u32 pagesX = (mipWidth + desc_.pageSize - 1) / desc_.pageSize;
    u32 pagesY = (mipHeight + desc_.pageSize - 1) / desc_.pageSize;
    
    if (x >= pagesX || y >= pagesY) return;
    
    u32 index = y * pagesX + x;
    if (index >= pages_[mipLevel].size()) return;
    
    auto& page = pages_[mipLevel][index];
    
    if (!page.loaded) {
        loadQueue_.push_back(&page);
    }
}

void VirtualTexture::update(u32 currentFrame) {
    // 处理加载队列
    for (auto* page : loadQueue_) {
        if (!page->loaded) {
            loadPage(*page);
        }
    }
    loadQueue_.clear();
    
    // 更新访问时间
    for (auto& mipPages : pages_) {
        for (auto& page : mipPages) {
            if (page.loaded && page.lastAccessFrame + 300 < currentFrame) {
                // 长时间未访问，放入 LRU 队列
                lruQueue_.push(&page);
            }
        }
    }
    
    // 更新页表
    updatePageTable();
}

void VirtualTexture::loadPage(VirtualTexturePage& page) {
    // 查找空闲物理缓存槽位
    u32 slotIndex = UINT32_MAX;
    for (u32 i = 0; i < physicalCacheUsed_.size(); i++) {
        if (!physicalCacheUsed_[i]) {
            slotIndex = i;
            break;
        }
    }
    
    if (slotIndex == UINT32_MAX) {
        // Need to evict
        if (!lruQueue_.empty()) {
            auto* pageToEvict = lruQueue_.front();
            lruQueue_.pop();
            evictPage(*pageToEvict);
            slotIndex = pageToEvict->physicalY * physicalCachePagesX_ + pageToEvict->physicalX;
        }
    }
    
    if (slotIndex != UINT32_MAX) {
        page.physicalX = slotIndex % physicalCachePagesX_;
        page.physicalY = slotIndex / physicalCachePagesX_;
        page.loaded = true;
        physicalCacheUsed_[slotIndex] = true;
        
        // 实际加载纹理数据到物理缓存
        // ...
    }
}

void VirtualTexture::evictPage(VirtualTexturePage& page) {
    if (!page.loaded) return;
    
    u32 slotIndex = page.physicalY * physicalCachePagesX_ + page.physicalX;
    physicalCacheUsed_[slotIndex] = false;
    page.loaded = false;
}

void VirtualTexture::updatePageTable() {
    // 更新页表纹理
    // GPU 上执行或 CPU 上传
}

Vec4 VirtualTexture::getPageTableUV(const Vec2& uv, u32 mipLevel) const {
    f32 scale = 1.0f / (1 << mipLevel);
    return Vec4(uv.x * scale, uv.y * scale, static_cast<f32>(mipLevel), 0.0f);
}

// ============================================================================
// TextureStreamer Implementation
// ============================================================================

TextureStreamer::TextureStreamer() = default;

TextureStreamer::~TextureStreamer() {
    shutdown();
}

bool TextureStreamer::initialize(VulkanBackend* backend, u64 maxMemoryMB) {
    backend_ = backend;
    maxMemory_ = maxMemoryMB * 1024 * 1024;
    return true;
}

void TextureStreamer::shutdown() {
    textureInfos_.clear();
    loadQueue_.clear();
    backend_ = nullptr;
}

void TextureStreamer::update(u32 currentFrame) {
    // 更新访问时间
    for (auto& [id, info] : textureInfos_) {
        if (info.lastAccessFrame + 600 < currentFrame && info.currentMipLevel > 0) {
            // 长时间未访问，降级 mip
            downgradeMipLevel(TextureHandle{id});
        }
    }
    
    // 处理加载队列
    processLoadQueue();
    
    // 检查内存压力
    checkMemoryPressure();
}

TextureHandle TextureStreamer::loadTexture(const std::string& path, LoadPriority priority, bool generateMips) {
    // 检查是否已加载
    auto it = std::find_if(textureInfos_.begin(), textureInfos_.end(),
        [&path](const auto& pair) { return pair.second.path == path; });
    
    if (it != textureInfos_.end()) {
        TextureHandle handle{it->first};
        textureInfos_[handle.index].lastAccessFrame = 0; // 重置访问时间
        return handle;
    }
    
    // 创建纹理
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (generateMips) {
        desc.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    
    auto texture = backend_->createTexture(desc);
    if (!texture.valid()) {
        return {};
    }
    
    // 创建信息
    TextureInfo info;
    info.path = path;
    info.fullyLoaded = false;
    info.currentMipLevel = 0;
    
    textureInfos_[texture.index] = info;
    
    // 添加到加载队列
    TextureLoadRequest request;
    request.path = path;
    request.texture = texture;
    request.priority = priority;
    loadQueue_.push_back(request);
    
    // 按优先级排序
    std::sort(loadQueue_.begin(), loadQueue_.end(),
        [](const auto& a, const auto& b) {
            return static_cast<u32>(a.priority) > static_cast<u32>(b.priority);
        });
    
    return texture;
}

void TextureStreamer::unloadTexture(TextureHandle texture) {
    auto it = textureInfos_.find(texture.index);
    if (it != textureInfos_.end()) {
        usedMemory_ -= it->second.memorySize;
        textureInfos_.erase(it);
        backend_->destroyTexture(texture);
    }
}

void TextureStreamer::upgradeMipLevel(TextureHandle texture) {
    auto it = textureInfos_.find(texture.index);
    if (it == textureInfos_.end()) return;
    
    auto& info = it->second;
    if (info.currentMipLevel > 0 && !info.fullyLoaded) {
        loadMipLevel(texture, info.currentMipLevel - 1);
    }
}

void TextureStreamer::downgradeMipLevel(TextureHandle texture) {
    auto it = textureInfos_.find(texture.index);
    if (it == textureInfos_.end()) return;
    
    auto& info = it->second;
    if (info.currentMipLevel < info.mipLevels - 1) {
        info.currentMipLevel++;
        // 释放高精度 mip 数据
        // ...
    }
}

void TextureStreamer::requestTexture(TextureHandle texture, LoadPriority priority) {
    auto it = textureInfos_.find(texture.index);
    if (it == textureInfos_.end()) return;
    
    TextureLoadRequest request;
    request.texture = texture;
    request.path = it->second.path;
    request.priority = priority;
    request.mipLevel = it->second.currentMipLevel;
    
    loadQueue_.push_back(request);
}

void TextureStreamer::preload(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        loadTexture(path, LoadPriority::Low, true);
    }
}

u32 TextureStreamer::getPendingRequestCount() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<u32>(loadQueue_.size());
}

const TextureInfo* TextureStreamer::getTextureInfo(TextureHandle texture) const {
    auto it = textureInfos_.find(texture.index);
    return it != textureInfos_.end() ? &it->second : nullptr;
}

bool TextureStreamer::loadMipLevel(TextureHandle texture, u32 mipLevel) {
    auto it = textureInfos_.find(texture.index);
    if (it == textureInfos_.end()) return false;
    
    auto& info = it->second;
    
    // 检查内存
    u64 requiredMemory = info.width * info.height * 4;  // 简化计算
    if (usedMemory_ + requiredMemory > maxMemory_) {
        checkMemoryPressure();
        if (usedMemory_ + requiredMemory > maxMemory_) {
            return false;
        }
    }
    
    // 实际加载纹理数据
    // ...
    
    info.currentMipLevel = mipLevel;
    usedMemory_ += requiredMemory;
    
    return true;
}

void TextureStreamer::processLoadQueue() {
    while (!loadQueue_.empty()) {
        auto request = loadQueue_.back();
        loadQueue_.pop_back();
        
        auto it = textureInfos_.find(request.texture.index);
        if (it == textureInfos_.end()) continue;
        
        auto& info = it->second;
        
        // 加载纹理文件
        if (info.path.find(".ktx") != std::string::npos) {
            KTXLoader::loadMipLevel(backend_, info.path, request.texture, request.mipLevel);
        } else if (info.path.find(".dds") != std::string::npos) {
            DDSLoader::load(backend_, info.path, request.texture, info);
        } else {
            // 标准图像格式，使用 stb_image
            // ...
        }
        
        if (request.onComplete) {
            request.onComplete(request.texture, true);
        }
        
        break;  // 每帧只加载一个
    }
}

void TextureStreamer::checkMemoryPressure() {
    if (usedMemory_ > maxMemory_ * 0.9) {
        evictLRU();
    }
}

void TextureStreamer::evictLRU() {
    // 按 LRU 顺序驱逐
    while (usedMemory_ > maxMemory_ * 0.8 && !lruList_.empty()) {
        auto texture = lruList_.back();
        lruList_.pop_back();
        
        auto it = textureInfos_.find(texture.index);
        if (it != textureInfos_.end()) {
            downgradeMipLevel(texture);
        }
    }
}

// ============================================================================
// TextureCache Implementation
// ============================================================================

TextureCache::TextureCache() = default;

TextureCache::~TextureCache() {
    shutdown();
}

bool TextureCache::initialize(VulkanBackend* backend, u32 maxEntries) {
    backend_ = backend;
    maxEntries_ = maxEntries;
    return true;
}

void TextureCache::shutdown() {
    clear();
    backend_ = nullptr;
}

TextureHandle TextureCache::getOrLoad(const std::string& path, LoadPriority priority) {
    auto it = entries_.find(path);
    if (it != entries_.end()) {
        it->second.accessCount++;
        return it->second.texture;
    }
    
    // 检查缓存大小
    if (entries_.size() >= maxEntries_) {
        clearUnused();
    }
    
    // 加载纹理
    TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend_->createTexture(desc);
    if (!texture.valid()) {
        return {};
    }
    
    CacheEntry entry;
    entry.texture = texture;
    entry.accessCount = 1;
    
    entries_[path] = entry;
    textureToPath_[texture.index] = path;
    
    return texture;
}

void TextureCache::preload(const std::string& path) {
    getOrLoad(path, LoadPriority::Low);
}

void TextureCache::clear() {
    for (auto& [path, entry] : entries_) {
        if (entry.texture.valid()) {
            backend_->destroyTexture(entry.texture);
        }
    }
    entries_.clear();
    textureToPath_.clear();
}

void TextureCache::clearUnused() {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->second.accessCount == 0) {
            textureToPath_.erase(it->second.texture.index);
            backend_->destroyTexture(it->second.texture);
            it = entries_.erase(it);
        } else {
            it->second.accessCount = 0;
            ++it;
        }
    }
}

u64 TextureCache::getTotalMemory() const {
    u64 total = 0;
    for (const auto& [path, entry] : entries_) {
        total += entry.width * entry.height * 4;
    }
    return total;
}

// ============================================================================
// KTXLoader Implementation
// ============================================================================

bool KTXLoader::load(VulkanBackend* backend, const std::string& path,
                     TextureHandle& outTexture, TextureInfo& outInfo) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    KTXHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // 验证标识符
    const u8 ktxIdentifier[] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };
    
    if (memcmp(header.identifier, ktxIdentifier, 12) != 0) {
        return false;
    }
    
    outInfo.width = header.pixelWidth;
    outInfo.height = header.pixelHeight;
    outInfo.mipLevels = header.numberOfMipmapLevels;
    
    // 创建纹理
    TextureDesc desc;
    desc.width = header.pixelWidth;
    desc.height = header.pixelHeight;
    desc.format = Format::RGBA8_UNORM;  // 简化
    desc.mipLevels = header.numberOfMipmapLevels;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    outTexture = backend->createTexture(desc);
    
    // 跳过键值对数据
    file.seekg(header.bytesOfKeyValueData, std::ios::cur);
    
    // 加载 mip 级别
    for (u32 mip = 0; mip < header.numberOfMipmapLevels; mip++) {
        u32 imageSize;
        file.read(reinterpret_cast<char*>(&imageSize), sizeof(imageSize));
        
        std::vector<char> data(imageSize);
        file.read(data.data(), imageSize);
        
        // 上传到 GPU
        // backend->uploadTextureData(outTexture, mip, data.data(), imageSize);
        
        // 跳过 cube map 面
        for (u32 face = 1; face < header.numberOfFaces; face++) {
            file.seekg(imageSize, std::ios::cur);
        }
        
        // 跳过数组元素
        for (u32 array = 1; array < header.numberOfArrayElements; array++) {
            file.seekg(imageSize, std::ios::cur);
        }
        
        // 4 字节对齐
        if (imageSize % 4 != 0) {
            file.seekg(4 - (imageSize % 4), std::ios::cur);
        }
    }
    
    outInfo.fullyLoaded = true;
    return true;
}

bool KTXLoader::loadMipLevel(VulkanBackend* backend, const std::string& path,
                              TextureHandle texture, u32 mipLevel) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    KTXHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // 跳过键值对数据
    file.seekg(header.bytesOfKeyValueData, std::ios::cur);
    
    // 跳到指定 mip 级别
    for (u32 mip = 0; mip < mipLevel; mip++) {
        u32 imageSize;
        file.read(reinterpret_cast<char*>(&imageSize), sizeof(imageSize));
        file.seekg(imageSize * header.numberOfFaces * header.numberOfArrayElements, std::ios::cur);
    }
    
    // 读取指定 mip 级别
    u32 imageSize;
    file.read(reinterpret_cast<char*>(&imageSize), sizeof(imageSize));
    
    std::vector<char> data(imageSize);
    file.read(data.data(), imageSize);
    
    return true;
}

bool KTXLoader::save(const std::string& path, TextureHandle texture, VulkanBackend* backend) {
    // 实现保存逻辑
    return false;
}

// ============================================================================
// DDSLoader Implementation
// ============================================================================

bool DDSLoader::load(VulkanBackend* backend, const std::string& path,
                     TextureHandle& outTexture, TextureInfo& outInfo) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    DDSHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    
    // 验证 magic
    if (header.magic != 0x20534444) {  // "DDS "
        return false;
    }
    
    outInfo.width = header.width;
    outInfo.height = header.height;
    outInfo.mipLevels = std::max(1u, header.mipMapCount);
    outInfo.format = detectFormat(header);
    
    // 创建纹理
    TextureDesc desc;
    desc.width = header.width;
    desc.height = header.height;
    desc.format = outInfo.format;
    desc.mipLevels = outInfo.mipLevels;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    outTexture = backend->createTexture(desc);
    
    // 加载数据
    // ...
    
    outInfo.fullyLoaded = true;
    return true;
}

bool DDSLoader::isCompressed(const DDSHeader& header) {
    return header.pixelFormat.flags & 0x4;  // DDPF_FOURCC
}

Format DDSLoader::detectFormat(const DDSHeader& header) {
    if (header.pixelFormat.flags & 0x4) {
        switch (header.pixelFormat.fourCC) {
            case 0x31545844: return Format::BC1_RGB_UNORM;   // DXT1
            case 0x32545844: return Format::BC2_UNORM;       // DXT3
            case 0x33545844: return Format::BC3_UNORM;       // DXT5
            case 0x54415449: return Format::BC5_UNORM;       // ATI2
            default: return Format::RGBA8_UNORM;
        }
    }
    
    return Format::RGBA8_UNORM;
}

// ============================================================================
// TextureGenerator Implementation
// ============================================================================

TextureHandle TextureGenerator::createSolidColor(VulkanBackend* backend,
                                                  const Vec4& color,
                                                  u32 width, u32 height) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    // 创建纯色数据
    std::vector<u32> data(width * height);
    u32 rgba = static_cast<u32>(color.r * 255) |
               (static_cast<u32>(color.g * 255) << 8) |
               (static_cast<u32>(color.b * 255) << 16) |
               (static_cast<u32>(color.a * 255) << 24);
    
    std::fill(data.begin(), data.end(), rgba);
    
    backend->uploadTextureData(texture, data.data(), data.size() * 4);
    
    return texture;
}

TextureHandle TextureGenerator::createGradient(VulkanBackend* backend,
                                               const Vec4& color1,
                                               const Vec4& color2,
                                               u32 width, u32 height,
                                               bool horizontal) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    std::vector<u8> data(width * height * 4);
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            f32 t = horizontal ? (static_cast<f32>(x) / width) : (static_cast<f32>(y) / height);
            
            Vec4 color = color1 * (1.0f - t) + color2 * t;
            
            u32 index = (y * width + x) * 4;
            data[index + 0] = static_cast<u8>(color.r * 255);
            data[index + 1] = static_cast<u8>(color.g * 255);
            data[index + 2] = static_cast<u8>(color.b * 255);
            data[index + 3] = static_cast<u8>(color.a * 255);
        }
    }
    
    backend->uploadTextureData(texture, data.data(), data.size());
    
    return texture;
}

TextureHandle TextureGenerator::createNoise(VulkanBackend* backend,
                                            u32 width, u32 height,
                                            u32 seed) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    std::vector<u8> data(width * height * 4);
    
    // 简单伪随机噪声
    u32 state = seed;
    for (u32 i = 0; i < data.size(); i += 4) {
        state = state * 1103515245 + 12345;
        u8 value = static_cast<u8>((state >> 16) & 0xFF);
        data[i + 0] = value;
        data[i + 1] = value;
        data[i + 2] = value;
        data[i + 3] = 255;
    }
    
    backend->uploadTextureData(texture, data.data(), data.size());
    
    return texture;
}

TextureHandle TextureGenerator::createCheckerboard(VulkanBackend* backend,
                                                   const Vec4& color1,
                                                   const Vec4& color2,
                                                   u32 width, u32 height,
                                                   u32 cellSize) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    std::vector<u8> data(width * height * 4);
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            bool isWhite = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            Vec4 color = isWhite ? color1 : color2;
            
            u32 index = (y * width + x) * 4;
            data[index + 0] = static_cast<u8>(color.r * 255);
            data[index + 1] = static_cast<u8>(color.g * 255);
            data[index + 2] = static_cast<u8>(color.b * 255);
            data[index + 3] = static_cast<u8>(color.a * 255);
        }
    }
    
    backend->uploadTextureData(texture, data.data(), data.size());
    
    return texture;
}

TextureHandle TextureGenerator::createRoundedRect(VulkanBackend* backend,
                                                  u32 width, u32 height,
                                                  f32 radius,
                                                  const Vec4& fillColor,
                                                  const Vec4& borderColor,
                                                  f32 borderWidth) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = Format::RGBA8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    std::vector<u8> data(width * height * 4);
    
    f32 cx = static_cast<f32>(width) / 2;
    f32 cy = static_cast<f32>(height) / 2;
    f32 rx = cx - radius;
    f32 ry = cy - radius;
    
    auto roundedDist = [&](f32 px, f32 py) -> f32 {
        f32 dx = std::abs(px - cx);
        f32 dy = std::abs(py - cy);
        
        if (dx > rx && dy > ry) {
            f32 cornerX = dx - rx;
            f32 cornerY = dy - ry;
            return std::sqrt(cornerX * cornerX + cornerY * cornerY);
        }
        return 0.0f;
    };
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            f32 dist = roundedDist(static_cast<f32>(x), static_cast<f32>(y));
            
            Vec4 color;
            if (dist > radius) {
                color = Vec4(0, 0, 0, 0);  // 透明
            } else if (dist > radius - borderWidth) {
                color = borderColor;  // 边框
            } else {
                color = fillColor;  // 填充
            }
            
            u32 index = (y * width + x) * 4;
            data[index + 0] = static_cast<u8>(color.r * 255);
            data[index + 1] = static_cast<u8>(color.g * 255);
            data[index + 2] = static_cast<u8>(color.b * 255);
            data[index + 3] = static_cast<u8>(color.a * 255);
        }
    }
    
    backend->uploadTextureData(texture, data.data(), data.size());
    
    return texture;
}

TextureHandle TextureGenerator::createShadow(VulkanBackend* backend,
                                             u32 size,
                                             f32 blurRadius) {
    TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = Format::R8_UNORM;
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    
    auto texture = backend->createTexture(desc);
    
    std::vector<u8> data(size * size);
    
    f32 center = static_cast<f32>(size) / 2;
    f32 innerRadius = center - blurRadius;
    
    for (u32 y = 0; y < size; y++) {
        for (u32 x = 0; x < size; x++) {
            f32 dx = static_cast<f32>(x) - center;
            f32 dy = static_cast<f32>(y) - center;
            f32 dist = std::sqrt(dx * dx + dy * dy);
            
            f32 alpha = 1.0f - std::clamp((dist - innerRadius) / blurRadius, 0.0f, 1.0f);
            data[y * size + x] = static_cast<u8>(alpha * 255);
        }
    }
    
    backend->uploadTextureData(texture, data.data(), data.size());
    
    return texture;
}

} // namespace Nova

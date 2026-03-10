/**
 * Nova Renderer - Texture System Implementation
 */

#include "Nova/Texture.h"
#include "Nova/Command.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <stb_image.h>
// Note: stb_image_write.h not included - save functionality disabled

#include <fstream>
#include <algorithm>

namespace Nova {

// ============================================================================
// ImageLoader Implementation
// ============================================================================

ImageData ImageLoader::load(const char* path) {
    ImageData image;
    
    int width, height, channels;
    stbi_uc* pixels = stbi_load(path, &width, &height, &channels, 4);
    
    if (!pixels) {
        return image;
    }
    
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.channels = 4;
    
    u64 size = static_cast<u64>(width) * height * 4;
    image.pixels.resize(size);
    memcpy(image.pixels.data(), pixels, size);
    
    stbi_image_free(pixels);
    return image;
}

ImageData ImageLoader::loadFromMemory(const u8* data, u64 size) {
    ImageData image;
    
    int width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), 
                                            &width, &height, &channels, 4);
    
    if (!pixels) {
        return image;
    }
    
    image.width = static_cast<u32>(width);
    image.height = static_cast<u32>(height);
    image.channels = 4;
    
    u64 pixelSize = static_cast<u64>(width) * height * 4;
    image.pixels.resize(pixelSize);
    memcpy(image.pixels.data(), pixels, pixelSize);
    
    stbi_image_free(pixels);
    return image;
}

bool ImageLoader::save(const char* path, const ImageData& image) {
    std::string p = path;
    if (p.size() >= 4) {
        std::string ext = p.substr(p.size() - 4);
        if (ext == ".png") {
            return savePNG(path, image.pixels.data(), image.width, image.height, image.channels);
        } else if (ext == ".jpg" || p.size() >= 5 && p.substr(p.size() - 5) == ".jpeg") {
            return saveJPG(path, image.pixels.data(), image.width, image.height, image.channels);
        }
    }
    return false;
}

bool ImageLoader::savePNG(const char* path, const u8* pixels, u32 width, u32 height, u32 channels) {
    // stb_image_write not available - disabled
    (void)path; (void)pixels; (void)width; (void)height; (void)channels;
    return false;
}

bool ImageLoader::saveJPG(const char* path, const u8* pixels, u32 width, u32 height, u32 channels, i32 quality) {
    // stb_image_write not available - disabled
    (void)path; (void)pixels; (void)width; (void)height; (void)channels; (void)quality;
    return false;
}

ImageData ImageLoader::convert(const ImageData& src, TextureFormat dstFormat) {
    // 简化实现，只支持 RGBA8
    ImageData dst = src;
    dst.format = dstFormat;
    return dst;
}

ImageData ImageLoader::resize(const ImageData& src, u32 newWidth, u32 newHeight) {
    ImageData dst;
    dst.width = newWidth;
    dst.height = newHeight;
    dst.channels = src.channels;
    dst.format = src.format;
    
    u64 size = static_cast<u64>(newWidth) * newHeight * src.channels;
    dst.pixels.resize(size);
    
    // 简单的最近邻采样
    f32 scaleX = static_cast<f32>(src.width) / newWidth;
    f32 scaleY = static_cast<f32>(src.height) / newHeight;
    
    for (u32 y = 0; y < newHeight; y++) {
        for (u32 x = 0; x < newWidth; x++) {
            u32 srcX = static_cast<u32>(x * scaleX);
            u32 srcY = static_cast<u32>(y * scaleY);
            
            for (u32 c = 0; c < src.channels; c++) {
                u64 dstIdx = (y * newWidth + x) * src.channels + c;
                u64 srcIdx = (srcY * src.width + srcX) * src.channels + c;
                dst.pixels[dstIdx] = src.pixels[srcIdx];
            }
        }
    }
    
    return dst;
}

ImageData ImageLoader::generateMips(const ImageData& src, u32 levels) {
    // 返回第一级 mipmap
    // 实际 mipmap 生成应该在 GPU 上进行
    return src;
}

ImageData ImageLoader::create(u32 width, u32 height, u32 channels, const Color& color) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = channels;
    
    u64 size = static_cast<u64>(width) * height * channels;
    image.pixels.resize(size);
    
    u8 r = static_cast<u8>(color.r * 255);
    u8 g = static_cast<u8>(color.g * 255);
    u8 b = static_cast<u8>(color.b * 255);
    u8 a = static_cast<u8>(color.a * 255);
    
    for (u64 i = 0; i < size; i += channels) {
        image.pixels[i] = r;
        if (channels > 1) image.pixels[i + 1] = g;
        if (channels > 2) image.pixels[i + 2] = b;
        if (channels > 3) image.pixels[i + 3] = a;
    }
    
    return image;
}

ImageData ImageLoader::createCheckerboard(u32 width, u32 height, u32 cellSize, 
                                          const Color& color1, const Color& color2) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    
    u64 size = static_cast<u64>(width) * height * 4;
    image.pixels.resize(size);
    
    u8 r1 = static_cast<u8>(color1.r * 255);
    u8 g1 = static_cast<u8>(color1.g * 255);
    u8 b1 = static_cast<u8>(color1.b * 255);
    u8 a1 = static_cast<u8>(color1.a * 255);
    
    u8 r2 = static_cast<u8>(color2.r * 255);
    u8 g2 = static_cast<u8>(color2.g * 255);
    u8 b2 = static_cast<u8>(color2.b * 255);
    u8 a2 = static_cast<u8>(color2.a * 255);
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u64 idx = (y * width + x) * 4;
            bool cell = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            
            if (cell) {
                image.pixels[idx] = r1;
                image.pixels[idx + 1] = g1;
                image.pixels[idx + 2] = b1;
                image.pixels[idx + 3] = a1;
            } else {
                image.pixels[idx] = r2;
                image.pixels[idx + 1] = g2;
                image.pixels[idx + 2] = b2;
                image.pixels[idx + 3] = a2;
            }
        }
    }
    
    return image;
}

ImageData ImageLoader::createGradient(u32 width, u32 height, const Color& c1, const Color& c2, bool horizontal) {
    ImageData image;
    image.width = width;
    image.height = height;
    image.channels = 4;
    
    u64 size = static_cast<u64>(width) * height * 4;
    image.pixels.resize(size);
    
    for (u32 y = 0; y < height; y++) {
        for (u32 x = 0; x < width; x++) {
            u64 idx = (y * width + x) * 4;
            f32 t = horizontal ? (f32)x / width : (f32)y / height;
            
            image.pixels[idx] = static_cast<u8>((c1.r + (c2.r - c1.r) * t) * 255);
            image.pixels[idx + 1] = static_cast<u8>((c1.g + (c2.g - c1.g) * t) * 255);
            image.pixels[idx + 2] = static_cast<u8>((c1.b + (c2.b - c1.b) * t) * 255);
            image.pixels[idx + 3] = static_cast<u8>((c1.a + (c2.a - c1.a) * t) * 255);
        }
    }
    
    return image;
}

// ============================================================================
// Texture Implementation
// ============================================================================

Texture::~Texture() {
    destroy();
}

Texture::Texture(Texture&& o) noexcept
    : device_(o.device_)
    , allocator_(o.allocator_)
    , image_(o.image_)
    , allocation_(o.allocation_)
    , view_(o.view_)
    , width_(o.width_)
    , height_(o.height_)
    , mipLevels_(o.mipLevels_)
    , format_(o.format_)
    , layout_(o.layout_) {
    o.device_ = VK_NULL_HANDLE;
    o.image_ = VK_NULL_HANDLE;
    o.allocation_ = VK_NULL_HANDLE;
    o.view_ = VK_NULL_HANDLE;
}

Texture& Texture::operator=(Texture&& o) noexcept {
    if (this != &o) {
        destroy();
        
        device_ = o.device_;
        allocator_ = o.allocator_;
        image_ = o.image_;
        allocation_ = o.allocation_;
        view_ = o.view_;
        width_ = o.width_;
        height_ = o.height_;
        mipLevels_ = o.mipLevels_;
        format_ = o.format_;
        layout_ = o.layout_;
        
        o.device_ = VK_NULL_HANDLE;
        o.image_ = VK_NULL_HANDLE;
        o.allocation_ = VK_NULL_HANDLE;
        o.view_ = VK_NULL_HANDLE;
    }
    return *this;
}

bool Texture::create(VkDevice device, VmaAllocator allocator, const TextureCreateInfo& info) {
    device_ = device;
    allocator_ = allocator;
    width_ = info.width;
    height_ = info.height;
    mipLevels_ = info.mipLevels;
    format_ = getVkFormat(info.format);
    
    // 创建图像
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = info.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = info.width;
    imageInfo.extent.height = info.height;
    imageInfo.extent.depth = info.depth;
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.format = format_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = info.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(info.samples);
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image_, &allocation_, nullptr) != VK_SUCCESS) {
        return false;
    }
    
    // 创建图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = info.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = info.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = info.arrayLayers;
    
    if (vkCreateImageView(device_, &viewInfo, nullptr, &view_) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        return false;
    }
    
    layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void Texture::destroy() {
    if (device_ == VK_NULL_HANDLE) return;
    
    if (view_) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    
    if (image_) {
        vmaDestroyImage(allocator_, image_, allocation_);
        image_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
    
    device_ = VK_NULL_HANDLE;
}

void Texture::upload(const void* data, u64 size, VkCommandBuffer cmd, VkQueue queue) {
    // 创建临时缓冲区
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, nullptr) != VK_SUCCESS) {
        return;
    }
    
    // 复制数据
    void* mapped;
    vmaMapMemory(allocator_, stagingAllocation, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(allocator_, stagingAllocation);
    
    // 转换布局
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // 复制缓冲区到图像
    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {width_, height_, 1};
    
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    
    // 转换到着色器只读布局
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // 提交并等待
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    
    // 清理临时缓冲区
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);
}

void Texture::upload(const ImageData& image, VkCommandBuffer cmd, VkQueue queue) {
    upload(image.pixels.data(), image.pixels.size(), cmd, queue);
}

void Texture::generateMips(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;
    
    i32 mipWidth = static_cast<i32>(width_);
    i32 mipHeight = static_cast<i32>(height_);
    
    for (u32 i = 1; i < mipLevels_; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        
        vkCmdBlitImage(cmd, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }
    
    barrier.subresourceRange.baseMipLevel = mipLevels_ - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void Texture::transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = layout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels_;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    
    // 设置访问标志
    if (layout_ == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
    } else if (layout_ == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    }
    
    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    } else {
        barrier.dstAccessMask = 0;
    }
    
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    layout_ = newLayout;
}

// ============================================================================
// TextureManager Implementation
// ============================================================================

bool TextureManager::initialize(VkDevice device, VmaAllocator allocator, 
                                VkCommandPool cmdPool, VkQueue queue) {
    device_ = device;
    allocator_ = allocator;
    cmdPool_ = cmdPool;
    queue_ = queue;
    return true;
}

void TextureManager::shutdown() {
    textures_.clear();
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

TextureHandle TextureManager::createFromFile(const char* path, const TextureCreateInfo& info) {
    ImageData image = ImageLoader::load(path);
    if (!image.valid()) {
        return {};
    }
    return createFromData(image, info);
}

TextureHandle TextureManager::createFromData(const ImageData& data, const TextureCreateInfo& info) {
    TextureCreateInfo texInfo = info;
    texInfo.width = data.width;
    texInfo.height = data.height;
    
    Texture tex;
    if (!tex.create(device_, allocator_, texInfo)) {
        return {};
    }
    
    // 分配命令缓冲
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device_, &allocInfo, &cmd);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    
    tex.upload(data, cmd, queue_);
    
    vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
    
    // 存储纹理
    u32 id = textures_.insert(std::move(tex));
    return TextureHandle{id};
}

TextureHandle TextureManager::create(const TextureCreateInfo& info) {
    Texture tex;
    if (!tex.create(device_, allocator_, info)) {
        return {};
    }
    
    u32 id = textures_.insert(std::move(tex));
    return TextureHandle{id};
}

void TextureManager::destroy(TextureHandle handle) {
    textures_.erase(handle.index);
}

Texture* TextureManager::get(TextureHandle handle) {
    return textures_.get(handle.index);
}

void TextureManager::loadAsync(const char* path, LoadCallback callback) {
    AsyncLoad load;
    load.path = path;
    load.callback = callback;
    asyncLoads_.push_back(std::move(load));
}

void TextureManager::processAsyncLoads() {
    for (auto& load : asyncLoads_) {
        if (!load.loaded) {
            load.data = ImageLoader::load(load.path.c_str());
            load.loaded = true;
        }
        
        if (load.data.valid() && load.callback) {
            auto handle = createFromData(load.data);
            load.callback(handle);
        }
    }
    asyncLoads_.clear();
}

// ============================================================================
// TextureAtlas Implementation
// ============================================================================

bool TextureAtlas::initialize(u32 size) {
    size_ = size;
    currentX_ = 0;
    currentY_ = 0;
    rowHeight_ = 0;
    
    // 创建空白纹理
    ImageData blank = ImageLoader::create(size, size, 4, Color::transparent());
    
    // 需要通过 TextureManager 创建纹理
    // 这里简化处理
    return true;
}

void TextureAtlas::shutdown() {
    texture_ = {};
}

TextureAtlas::Allocation TextureAtlas::allocate(u32 width, u32 height) {
    Allocation result;
    
    // 检查当前行是否有空间
    if (currentX_ + width > size_) {
        // 换行
        currentX_ = 0;
        currentY_ += rowHeight_;
        rowHeight_ = 0;
    }
    
    // 检查是否超出图集
    if (currentY_ + height > size_) {
        return result;
    }
    
    // 分配空间
    f32 u0 = static_cast<f32>(currentX_) / size_;
    f32 v0 = static_cast<f32>(currentY_) / size_;
    f32 u1 = static_cast<f32>(currentX_ + width) / size_;
    f32 v1 = static_cast<f32>(currentY_ + height) / size_;
    
    result.uvRect = Vec4(u0, v0, u1, v1);
    result.success = true;
    
    currentX_ += width;
    rowHeight_ = std::max(rowHeight_, height);
    
    return result;
}

void TextureAtlas::upload(u32 x, u32 y, const ImageData& image) {
    // 上传到纹理的指定区域
    // 需要 GPU 命令
}

void TextureAtlas::clear() {
    currentX_ = 0;
    currentY_ = 0;
    rowHeight_ = 0;
}

} // namespace Nova

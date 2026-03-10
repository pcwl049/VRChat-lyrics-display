/**
 * Nova Renderer - Texture System
 * 纹理系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>

// VMA 内存分配器前向声明
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T*;
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace Nova {

// 纹理格式
enum class TextureFormat : u32 {
    R8,
    RG8,
    RGB8,
    RGBA8,
    R16,
    RG16,
    RGBA16,
    R16F,
    RG16F,
    RGBA16F,
    R32F,
    RG32F,
    RGBA32F,
    R8UNorm,
    RG8UNorm,
    RGBA8UNorm,
    R8SNorm,
    RG8SNorm,
    RGBA8SNorm,
    R8SInt,
    RG8SInt,
    RGBA8SInt,
    R8UInt,
    RG8UInt,
    RGBA8UInt,
    BC1RGB,
    BC1RGBA,
    BC2,
    BC3,
    BC4,
    BC5,
    BC6H,
    BC7,
    Depth16,
    Depth24Stencil8,
    Depth32F,
    Depth32FStencil8
};

inline VkFormat getVkFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8: return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8: return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGB8: return VK_FORMAT_R8G8B8_UNORM;
        case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::R16: return VK_FORMAT_R16_UNORM;
        case TextureFormat::RG16: return VK_FORMAT_R16G16_UNORM;
        case TextureFormat::RGBA16: return VK_FORMAT_R16G16B16A16_UNORM;
        case TextureFormat::R16F: return VK_FORMAT_R16_SFLOAT;
        case TextureFormat::RG16F: return VK_FORMAT_R16G16_SFLOAT;
        case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TextureFormat::R32F: return VK_FORMAT_R32_SFLOAT;
        case TextureFormat::RG32F: return VK_FORMAT_R32G32_SFLOAT;
        case TextureFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TextureFormat::R8UNorm: return VK_FORMAT_R8_UNORM;
        case TextureFormat::RG8UNorm: return VK_FORMAT_R8G8_UNORM;
        case TextureFormat::RGBA8UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::R8SNorm: return VK_FORMAT_R8_SNORM;
        case TextureFormat::RG8SNorm: return VK_FORMAT_R8G8_SNORM;
        case TextureFormat::RGBA8SNorm: return VK_FORMAT_R8G8B8A8_SNORM;
        case TextureFormat::R8SInt: return VK_FORMAT_R8_SINT;
        case TextureFormat::RG8SInt: return VK_FORMAT_R8G8_SINT;
        case TextureFormat::RGBA8SInt: return VK_FORMAT_R8G8B8A8_SINT;
        case TextureFormat::R8UInt: return VK_FORMAT_R8_UINT;
        case TextureFormat::RG8UInt: return VK_FORMAT_R8G8_UINT;
        case TextureFormat::RGBA8UInt: return VK_FORMAT_R8G8B8A8_UINT;
        case TextureFormat::BC1RGB: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case TextureFormat::BC1RGBA: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case TextureFormat::BC2: return VK_FORMAT_BC2_UNORM_BLOCK;
        case TextureFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case TextureFormat::BC4: return VK_FORMAT_BC4_UNORM_BLOCK;
        case TextureFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case TextureFormat::BC6H: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
        case TextureFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        case TextureFormat::Depth16: return VK_FORMAT_D16_UNORM;
        case TextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::Depth32F: return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::Depth32FStencil8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
}

// 纹理用途
enum class TextureUsage : u32 {
    Sampled = VK_IMAGE_USAGE_SAMPLED_BIT,
    Storage = VK_IMAGE_USAGE_STORAGE_BIT,
    ColorAttachment = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    DepthStencilAttachment = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    TransferSrc = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    TransferDst = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    InputAttachment = VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT
};

// 纹理创建描述
struct TextureCreateInfo {
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    TextureFormat format = TextureFormat::RGBA8;
    u32 usage = static_cast<u32>(TextureUsage::Sampled) | static_cast<u32>(TextureUsage::TransferDst);
    u32 samples = 1;
    const char* name = nullptr;
    
    // 快捷方法
    TextureCreateInfo& setSize(u32 w, u32 h, u32 d = 1) {
        width = w; height = h; depth = d;
        return *this;
    }
    TextureCreateInfo& setFormat(TextureFormat f) {
        format = f;
        return *this;
    }
    TextureCreateInfo& setUsage(u32 u) {
        usage = u;
        return *this;
    }
    TextureCreateInfo& addUsage(TextureUsage u) {
        usage |= static_cast<u32>(u);
        return *this;
    }
    TextureCreateInfo& setMipLevels(u32 mips) {
        mipLevels = mips;
        return *this;
    }
    TextureCreateInfo& setArrayLayers(u32 layers) {
        arrayLayers = layers;
        return *this;
    }
    TextureCreateInfo& setSamples(u32 s) {
        samples = s;
        return *this;
    }
};

// 图像数据
struct ImageData {
    std::vector<u8> pixels;
    u32 width = 0;
    u32 height = 0;
    u32 channels = 4;
    TextureFormat format = TextureFormat::RGBA8;
    
    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
    u64 size() const { return pixels.size(); }
};

// 图像加载器
class ImageLoader {
public:
    // 加载图像文件
    static ImageData load(const char* path);
    static ImageData loadFromMemory(const u8* data, u64 size);
    
    // 保存图像文件
    static bool save(const char* path, const ImageData& image);
    static bool savePNG(const char* path, const u8* pixels, u32 width, u32 height, u32 channels);
    static bool saveJPG(const char* path, const u8* pixels, u32 width, u32 height, u32 channels, i32 quality = 90);
    
    // 格式转换
    static ImageData convert(const ImageData& src, TextureFormat dstFormat);
    static ImageData resize(const ImageData& src, u32 newWidth, u32 newHeight);
    static ImageData generateMips(const ImageData& src, u32 levels);
    
    // 创建图像
    static ImageData create(u32 width, u32 height, u32 channels, const Color& color = Color::white());
    static ImageData createCheckerboard(u32 width, u32 height, u32 cellSize, const Color& color1, const Color& color2);
    static ImageData createGradient(u32 width, u32 height, const Color& c1, const Color& c2, bool horizontal = true);
};

// 纹理类
class Texture {
public:
    Texture() = default;
    ~Texture();
    
    // 移动
    Texture(Texture&& o) noexcept;
    Texture& operator=(Texture&& o) noexcept;
    
    // 禁止拷贝
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    
    // 创建
    bool create(VkDevice device, VmaAllocator allocator, const TextureCreateInfo& info);
    void destroy();
    
    // 上传数据
    void upload(const void* data, u64 size, VkCommandBuffer cmd, VkQueue queue);
    void upload(const ImageData& image, VkCommandBuffer cmd, VkQueue queue);
    
    // 生成 Mipmap
    void generateMips(VkCommandBuffer cmd);
    
    // 信息
    u32 getWidth() const { return width_; }
    u32 getHeight() const { return height_; }
    u32 getMipLevels() const { return mipLevels_; }
    VkFormat getFormat() const { return format_; }
    VkImageLayout getLayout() const { return layout_; }
    
    // 原生访问
    VkImage getImage() const { return image_; }
    VkImageView getView() const { return view_; }
    
    // 转换布局
    void transitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                         VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    
    u32 width_ = 0;
    u32 height_ = 0;
    u32 mipLevels_ = 1;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkImageLayout layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

// 纹理管理器
class TextureManager {
public:
    bool initialize(VkDevice device, VmaAllocator allocator, VkCommandPool cmdPool, VkQueue queue);
    void shutdown();
    
    // 从文件创建
    TextureHandle createFromFile(const char* path, const TextureCreateInfo& info = {});
    
    // 从数据创建
    TextureHandle createFromData(const ImageData& data, const TextureCreateInfo& info = {});
    
    // 创建空白纹理
    TextureHandle create(const TextureCreateInfo& info);
    
    // 销毁
    void destroy(TextureHandle handle);
    
    // 获取
    Texture* get(TextureHandle handle);
    
    // 异步加载
    using LoadCallback = std::function<void(TextureHandle)>;
    void loadAsync(const char* path, LoadCallback callback);
    void processAsyncLoads();
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool cmdPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    
    ResourcePool<Texture, void> textures_;
    
    struct AsyncLoad {
        std::string path;
        LoadCallback callback;
        ImageData data;
        bool loaded = false;
    };
    std::vector<AsyncLoad> asyncLoads_;
};

// 纹理图集
class TextureAtlas {
public:
    bool initialize(u32 size = 4096);
    void shutdown();
    
    // 分配区域
    struct Allocation {
        Vec4 uvRect;
        bool success = false;
    };
    Allocation allocate(u32 width, u32 height);
    
    // 上传
    void upload(u32 x, u32 y, const ImageData& image);
    
    // 获取纹理
    TextureHandle getTexture() const { return texture_; }
    
    // 清空
    void clear();
    
private:
    TextureHandle texture_;
    u32 size_ = 0;
    u32 currentX_ = 0;
    u32 currentY_ = 0;
    u32 rowHeight_ = 0;
};

} // namespace Nova

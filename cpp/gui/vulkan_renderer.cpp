/**
 * Nova Renderer - Vulkan Implementation
 * 
 * 核心实现:
 * - Vulkan 实例/设备创建
 * - Swapchain 管理
 * - 资源创建和绑定
 * - MSDF 字体生成
 * - 着色器特效
 * - 多线程命令录制
 */

#include "vulkan_renderer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <set>
#include <mutex>

// VMA 内存分配器
#define VMA_IMPLEMENTATION
#include <vulkan/vk_mem_alloc.h>

// FreeType 用于字体
#include <ft2build.h>
#include FT_FREETYPE_H

// msdfgen 用于 MSDF
#include <msdfgen.h>
#include <msdfgen-ext.h>

namespace Nova {

// ============================================================================
// 内部工具
// ============================================================================

namespace Internal {

const char* VkResultToString(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR: return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
        case VK_ERROR_VALIDATION_FAILED_EXT: return "VK_ERROR_VALIDATION_FAILED_EXT";
        default: return "UNKNOWN_VK_RESULT";
    }
}

void LogError(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Nova Error] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void LogFatal(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[Nova FATAL] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void LogInfo(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf("[Nova] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}

void LogDebug(const char* fmt, ...) {
#ifdef _DEBUG
    va_list args;
    va_start(args, fmt);
    printf("[Nova Debug] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
#endif
}

} // namespace Internal

// ============================================================================
// 数学函数实现
// ============================================================================

Mat3 Mat3::translate(f32 tx, f32 ty) {
    Mat3 r;
    r.m[6] = tx;
    r.m[7] = ty;
    return r;
}

Mat3 Mat3::scale(f32 sx, f32 sy) {
    Mat3 r;
    r.m[0] = sx;
    r.m[4] = sy;
    return r;
}

Mat3 Mat3::rotate(f32 angle) {
    f32 c = cosf(angle);
    f32 s = sinf(angle);
    Mat3 r;
    r.m[0] = c;
    r.m[1] = s;
    r.m[3] = -s;
    r.m[4] = c;
    return r;
}

Mat3 Mat3::ortho(f32 left, f32 right, f32 bottom, f32 top) {
    Mat3 r;
    r.m[0] = 2.0f / (right - left);
    r.m[4] = 2.0f / (top - bottom);
    r.m[6] = -(right + left) / (right - left);
    r.m[7] = -(top + bottom) / (top - bottom);
    return r;
}

Mat4 Mat4::ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ) {
    Mat4 r;
    r.m[0] = 2.0f / (right - left);
    r.m[5] = 2.0f / (top - bottom);
    r.m[10] = -2.0f / (farZ - nearZ);
    r.m[12] = -(right + left) / (right - left);
    r.m[13] = -(top + bottom) / (top - bottom);
    r.m[14] = -(farZ + nearZ) / (farZ - nearZ);
    return r;
}

Mat4 Mat4::perspective(f32 fov, f32 aspect, f32 nearZ, f32 farZ) {
    Mat4 r;
    f32 tanHalfFov = tanf(fov / 2.0f);
    r.m[0] = 1.0f / (aspect * tanHalfFov);
    r.m[5] = 1.0f / tanHalfFov;
    r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    r.m[11] = -1.0f;
    r.m[14] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    r.m[15] = 0.0f;
    return r;
}

template<> Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

template<> Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

template<> Vec4 Lerp(const Vec4& a, const Vec4& b, f32 t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    };
}

template<> Color Lerp(const Color& a, const Color& b, f32 t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t
    };
}

// ============================================================================
// 缓动函数实现
// ============================================================================

namespace Easing {

f32 Linear(f32 t) { return t; }

f32 EaseIn(f32 t) { return t * t; }

f32 EaseOut(f32 t) { return t * (2.0f - t); }

f32 EaseInOut(f32 t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f) * t;
}

f32 EaseInQuad(f32 t) { return t * t; }

f32 EaseOutQuad(f32 t) { return t * (2.0f - t); }

f32 EaseInOutQuad(f32 t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f) * t;
}

f32 EaseInCubic(f32 t) { return t * t * t; }

f32 EaseOutCubic(f32 t) {
    f32 t1 = t - 1.0f;
    return t1 * t1 * t1 + 1.0f;
}

f32 EaseInOutCubic(f32 t) {
    return t < 0.5f
        ? 4.0f * t * t * t
        : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

f32 EaseInElastic(f32 t) {
    if (t == 0 || t == 1) return t;
    f32 p = 0.3f;
    f32 s = p / 4.0f;
    f32 t1 = t - 1.0f;
    return -powf(2.0f, 10.0f * t1) * sinf((t1 - s) * (2.0f * 3.14159f) / p);
}

f32 EaseOutElastic(f32 t) {
    if (t == 0 || t == 1) return t;
    f32 p = 0.3f;
    f32 s = p / 4.0f;
    return powf(2.0f, -10.0f * t) * sinf((t - s) * (2.0f * 3.14159f) / p) + 1.0f;
}

f32 EaseOutBounce(f32 t) {
    if (t < 1.0f / 2.75f) {
        return 7.5625f * t * t;
    } else if (t < 2.0f / 2.75f) {
        f32 t1 = t - 1.5f / 2.75f;
        return 7.5625f * t1 * t1 + 0.75f;
    } else if (t < 2.5f / 2.75f) {
        f32 t1 = t - 2.25f / 2.75f;
        return 7.5625f * t1 * t1 + 0.9375f;
    } else {
        f32 t1 = t - 2.625f / 2.75f;
        return 7.5625f * t1 * t1 + 0.984375f;
    }
}

} // namespace Easing

// ============================================================================
// ResourcePool 实现
// ============================================================================

ResourcePool::ResourcePool(u32 maxTextures, u32 maxBuffers, u32 maxDescriptors)
    : maxTextures_(maxTextures)
    , maxBuffers_(maxBuffers) {
}

ResourcePool::~ResourcePool() {
}

TextureHandle ResourcePool::allocateTexture() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ResourceID id;
    if (!freeTextureIDs_.empty()) {
        id = freeTextureIDs_.front();
        freeTextureIDs_.pop();
    } else {
        id = nextTextureID_++;
        if (id > maxTextures_) {
            Internal::LogError("Texture pool exhausted");
            return TextureHandle();
        }
    }
    
    return TextureHandle(id);
}

void ResourcePool::deallocateTexture(TextureHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    textures_.erase(handle.id());
    freeTextureIDs_.push(handle.id());
}

void ResourcePool::setTextureData(TextureHandle handle, Ref<Image> image) {
    std::lock_guard<std::mutex> lock(mutex_);
    textures_[handle.id()] = image;
}

Ref<Image> ResourcePool::getTextureData(TextureHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = textures_.find(handle.id());
    return it != textures_.end() ? it->second : nullptr;
}

BufferHandle ResourcePool::allocateBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ResourceID id;
    if (!freeBufferIDs_.empty()) {
        id = freeBufferIDs_.front();
        freeBufferIDs_.pop();
    } else {
        id = nextBufferID_++;
        if (id > maxBuffers_) {
            Internal::LogError("Buffer pool exhausted");
            return BufferHandle();
        }
    }
    
    return BufferHandle(id);
}

void ResourcePool::deallocateBuffer(BufferHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_.erase(handle.id());
    freeBufferIDs_.push(handle.id());
}

void ResourcePool::setBufferData(BufferHandle handle, Ref<Buffer> buffer) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffers_[handle.id()] = buffer;
}

Ref<Buffer> ResourcePool::getBufferData(BufferHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buffers_.find(handle.id());
    return it != buffers_.end() ? it->second : nullptr;
}

// ============================================================================
// VulkanBackend 实现
// ============================================================================

VulkanBackend::VulkanBackend() {
}

VulkanBackend::~VulkanBackend() {
    shutdown();
}

bool VulkanBackend::initialize(const RendererConfig& config) {
    config_ = config;
    
    Internal::LogInfo("Initializing Vulkan backend...");
    
    if (!createInstance()) return false;
    if (config_.enableValidation && !setupDebugMessenger()) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createDevice()) return false;
    if (!createAllocator()) return false;
    if (!createSwapchain()) return false;
    if (!createImageViews()) return false;
    if (!createRenderPass()) return false;
    if (!createFramebuffers()) return false;
    if (!createCommandPool()) return false;
    if (!createCommandBuffers()) return false;
    if (!createSyncObjects()) return false;
    
    // 初始化资源池
    resourcePool_ = new ResourcePool(config_.maxTextures, config_.maxBuffers, config_.maxDescriptors);
    
    // 初始化 MSDF 生成器
    if (config_.enableMSDF && !initMSDFGenerator()) {
        Internal::LogError("Failed to initialize MSDF generator");
    }
    
    Internal::LogInfo("Vulkan backend initialized successfully");
    Internal::LogInfo("Device: %s", capabilities_.deviceName.c_str());
    
    return true;
}

void VulkanBackend::shutdown() {
    if (!device_) return;
    
    vkDeviceWaitIdle(device_);
    
    // 清理 MSDF
    delete msdfGenerator_;
    msdfGenerator_ = nullptr;
    
    // 清理资源池
    delete resourcePool_;
    resourcePool_ = nullptr;
    
    // 清理字体图集
    delete fontAtlas_;
    fontAtlas_ = nullptr;
    
    // 清理特效
    effects_.clear();
    
    // 清理同步对象
    for (size_t i = 0; i < config_.maxFramesInFlight; i++) {
        vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
        vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    
    // 清理命令缓冲区
    vkFreeCommandBuffers(device_, commandPool_, 
                         static_cast<u32>(commandBuffers_.size()), commandBuffers_.data());
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    
    // 清理 Framebuffers
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    
    // 清理 RenderPass
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    
    // 清理 Swapchain
    cleanupSwapchain();
    
    // 清理 VMA
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    
    // 清理 Device
    vkDestroyDevice(device_, nullptr);
    
    // 清理 Debug Messenger
    if (debugMessenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(instance_, debugMessenger_, nullptr);
        }
    }
    
    // 清理 Instance
    vkDestroyInstance(instance_, nullptr);
    
    device_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    
    Internal::LogInfo("Vulkan backend shutdown complete");
}

bool VulkanBackend::createInstance() {
    // 应用信息
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config_.appName;
    appInfo.applicationVersion = config_.appVersion;
    appInfo.pEngineName = "Nova Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(NOVA_RENDERER_VERSION_MAJOR, 
                                            NOVA_RENDERER_VERSION_MINOR, 
                                            NOVA_RENDERER_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    // 获取所需扩展
    std::vector<const char*> extensions;
    
    // 必需的 Win32 表面扩展
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    
    // 调试扩展
    if (config_.enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    // 用户自定义扩展
    for (auto ext : config_.requiredExtensions) {
        extensions.push_back(ext);
    }
    
    // 验证层
    std::vector<const char*> layers;
    if (config_.enableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    for (auto layer : config_.requiredLayers) {
        layers.push_back(layer);
    }
    
    // 创建实例
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
    
    Internal::LogDebug("Vulkan instance created");
    return true;
}

bool VulkanBackend::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    
    // 回调函数
    auto callback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                       VkDebugUtilsMessageTypeFlagsEXT type,
                       const VkDebugUtilsMessengerCallbackDataEXT* data,
                       void* userData) -> VkBool32 {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            Internal::LogError("Vulkan Validation: %s", data->pMessage);
        } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            Internal::LogInfo("Vulkan Warning: %s", data->pMessage);
        }
        return VK_FALSE;
    };
    
    createInfo.pfnUserCallback = callback;
    createInfo.pUserData = nullptr;
    
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    
    if (func) {
        VK_CHECK(func(instance_, &createInfo, nullptr, &debugMessenger_));
        Internal::LogDebug("Debug messenger set up");
        return true;
    }
    
    Internal::LogError("Failed to set up debug messenger");
    return false;
}

bool VulkanBackend::pickPhysicalDevice() {
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    
    if (deviceCount == 0) {
        Internal::LogError("No Vulkan-compatible GPUs found");
        return false;
    }
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    // 评分选择最佳设备
    i32 bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(device, &features);
        
        i32 score = 0;
        
        // 独立显卡优先
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }
        
        // 集成显卡 (笔记本优化)
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            if (config_.preferIntegratedGPU) {
                score += 2000;
            } else {
                score += 100;
            }
        }
        
        // 纹理大小限制
        score += props.limits.maxImageDimension2D;
        
        // 检查必需功能
        if (!features.fillModeNonSolid) {
            score = -1;  // 不满足要求
        }
        
        if (score > bestScore) {
            bestScore = score;
            bestDevice = device;
        }
    }
    
    if (bestDevice == VK_NULL_HANDLE) {
        Internal::LogError("No suitable GPU found");
        return false;
    }
    
    physicalDevice_ = bestDevice;
    
    // 获取设备属性
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    
    capabilities_.deviceName = props.deviceName;
    capabilities_.maxTextureSize = props.limits.maxImageDimension2D;
    capabilities_.maxComputeWorkGroupSize = props.limits.maxComputeWorkGroupSize[0];
    capabilities_.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    
    // 驱动版本
    char version[64];
    sprintf_s(version, "%d.%d.%d",
        VK_VERSION_MAJOR(props.driverVersion),
        VK_VERSION_MINOR(props.driverVersion),
        VK_VERSION_PATCH(props.driverVersion));
    capabilities_.driverVersion = version;
    
    Internal::LogInfo("Selected GPU: %s", props.deviceName);
    return true;
}

bool VulkanBackend::createDevice() {
    // 查找队列族
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice_, &queueFamilyCount, queueFamilies.data());
    
    // 查找图形队列
    for (u32 i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamily_ = i;
            break;
        }
    }
    
    // 查找计算队列 (优先找专用计算队列)
    for (u32 i = 0; i < queueFamilyCount; i++) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            computeQueueFamily_ = i;
            break;
        }
    }
    if (computeQueueFamily_ == UINT32_MAX) {
        computeQueueFamily_ = graphicsQueueFamily_;  // 回退到图形队列
    }
    
    // 创建 Surface (Win32)
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hwnd = config_.hwnd;
    surfaceInfo.hinstance = GetModuleHandle(nullptr);
    
    VkSurfaceKHR surface;
    VK_CHECK(vkCreateWin32SurfaceKHR(instance_, &surfaceInfo, nullptr, &surface));
    
    // 查找 present 队列
    for (u32 i = 0; i < queueFamilyCount; i++) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice_, i, surface, &presentSupport);
        if (presentSupport) {
            presentQueueFamily_ = i;
            break;
        }
    }
    
    // 去重队列族
    std::set<u32> uniqueQueueFamilies = {
        graphicsQueueFamily_,
        computeQueueFamily_,
        presentQueueFamily_
    };
    
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    f32 queuePriority = 1.0f;
    
    for (u32 queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }
    
    // 设备特性
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    
    // 设备扩展
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
    };
    
    // 创建设备
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    
    VK_CHECK(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));
    
    // 获取队列
    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, computeQueueFamily_, 0, &computeQueue_);
    vkGetDeviceQueue(device_, presentQueueFamily_, 0, &presentQueue_);
    
    // 保存 surface
    // (需要在类中添加 VkSurfaceKHR surface_ 成员)
    
    Internal::LogDebug("Vulkan device created");
    return true;
}

bool VulkanBackend::createAllocator() {
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.instance = instance_;
    allocatorInfo.physicalDevice = physicalDevice_;
    allocatorInfo.device = device_;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    
    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator_));
    
    Internal::LogDebug("VMA allocator created");
    return true;
}

bool VulkanBackend::createSwapchain() {
    // 获取 Surface 能力
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &capabilities);
    
    // 选择 Surface 格式
    u32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());
    
    VkSurfaceFormatKHR selectedFormat = formats[0];
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            selectedFormat = format;
            break;
        }
    }
    swapchainFormat_ = selectedFormat.format;
    
    // 选择 Present Mode
    u32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());
    
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // 默认 VSync
    if (!config_.vsync) {
        for (const auto& mode : presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                presentMode = mode;
                break;
            }
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                presentMode = mode;
            }
        }
    }
    
    // 选择 Extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        swapchainExtent_ = capabilities.currentExtent;
    } else {
        swapchainExtent_.width = max(capabilities.minImageExtent.width, 
                                     min(config_.width, capabilities.maxImageExtent.width));
        swapchainExtent_.height = max(capabilities.minImageExtent.height,
                                      min(config_.height, capabilities.maxImageExtent.height));
    }
    
    // 图像数量
    u32 imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    // 创建 Swapchain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapchainFormat_;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    u32 queueFamilyIndices[] = {graphicsQueueFamily_, presentQueueFamily_};
    if (graphicsQueueFamily_ != presentQueueFamily_) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    VK_CHECK(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_));
    
    // 获取 Swapchain 图像
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
    
    Internal::LogDebug("Swapchain created: %ux%u, %u images",
                       swapchainExtent_.width, swapchainExtent_.height, imageCount);
    return true;
}

void VulkanBackend::cleanupSwapchain() {
    for (auto view : swapchainImageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanBackend::recreateSwapchain() {
    vkDeviceWaitIdle(device_);
    
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createFramebuffers();
}

bool VulkanBackend::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    
    for (size_t i = 0; i < swapchainImages_.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        VK_CHECK(vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]));
    }
    
    return true;
}

bool VulkanBackend::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    
    VK_CHECK(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_));
    
    return true;
}

bool VulkanBackend::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    
    for (size_t i = 0; i < swapchainImageViews_.size(); i++) {
        VkImageView attachments[] = {swapchainImageViews_[i]};
        
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = attachments;
        createInfo.width = swapchainExtent_.width;
        createInfo.height = swapchainExtent_.height;
        createInfo.layers = 1;
        
        VK_CHECK(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]));
    }
    
    return true;
}

bool VulkanBackend::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_));
    
    return true;
}

bool VulkanBackend::createCommandBuffers() {
    commandBuffers_.resize(config_.maxFramesInFlight);
    
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<u32>(commandBuffers_.size());
    
    VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()));
    
    return true;
}

bool VulkanBackend::createSyncObjects() {
    imageAvailableSemaphores_.resize(config_.maxFramesInFlight);
    renderFinishedSemaphores_.resize(config_.maxFramesInFlight);
    inFlightFences_.resize(config_.maxFramesInFlight);
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (size_t i = 0; i < config_.maxFramesInFlight; i++) {
        VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]));
        VK_CHECK(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]));
        VK_CHECK(vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]));
    }
    
    return true;
}

bool VulkanBackend::initMSDFGenerator() {
    msdfGenerator_ = new MSDFGenerator(
        config_.fontAtlasSize,
        config_.msdfPixelRange,
        config_.msdfMargin
    );
    return true;
}

bool VulkanBackend::beginFrame() {
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    
    VkResult result = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX,
        imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE,
        &currentImageIndex_
    );
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return false;
    }
    
    VK_CHECK_FATAL(result);
    
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
    vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
    
    // 开始命令缓冲区录制
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo);
    
    // 开始 RenderPass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[currentImageIndex_];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(commandBuffers_[currentFrame_], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    return true;
}

void VulkanBackend::endFrame() {
    vkCmdEndRenderPass(commandBuffers_[currentFrame_]);
    vkEndCommandBuffer(commandBuffers_[currentFrame_]);
}

void VulkanBackend::present() {
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores_[currentFrame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];
    
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    VK_CHECK_FATAL(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]));
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &currentImageIndex_;
    
    VkResult result = vkQueuePresentKHR(presentQueue_, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
    }
    
    currentFrame_ = (currentFrame_ + 1) % config_.maxFramesInFlight;
}

// ... 更多实现将在后续部分添加

// ============================================================================
// MSDFGenerator 实现
// ============================================================================

struct MSDFGenerator::FontData {
    FT_Face face;
    std::unordered_map<u32, GlyphInfo> glyphs;
    u32 pixelSize;
};

MSDFGenerator::MSDFGenerator(u32 atlasSize, u32 pixelRange, u32 margin)
    : atlasSize_(atlasSize)
    , pixelRange_(pixelRange)
    , margin_(margin) {
    
    // 初始化 FreeType
    FT_Init_FreeType(&ftLibrary_);
    
    // 创建图集纹理
    // atlasTexture_ = ... 创建空白纹理
}

MSDFGenerator::~MSDFGenerator() {
    fonts_.clear();
    FT_Done_FreeType(ftLibrary_);
}

bool MSDFGenerator::loadFont(const FontDesc& desc, FontHandle handle) {
    auto data = std::make_unique<FontData>();
    
    if (FT_New_Face(ftLibrary_, desc.path.c_str(), 0, &data->face)) {
        return false;
    }
    
    FT_Set_Pixel_Sizes(data->face, 0, static_cast<FT_UInt>(desc.pixelSize));
    data->pixelSize = static_cast<u32>(desc.pixelSize);
    
    fonts_[handle.id()] = std::move(data);
    
    if (desc.preloadCommon) {
        pregenerateCommon(handle);
    }
    
    return true;
}

bool MSDFGenerator::generateGlyph(FontHandle font, u32 codepoint, GlyphInfo& outInfo) {
    auto it = fonts_.find(font.id());
    if (it == fonts_.end()) return false;
    
    auto& fontData = it->second;
    
    // 检查是否已生成
    auto glyphIt = fontData->glyphs.find(codepoint);
    if (glyphIt != fontData->glyphs.end()) {
        outInfo = glyphIt->second;
        return true;
    }
    
    // 加载字形
    if (FT_Load_Char(fontData->face, codepoint, FT_LOAD_RENDER)) {
        return false;
    }
    
    auto& glyph = fontData->face->glyph;
    
    // 生成 MSDF
    msdfgen::Shape shape;
    if (msdfgen::loadGlyph(shape, fontData->face, codepoint)) {
        shape.normalize();
        
        f32 scale = fontData->pixelSize / (f32)fontData->face->units_per_EM;
        
        // 计算边界
        msdfgen::Bounds<f32> bounds = shape.getBounds();
        u32 width = static_cast<u32>((bounds.r - bounds.l) * scale + 2 * margin_);
        u32 height = static_cast<u32>((bounds.t - bounds.b) * scale + 2 * margin_);
        
        // 分配图集空间
        Rect atlasRect;
        if (!atlasAllocator_.allocate(width, height, atlasSize_, atlasRect)) {
            return false;
        }
        
        // 生成 MSDF
        msdfgen::Bitmap<msdfgen::FloatRGB> msdf(width, height);
        msdfgen::SDFTransformation transformation(
            msdfgen::Projection(scale, msdfgen::Vector2(-bounds.l * scale + margin_, -bounds.b * scale + margin_)),
            msdfgen::Range(pixelRange_)
        );
        msdfgen::generateMSDF(msdf, shape, transformation);
        
        // 上传到图集纹理
        // ...
        
        // 保存字形信息
        GlyphInfo info;
        info.glyphIndex = codepoint;
        info.size = {static_cast<f32>(width), static_cast<f32>(height)};
        info.bearing = {static_cast<f32>(glyph->bitmap_left), static_cast<f32>(glyph->bitmap_top)};
        info.advance = static_cast<f32>(glyph->advance.x >> 6);
        info.atlasRect = atlasRect;
        info.hasMSDF = true;
        
        fontData->glyphs[codepoint] = info;
        outInfo = info;
        
        return true;
    }
    
    return false;
}

void MSDFGenerator::pregenerateCommon(FontHandle font) {
    // 预生成常用字符 (ASCII + 常用中文)
    for (u32 c = 0x0020; c <= 0x007F; c++) {
        GlyphInfo info;
        generateGlyph(font, c, info);
    }
    
    // 常用中文
    const u32 commonChinese[] = {
        0x7684, 0x4E00, 0x4E8C, 0x4E09, 0x56DB, 0x4E94, 0x516D,
        0x4E03, 0x516B, 0x4E5D, 0x5341, 0x767E, 0x5343, 0x4E07,
        // ... 更多常用字
    };
    
    for (u32 c : commonChinese) {
        GlyphInfo info;
        generateGlyph(font, c, info);
    }
}

bool MSDFGenerator::AtlasAllocator::allocate(u32 width, u32 height, u32 atlasSize, Rect& outRect) {
    if (currentX + width > atlasSize) {
        currentX = 0;
        currentY += rowHeight;
        rowHeight = 0;
    }
    
    if (currentY + height > atlasSize) {
        return false;  // 图集已满
    }
    
    outRect.x = static_cast<f32>(currentX);
    outRect.y = static_cast<f32>(currentY);
    outRect.width = static_cast<f32>(width);
    outRect.height = static_cast<f32>(height);
    
    currentX += width;
    rowHeight = max(rowHeight, height);
    
    return true;
}

// ============================================================================
// Renderer 实现
// ============================================================================

namespace {
    Renderer* g_renderer = nullptr;
}

Renderer* GetRenderer() { return g_renderer; }
void SetRenderer(Renderer* renderer) { g_renderer = renderer; }

Renderer::Renderer() {
    SetRenderer(this);
}

Renderer::~Renderer() {
    shutdown();
    if (GetRenderer() == this) {
        SetRenderer(nullptr);
    }
}

bool Renderer::initialize(const RendererConfig& config) {
    config_ = config;
    
    if (!selectBackend()) {
        Internal::LogError("Failed to select rendering backend");
        return false;
    }
    
    if (!backend_->initialize(config)) {
        Internal::LogError("Failed to initialize backend");
        return false;
    }
    
    initialized_ = true;
    Internal::LogInfo("Renderer initialized with %s backend", backend_->name());
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) return;
    
    backend_->shutdown();
    backend_.reset();
    initialized_ = false;
    
    Internal::LogInfo("Renderer shutdown");
}

bool Renderer::selectBackend() {
    if (config_.preferredBackend == BackendType::Auto) {
        // 自动选择：Vulkan > D3D11 > GDI
        if (tryVulkanBackend()) return true;
        if (tryD3D11Backend()) return true;
        if (tryGDIBackend()) return true;
        return false;
    }
    
    switch (config_.preferredBackend) {
        case BackendType::Vulkan:
            return tryVulkanBackend();
        case BackendType::D3D11:
            return tryD3D11Backend();
        case BackendType::GDI:
            return tryGDIBackend();
        default:
            return false;
    }
}

bool Renderer::tryVulkanBackend() {
    backend_ = std::make_unique<VulkanBackend>();
    return true;  // 实际初始化在 initialize 中
}

bool Renderer::tryD3D11Backend() {
    // TODO: 实现 D3D11 后端
    return false;
}

bool Renderer::tryGDIBackend() {
    // TODO: 实现 GDI 后端
    return false;
}

bool Renderer::beginFrame() {
    return backend_->beginFrame();
}

void Renderer::endFrame() {
    backend_->endFrame();
}

void Renderer::present() {
    backend_->present();
}

BackendType Renderer::currentBackend() const {
    return backend_->type();
}

const BackendCapabilities& Renderer::capabilities() const {
    return backend_->capabilities();
}

const RenderBackend::Stats& Renderer::stats() const {
    return backend_->stats();
}

void Renderer::resetStats() {
    backend_->resetStats();
}

void Renderer::resize(u32 width, u32 height) {
    config_.width = width;
    config_.height = height;
    backend_->resize(width, height);
}

void Renderer::updateAnimations(f32 deltaTime) {
    // 动画更新在各个 AnimValue 中自动处理
    // 这里可以添加全局动画管理器
}

// 即时模式绘制 API (简化实现)

void Renderer::drawRect(const Rect& rect, const Color& color) {
    // TODO: 使用预编译的着色器特效
}

void Renderer::drawRoundRect(const Rect& rect, f32 radius, const Color& color) {
    // TODO: 使用圆角矩形着色器
}

void Renderer::drawText(const TextRun& text) {
    backend_->drawText(text);
}

} // namespace Nova

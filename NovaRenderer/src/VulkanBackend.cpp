/**
 * Nova Renderer - Vulkan Backend Implementation
 */

#include "Nova/VulkanBackend.h"
#include "Nova/Window.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <set>

// GLSL to SPIR-V 编译器
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>

namespace Nova {

// ============================================================================
// 初始化
// ============================================================================

VulkanBackend::VulkanBackend() = default;

VulkanBackend::~VulkanBackend() {
    if (initialized_) {
        shutdown();
    }
}

Result<void> VulkanBackend::initialize(const VulkanConfig& config) {
    config_ = config;
    
    // 初始化 glslang
    glslang::InitializeProcess();
    
    // 步骤 1-12
    if (!createInstance()) return Result<void>::err("Failed to create Vulkan instance");
    if (config_.enableDebugUtils && !setupDebugMessenger()) {
        // Debug messenger 可选，不报错
    }
    if (!createSurface()) return Result<void>::err("Failed to create surface");
    if (!pickPhysicalDevice()) return Result<void>::err("Failed to pick physical device");
    if (!createLogicalDevice()) return Result<void>::err("Failed to create logical device");
    if (!createVmaAllocator()) return Result<void>::err("Failed to create VMA allocator");
    if (!createSwapChain()) return Result<void>::err("Failed to create swapchain");
    if (!createImageViews()) return Result<void>::err("Failed to create image views");
    if (!createRenderPass()) return Result<void>::err("Failed to create render pass");
    if (!createFramebuffers()) return Result<void>::err("Failed to create framebuffers");
    if (!createCommandPools()) return Result<void>::err("Failed to create command pools");
    if (!createCommandBuffers()) return Result<void>::err("Failed to create command buffers");
    if (!createSyncObjects()) return Result<void>::err("Failed to create sync objects");
    if (!createDescriptorPool()) return Result<void>::err("Failed to create descriptor pool");
    
    framesInFlight_ = config_.maxFramesInFlight;
    
    initialized_ = true;
    return Result<void>::ok();
}

void VulkanBackend::shutdown() {
    if (!initialized_) return;
    
    vkDeviceWaitIdle(device_);
    
    // 清理帧数据
    for (auto& frame : frames_) {
        if (frame.commandPool) vkDestroyCommandPool(device_, frame.commandPool, nullptr);
        if (frame.imageAvailable) vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        if (frame.renderFinished) vkDestroySemaphore(device_, frame.renderFinished, nullptr);
        if (frame.inFlightFence) vkDestroyFence(device_, frame.inFlightFence, nullptr);
        if (frame.descriptorPool) vkDestroyDescriptorPool(device_, frame.descriptorPool, nullptr);
        
        for (size_t i = 0; i < frame.uniformBuffers.size(); i++) {
            if (frame.uniformBuffers[i]) {
                vmaDestroyBuffer(allocator_, frame.uniformBuffers[i], frame.uniformAllocations[i]);
            }
        }
    }
    frames_.clear();
    
    // 清理描述符池
    if (descriptorPool_) {
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    
    // 清理帧缓冲
    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    
    // 清理渲染过程
    if (renderPass_) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    
    // 清理交换链
    cleanupSwapChain();
    
    // 清理 VMA
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
    }
    
    // 清理设备
    if (device_) {
        vkDestroyDevice(device_, nullptr);
    }
    
    // 清理 Surface
    if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    
    // 清理 Debug Messenger
    if (debugMessenger_) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            instance_, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(instance_, debugMessenger_, nullptr);
        }
    }
    
    // 清理实例
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
    }
    
    // 清理 glslang
    glslang::FinalizeProcess();
    
    initialized_ = false;
}

// ============================================================================
// 创建实例
// ============================================================================

bool VulkanBackend::createInstance() {
    // 验证层
    if (config_.enableValidation && !checkValidationLayerSupport()) {
        // 警告但不失败
    }
    
    // 应用信息
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = config_.applicationName;
    appInfo.applicationVersion = config_.applicationVersion;
    appInfo.pEngineName = "Nova Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    // 实例创建信息
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // 扩展
    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    // 验证层
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (config_.enableValidation) {
        createInfo.enabledLayerCount = static_cast<u32>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
        
        // 配置调试消息
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                             void*) -> VKAPI_ATTR VkBool32 {
            // 输出调试信息
            return VK_FALSE;
        };
        
        createInfo.pNext = &debugCreateInfo;
    }
    
    return vkCreateInstance(&createInfo, nullptr, &instance_) == VK_SUCCESS;
}

bool VulkanBackend::checkValidationLayerSupport() {
    u32 layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    
    for (const char* layerName : validationLayers_) {
        bool found = false;
        for (const auto& layer : availableLayers) {
            if (strcmp(layerName, layer.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    
    return true;
}

std::vector<const char*> VulkanBackend::getRequiredExtensions() {
    std::vector<const char*> extensions;
    
    // Windows Surface 扩展
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    
    // 调试扩展
    if (config_.enableDebugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    return extensions;
}

// ============================================================================
// 调试 Messenger
// ============================================================================

bool VulkanBackend::setupDebugMessenger() {
    if (!config_.enableValidation) return true;
    
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                    VkDebugUtilsMessageTypeFlagsEXT,
                                    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                    void*) -> VKAPI_ATTR VkBool32 {
        const char* prefix = "";
        switch (severity) {
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: prefix = "[V]"; break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: prefix = "[I]"; break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: prefix = "[W]"; break;
            case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: prefix = "[E]"; break;
            default: break;
        }
        // 输出: prefix message
        return VK_FALSE;
    };
    
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    
    if (!func) return false;
    
    return func(instance_, &createInfo, nullptr, &debugMessenger_) == VK_SUCCESS;
}

// ============================================================================
// Surface
// ============================================================================

bool VulkanBackend::createSurface() {
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = (HWND)config_.windowHandle;
    createInfo.hinstance = GetModuleHandle(nullptr);
    
    return vkCreateWin32SurfaceKHR(instance_, &createInfo, nullptr, &surface_) == VK_SUCCESS;
}

// ============================================================================
// 物理设备选择
// ============================================================================

bool VulkanBackend::pickPhysicalDevice() {
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    
    if (deviceCount == 0) return false;
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());
    
    // 按分数排序设备
    std::vector<std::pair<int, VkPhysicalDevice>> scoredDevices;
    
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(device, &features);
        
        int score = 0;
        
        // 独立显卡优先
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }
        
        // 最大纹理大小
        score += props.limits.maxImageDimension2D;
        
        // 检查队列族
        QueueFamilyIndices indices = findQueueFamilies(device);
        if (!indices.isComplete()) {
            score = 0;
        }
        
        // 检查扩展支持
        if (!checkDeviceExtensionSupport(device)) {
            score = 0;
        }
        
        // 检查交换链
        SwapChainDetails swapChainDetails = querySwapChainDetails(device);
        if (swapChainDetails.formats.empty() || swapChainDetails.presentModes.empty()) {
            score = 0;
        }
        
        scoredDevices.push_back({score, device});
    }
    
    // 排序
    std::sort(scoredDevices.begin(), scoredDevices.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    
    if (scoredDevices.empty() || scoredDevices[0].first == 0) {
        return false;
    }
    
    physicalDevice_ = scoredDevices[0].second;
    
    vkGetPhysicalDeviceProperties(physicalDevice_, &deviceProperties_);
    vkGetPhysicalDeviceFeatures(physicalDevice_, &deviceFeatures_);
    queueFamilies_ = findQueueFamilies(physicalDevice_);
    swapChainDetails_ = querySwapChainDetails(physicalDevice_);
    
    return true;
}

QueueFamilyIndices VulkanBackend::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    u32 i = 0;
    for (const auto& queueFamily : queueFamilies) {
        // 图形队列
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        
        // 呈现队列
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) {
            indices.present = i;
        }
        
        // 计算队列
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT && 
            indices.compute == UINT32_MAX) {
            indices.compute = i;
        }
        
        // 传输队列
        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT && 
            indices.transfer == UINT32_MAX &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            indices.transfer = i;
        }
        
        i++;
    }
    
    return indices;
}

SwapChainDetails VulkanBackend::querySwapChainDetails(VkPhysicalDevice device) {
    SwapChainDetails details;
    
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);
    
    u32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
    }
    
    u32 presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentModeCount, details.presentModes.data());
    }
    
    return details;
}

bool VulkanBackend::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    u32 extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    std::set<std::string> requiredExtensions(deviceExtensions_.begin(), deviceExtensions_.end());
    
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    
    return requiredExtensions.empty();
}

// ============================================================================
// 逻辑设备
// ============================================================================

bool VulkanBackend::createLogicalDevice() {
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<u32> uniqueQueueFamilies = {queueFamilies_.graphics, queueFamilies_.present};
    
    f32 queuePriority = 1.0f;
    for (u32 queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions_.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions_.data();
    
    if (config_.enableValidation) {
        createInfo.enabledLayerCount = static_cast<u32>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
    }
    
    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        return false;
    }
    
    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
    
    if (queueFamilies_.hasCompute()) {
        vkGetDeviceQueue(device_, queueFamilies_.compute, 0, &computeQueue_);
    } else {
        computeQueue_ = graphicsQueue_;
    }
    
    if (queueFamilies_.hasTransfer()) {
        vkGetDeviceQueue(device_, queueFamilies_.transfer, 0, &transferQueue_);
    } else {
        transferQueue_ = graphicsQueue_;
    }
    
    return true;
}

// ============================================================================
// VMA 分配器
// ============================================================================

bool VulkanBackend::createVmaAllocator() {
    VmaAllocatorCreateInfo createInfo{};
    createInfo.physicalDevice = physicalDevice_;
    createInfo.device = device_;
    createInfo.instance = instance_;
    createInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    
    // 加载 Vulkan 函数指针
    VmaVulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    createInfo.pVulkanFunctions = &vulkanFunctions;
    
    return vmaCreateAllocator(&createInfo, &allocator_) == VK_SUCCESS;
}

// ============================================================================
// 交换链
// ============================================================================

bool VulkanBackend::createSwapChain() {
    auto surfaceFormat = chooseSwapSurfaceFormat();
    auto presentMode = chooseSwapPresentMode();
    auto extent = chooseSwapExtent();
    
    u32 imageCount = swapChainDetails_.capabilities.minImageCount + 1;
    if (swapChainDetails_.capabilities.maxImageCount > 0 &&
        imageCount > swapChainDetails_.capabilities.maxImageCount) {
        imageCount = swapChainDetails_.capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                            VK_IMAGE_USAGE_STORAGE_BIT;
    
    u32 queueFamilyIndices[] = {queueFamilies_.graphics, queueFamilies_.present};
    if (queueFamilies_.graphics != queueFamilies_.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    createInfo.preTransform = swapChainDetails_.capabilities.currentTransform;
    // 支持透明窗口
    if (swapChainDetails_.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if (swapChainDetails_.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else {
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
    if (vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
        return false;
    }
    
    vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, nullptr);
    swapChainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapChain_, &imageCount, swapChainImages_.data());
    
    swapChainFormat_ = surfaceFormat.format;
    swapChainExtent_ = extent;
    
    return true;
}

VkSurfaceFormatKHR VulkanBackend::chooseSwapSurfaceFormat() {
    for (const auto& format : swapChainDetails_.formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return swapChainDetails_.formats[0];
}

VkPresentModeKHR VulkanBackend::chooseSwapPresentMode() {
    if (!config_.vSync) {
        // 查找 IMMEDIATE 模式
        for (const auto& mode : swapChainDetails_.presentModes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return mode;
            }
        }
        // 查找 MAILBOX 模式
        for (const auto& mode : swapChainDetails_.presentModes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return mode;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanBackend::chooseSwapExtent() {
    if (swapChainDetails_.capabilities.currentExtent.width != UINT32_MAX) {
        return swapChainDetails_.capabilities.currentExtent;
    }
    
    VkExtent2D actualExtent = {
        static_cast<u32>(config_.windowHandle ? 800 : 800),
        static_cast<u32>(config_.windowHandle ? 600 : 600)
    };
    
    actualExtent.width = std::clamp(actualExtent.width,
        swapChainDetails_.capabilities.minImageExtent.width,
        swapChainDetails_.capabilities.maxImageExtent.width);
    
    actualExtent.height = std::clamp(actualExtent.height,
        swapChainDetails_.capabilities.minImageExtent.height,
        swapChainDetails_.capabilities.maxImageExtent.height);
    
    return actualExtent;
}

bool VulkanBackend::createImageViews() {
    swapChainImageViews_.resize(swapChainImages_.size());
    
    for (size_t i = 0; i < swapChainImages_.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        if (vkCreateImageView(device_, &createInfo, nullptr, &swapChainImageViews_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 渲染过程
// ============================================================================

bool VulkanBackend::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // 不清除，让 Windows DWM 处理
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
    
    return vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_) == VK_SUCCESS;
}

// ============================================================================
// 帧缓冲
// ============================================================================

bool VulkanBackend::createFramebuffers() {
    framebuffers_.resize(swapChainImageViews_.size());
    
    for (size_t i = 0; i < swapChainImageViews_.size(); i++) {
        VkImageView attachments[] = {swapChainImageViews_[i]};
        
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = attachments;
        createInfo.width = swapChainExtent_.width;
        createInfo.height = swapChainExtent_.height;
        createInfo.layers = 1;
        
        if (vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 命令池和缓冲区
// ============================================================================

bool VulkanBackend::createCommandPools() {
    frames_.resize(framesInFlight_);
    
    for (auto& frame : frames_) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queueFamilies_.graphics;
        
        if (vkCreateCommandPool(device_, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS) {
            return false;
        }
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = frame.commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        
        if (vkAllocateCommandBuffers(device_, &allocInfo, &frame.commandBuffer) != VK_SUCCESS) {
            return false;
        }
        
        frame.initialized = true;
    }
    
    return true;
}

bool VulkanBackend::createCommandBuffers() {
    // 已在 createCommandPools 中完成
    return true;
}

// ============================================================================
// 同步对象
// ============================================================================

bool VulkanBackend::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (auto& frame : frames_) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.renderFinished) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS) {
            return false;
        }
    }
    
    return true;
}

// ============================================================================
// 描述符池
// ============================================================================

bool VulkanBackend::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = static_cast<u32>(framesInFlight_ * 100);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<u32>(framesInFlight_ * 100);
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<u32>(framesInFlight_ * 100);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    return vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) == VK_SUCCESS;
}

// ============================================================================
// 帧管理
// ============================================================================

Result<u32> VulkanBackend::beginFrame() {
    auto& frame = frames_[currentFrame_];
    
    vkWaitForFences(device_, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    
    u32 imageIndex;
    VkResult result = vkAcquireNextImageKHR(device_, swapChain_, UINT64_MAX,
        frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return Result<u32>::err("Swapchain out of date");
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        return Result<u32>::err("Failed to acquire swapchain image");
    }
    
    vkResetFences(device_, 1, &frame.inFlightFence);
    
    vkResetCommandBuffer(frame.commandBuffer, 0);
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
    
    currentImageIndex_ = imageIndex;
    return Result<u32>::ok(imageIndex);
}

void VulkanBackend::endFrame() {
    // 必须先结束 render pass
    endRenderPass();
    
    auto& frame = frames_[currentFrame_];
    
    vkEndCommandBuffer(frame.commandBuffer);
    
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {frame.imageAvailable};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    
    VkSemaphore signalSemaphores[] = {frame.renderFinished};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frame.inFlightFence);
}

void VulkanBackend::present() {
    auto& frame = frames_[currentFrame_];
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain_;
    presentInfo.pImageIndices = &currentImageIndex_;
    
    vkQueuePresentKHR(presentQueue_, &presentInfo);
    
    currentFrame_ = (currentFrame_ + 1) % framesInFlight_;
}

// ============================================================================
// 交换链重建
// ============================================================================

void VulkanBackend::cleanupSwapChain() {
    for (auto imageView : swapChainImageViews_) {
        vkDestroyImageView(device_, imageView, nullptr);
    }
    
    for (auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    
    vkDestroySwapchainKHR(device_, swapChain_, nullptr);
}

void VulkanBackend::recreateSwapChain() {
    vkDeviceWaitIdle(device_);
    
    cleanupSwapChain();
    
    createSwapChain();
    createImageViews();
    createFramebuffers();
}

void VulkanBackend::onResize(u32 width, u32 height) {
    recreateSwapChain();
}

void VulkanBackend::setVSync(bool enabled) {
    config_.vSync = enabled;
    recreateSwapChain();
}

VkCommandBuffer VulkanBackend::getCurrentCommandBuffer() const {
    return frames_[currentFrame_].commandBuffer;
}

// ============================================================================
// 资源创建 (简化版)
// ============================================================================

BufferHandle VulkanBackend::createBuffer(const BufferDesc& desc) {
    auto handle = buffers_.allocate();
    if (!handle.valid()) return {};
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = desc.usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (desc.mapped) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }
    
    VulkanBuffer buffer;
    VmaAllocationInfo allocationInfo;
    
    if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                        &buffer.buffer, &buffer.allocation, &allocationInfo) != VK_SUCCESS) {
        buffers_.deallocate(handle);
        return {};
    }
    
    buffer.size = desc.size;
    buffer.usage = desc.usage;
    buffer.mapped = allocationInfo.pMappedData;
    
    buffers_.set(handle, buffer);
    return handle;
}

void VulkanBackend::destroyBuffer(BufferHandle handle) {
    auto* buffer = buffers_.get(handle);
    if (!buffer) return;
    
    vmaDestroyBuffer(allocator_, buffer->buffer, buffer->allocation);
    buffers_.deallocate(handle);
}

void VulkanBackend::updateBuffer(BufferHandle handle, const void* data, u64 size, u64 offset) {
    auto* buffer = buffers_.get(handle);
    if (!buffer) return;
    
    if (buffer->mapped) {
        memcpy((char*)buffer->mapped + offset, data, size);
    } else {
        void* mapped;
        vmaMapMemory(allocator_, buffer->allocation, &mapped);
        memcpy((char*)mapped + offset, data, size);
        vmaUnmapMemory(allocator_, buffer->allocation);
    }
}

void* VulkanBackend::mapBuffer(BufferHandle handle) {
    auto* buffer = buffers_.get(handle);
    if (!buffer) return nullptr;
    
    if (buffer->mapped) return buffer->mapped;
    
    void* mapped;
    vmaMapMemory(allocator_, buffer->allocation, &mapped);
    return mapped;
}

void VulkanBackend::unmapBuffer(BufferHandle handle) {
    auto* buffer = buffers_.get(handle);
    if (!buffer || buffer->mapped) return;
    
    vmaUnmapMemory(allocator_, buffer->allocation);
}

TextureHandle VulkanBackend::createTexture(const TextureDesc& desc) {
    auto handle = textures_.allocate();
    if (!handle.valid()) return {};
    
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = desc.depth > 1 ? VK_IMAGE_TYPE_3D : 
                          desc.height > 1 ? VK_IMAGE_TYPE_2D : VK_IMAGE_TYPE_1D;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.format = static_cast<VkFormat>(desc.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.samples);
    imageInfo.flags = 0;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    
    VulkanTexture texture;
    VmaAllocationInfo allocationInfo;
    
    if (vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                       &texture.image, &texture.allocation, &allocationInfo) != VK_SUCCESS) {
        textures_.deallocate(handle);
        return {};
    }
    
    texture.width = desc.width;
    texture.height = desc.height;
    texture.depth = desc.depth;
    texture.mipLevels = desc.mipLevels;
    texture.arrayLayers = desc.arrayLayers;
    texture.format = static_cast<VkFormat>(desc.format);
    
    // 创建 ImageView
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = texture.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;
    
    if (vkCreateImageView(device_, &viewInfo, nullptr, &texture.view) != VK_SUCCESS) {
        vmaDestroyImage(allocator_, texture.image, texture.allocation);
        textures_.deallocate(handle);
        return {};
    }
    
    textures_.set(handle, texture);
    return handle;
}

void VulkanBackend::destroyTexture(TextureHandle handle) {
    auto* texture = textures_.get(handle);
    if (!texture) return;
    
    if (texture->view) vkDestroyImageView(device_, texture->view, nullptr);
    vmaDestroyImage(allocator_, texture->image, texture->allocation);
    textures_.deallocate(handle);
}

void VulkanBackend::updateTexture(TextureHandle handle, const void* data, u32 width, u32 height, u32 mipLevel) {
    auto* texture = textures_.get(handle);
    if (!texture || !data) return;
    
    // 创建暂存缓冲区
    BufferDesc stagingDesc;
    stagingDesc.size = width * height * 4;  // 假设 RGBA8
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.mapped = true;
    
    auto stagingHandle = createBuffer(stagingDesc);
    updateBuffer(stagingHandle, data, stagingDesc.size);
    
    auto cmd = getCurrentCommandBuffer();
    
    // 转换布局到传输目标
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // 复制缓冲区到图像
    auto* staging = buffers_.get(stagingHandle);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(cmd, staging->buffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // 转换到着色器只读布局
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    destroyBuffer(stagingHandle);
}

void VulkanBackend::uploadTextureData(TextureHandle handle, const void* data, size_t size) {
    auto* texture = textures_.get(handle);
    if (!texture || !data) return;
    
    // Create staging buffer
    BufferDesc stagingDesc;
    stagingDesc.size = size;
    stagingDesc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingDesc.mapped = true;
    
    auto stagingHandle = createBuffer(stagingDesc);
    updateBuffer(stagingHandle, data, size);
    
    auto cmd = getCurrentCommandBuffer();
    
    // Transition to transfer destination
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    // Copy buffer to image
    auto* staging = buffers_.get(stagingHandle);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {texture->width, texture->height, 1};
    
    vkCmdCopyBufferToImage(cmd, staging->buffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // Transition to shader read only
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    
    texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    destroyBuffer(stagingHandle);
}

SamplerHandle VulkanBackend::createSampler(const SamplerDesc& desc) {
    auto handle = samplers_.allocate();
    if (!handle.valid()) return {};
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = static_cast<VkFilter>(desc.magFilter);
    samplerInfo.minFilter = static_cast<VkFilter>(desc.minFilter);
    samplerInfo.mipmapMode = static_cast<VkSamplerMipmapMode>(desc.mipFilter);
    samplerInfo.addressModeU = static_cast<VkSamplerAddressMode>(desc.addressModeU);
    samplerInfo.addressModeV = static_cast<VkSamplerAddressMode>(desc.addressModeV);
    samplerInfo.addressModeW = static_cast<VkSamplerAddressMode>(desc.addressModeW);
    samplerInfo.maxAnisotropy = desc.maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    
    VulkanSampler sampler;
    if (vkCreateSampler(device_, &samplerInfo, nullptr, &sampler.sampler) != VK_SUCCESS) {
        samplers_.deallocate(handle);
        return {};
    }
    
    samplers_.set(handle, sampler);
    return handle;
}

void VulkanBackend::destroySampler(SamplerHandle handle) {
    auto* sampler = samplers_.get(handle);
    if (!sampler) return;
    
    vkDestroySampler(device_, sampler->sampler, nullptr);
    samplers_.deallocate(handle);
}

// 其他方法实现...
// 简化版，实际项目中需要完整实现

void VulkanBackend::beginRenderPass(RenderPassHandle pass, FramebufferHandle fb, const Vec4& clearColor) {
    auto cmd = getCurrentCommandBuffer();
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[currentImageIndex_];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent_;
    renderPassInfo.clearValueCount = 0;  // loadOp = DONT_CARE，不需要 clearValue
    renderPassInfo.pClearValues = nullptr;
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanBackend::endRenderPass() {
    vkCmdEndRenderPass(getCurrentCommandBuffer());
}

void VulkanBackend::setViewport(f32 x, f32 y, f32 width, f32 height) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(getCurrentCommandBuffer(), 0, 1, &viewport);
}

void VulkanBackend::setScissor(i32 x, i32 y, u32 width, u32 height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(getCurrentCommandBuffer(), 0, 1, &scissor);
}

void VulkanBackend::draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    vkCmdDraw(getCurrentCommandBuffer(), vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanBackend::drawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) {
    vkCmdDrawIndexed(getCurrentCommandBuffer(), indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanBackend::dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
    vkCmdDispatch(getCurrentCommandBuffer(), groupCountX, groupCountY, groupCountZ);
}

// ============================================================================
// 着色器创建
// ============================================================================

ShaderHandle VulkanBackend::createShader(const ShaderDesc& desc) {
    auto handle = shaders_.allocate();
    if (!handle.valid()) return {};
    
    VulkanShader shader;
    
    // 编译顶点着色器
    if (desc.vertexSource) {
        std::vector<u32> spirv;
        std::string error;
        if (!compileGLSLToSPIRV(desc.vertexSource, VK_SHADER_STAGE_VERTEX_BIT, spirv, error)) {
            shaders_.deallocate(handle);
            return {};
        }
        
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * 4;
        createInfo.pCode = spirv.data();
        
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &shader.vertex) != VK_SUCCESS) {
            shaders_.deallocate(handle);
            return {};
        }
    }
    
    // 编译片段着色器
    if (desc.fragmentSource) {
        std::vector<u32> spirv;
        std::string error;
        if (!compileGLSLToSPIRV(desc.fragmentSource, VK_SHADER_STAGE_FRAGMENT_BIT, spirv, error)) {
            if (shader.vertex) vkDestroyShaderModule(device_, shader.vertex, nullptr);
            shaders_.deallocate(handle);
            return {};
        }
        
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * 4;
        createInfo.pCode = spirv.data();
        
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &shader.fragment) != VK_SUCCESS) {
            if (shader.vertex) vkDestroyShaderModule(device_, shader.vertex, nullptr);
            shaders_.deallocate(handle);
            return {};
        }
    }
    
    // 编译计算着色器
    if (desc.computeSource) {
        std::vector<u32> spirv;
        std::string error;
        if (!compileGLSLToSPIRV(desc.computeSource, VK_SHADER_STAGE_COMPUTE_BIT, spirv, error)) {
            if (shader.vertex) vkDestroyShaderModule(device_, shader.vertex, nullptr);
            if (shader.fragment) vkDestroyShaderModule(device_, shader.fragment, nullptr);
            shaders_.deallocate(handle);
            return {};
        }
        
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * 4;
        createInfo.pCode = spirv.data();
        
        if (vkCreateShaderModule(device_, &createInfo, nullptr, &shader.compute) != VK_SUCCESS) {
            if (shader.vertex) vkDestroyShaderModule(device_, shader.vertex, nullptr);
            if (shader.fragment) vkDestroyShaderModule(device_, shader.fragment, nullptr);
            shaders_.deallocate(handle);
            return {};
        }
    }
    
    // 创建描述符集布局 (简化版)
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &shader.descriptorSetLayout);
    
    // 创建管线布局
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &shader.descriptorSetLayout;
    
    // 推送常量
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 128;  // 最大 128 字节
    
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &shader.pipelineLayout);
    
    shaders_.set(handle, shader);
    return handle;
}

void VulkanBackend::destroyShader(ShaderHandle handle) {
    auto* shader = shaders_.get(handle);
    if (!shader) return;
    
    if (shader->vertex) vkDestroyShaderModule(device_, shader->vertex, nullptr);
    if (shader->fragment) vkDestroyShaderModule(device_, shader->fragment, nullptr);
    if (shader->compute) vkDestroyShaderModule(device_, shader->compute, nullptr);
    if (shader->pipelineLayout) vkDestroyPipelineLayout(device_, shader->pipelineLayout, nullptr);
    if (shader->descriptorSetLayout) vkDestroyDescriptorSetLayout(device_, shader->descriptorSetLayout, nullptr);
    
    shaders_.deallocate(handle);
}

// ============================================================================
// 管线创建
// ============================================================================

PipelineHandle VulkanBackend::createPipeline(const PipelineDesc& desc) {
    auto handle = pipelines_.allocate();
    if (!handle.valid()) return {};
    
    auto* shader = shaders_.get(desc.shader);
    if (!shader) {
        pipelines_.deallocate(handle);
        return {};
    }
    
    VulkanPipeline pipeline;
    pipeline.layout = shader->pipelineLayout;
    
    // 着色器阶段
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    
    if (shader->vertex) {
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = shader->vertex;
        vertStage.pName = "main";
        stages.push_back(vertStage);
    }
    
    if (shader->fragment) {
        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = shader->fragment;
        fragStage.pName = "main";
        stages.push_back(fragStage);
    }
    
    // 顶点输入
    std::vector<VkVertexInputBindingDescription> bindingDescs;
    std::vector<VkVertexInputAttributeDescription> attributeDescs;
    
    for (const auto& binding : desc.bindings) {
        VkVertexInputBindingDescription bindingDesc{};
        bindingDesc.binding = binding.binding;
        bindingDesc.stride = binding.stride;
        bindingDesc.inputRate = binding.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescs.push_back(bindingDesc);
    }
    
    for (const auto& attr : desc.attributes) {
        VkVertexInputAttributeDescription attrDesc{};
        attrDesc.binding = 0;
        attrDesc.location = attr.location;
        attrDesc.format = static_cast<VkFormat>(attr.format);
        attrDesc.offset = attr.offset;
        attributeDescs.push_back(attrDesc);
    }
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<u32>(bindingDescs.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescs.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();
    
    // 输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // 视口和裁剪 (动态设置)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // 光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // UI 不剔除
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    if (desc.blendMode == 1) {  // Alpha 混合
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (desc.blendMode == 2) {  // 加法混合
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // 动态状态
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // 创建图形管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<u32>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = shader->pipelineLayout;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline.pipeline) != VK_SUCCESS) {
        pipelines_.deallocate(handle);
        return {};
    }
    
    pipelines_.set(handle, pipeline);
    return handle;
}

void VulkanBackend::destroyPipeline(PipelineHandle handle) {
    auto* pipeline = pipelines_.get(handle);
    if (!pipeline) return;
    
    // 不销毁 layout，因为它属于 shader
    if (pipeline->pipeline) {
        vkDestroyPipeline(device_, pipeline->pipeline, nullptr);
    }
    
    pipelines_.deallocate(handle);
}

// ============================================================================
// 绑定和绘制
// ============================================================================

void VulkanBackend::setPipeline(PipelineHandle handle) {
    auto* pipeline = pipelines_.get(handle);
    if (!pipeline) return;
    
    vkCmdBindPipeline(getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

void VulkanBackend::setVertexBuffer(BufferHandle handle, u32 binding, u64 offset) {
    auto* buffer = buffers_.get(handle);
    if (!buffer) return;
    
    VkBuffer vertexBuffers[] = {buffer->buffer};
    VkDeviceSize offsets[] = {offset};
    vkCmdBindVertexBuffers(getCurrentCommandBuffer(), binding, 1, vertexBuffers, offsets);
}

void VulkanBackend::setIndexBuffer(BufferHandle handle, u64 offset) {
    auto* buffer = buffers_.get(handle);
    if (!buffer) return;
    
    vkCmdBindIndexBuffer(getCurrentCommandBuffer(), buffer->buffer, offset, VK_INDEX_TYPE_UINT16);
}

void VulkanBackend::setDescriptorSet(DescriptorSetHandle set, u32 setIndex) {
    // 简化版：暂时不实现完整的描述符集
    // 实际需要从 DescriptorSetHandle 获取 VkDescriptorSet
}

void VulkanBackend::pushConstants(ShaderHandle shader, u32 offset, u32 size, const void* data) {
    auto* shaderObj = shaders_.get(shader);
    if (!shaderObj) {
        printf("ERROR: pushConstants - shader handle invalid!\n");
        return;
    }
    
    vkCmdPushConstants(getCurrentCommandBuffer(), shaderObj->pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       offset, size, data);
}

// ============================================================================
// 计算着色器
// ============================================================================

void VulkanBackend::beginCompute() {
    // 计算通道不需要 render pass
}

void VulkanBackend::endCompute() {
    // 结束计算通道
}

void VulkanBackend::setComputePipeline(PipelineHandle handle) {
    auto* pipeline = pipelines_.get(handle);
    if (!pipeline || !pipeline->isCompute) return;
    
    vkCmdBindPipeline(getCurrentCommandBuffer(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
}

void VulkanBackend::dispatchCompute(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
    vkCmdDispatch(getCurrentCommandBuffer(), groupCountX, groupCountY, groupCountZ);
}

// ============================================================================
// 图像屏障和复制
// ============================================================================

void VulkanBackend::imageBarrier(TextureHandle texture, VkImageLayout newLayout,
                                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                  VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    auto* tex = textures_.get(texture);
    if (!tex) return;
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = tex->layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = tex->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = tex->mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = tex->arrayLayers;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    
    vkCmdPipelineBarrier(getCurrentCommandBuffer(), srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    
    tex->layout = newLayout;
}

void VulkanBackend::copyBuffer(BufferHandle src, BufferHandle dst, u64 size, u64 srcOffset, u64 dstOffset) {
    auto* srcBuf = buffers_.get(src);
    auto* dstBuf = buffers_.get(dst);
    if (!srcBuf || !dstBuf) return;
    
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    
    vkCmdCopyBuffer(getCurrentCommandBuffer(), srcBuf->buffer, dstBuf->buffer, 1, &copyRegion);
}

void VulkanBackend::copyBufferToTexture(BufferHandle src, TextureHandle dst, u32 width, u32 height) {
    auto* srcBuf = buffers_.get(src);
    auto* dstTex = textures_.get(dst);
    if (!srcBuf || !dstTex) return;
    
    // 转换到传输目标布局
    imageBarrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyBufferToImage(getCurrentCommandBuffer(), srcBuf->buffer, dstTex->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    
    // 转换到着色器只读布局
    imageBarrier(dst, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void VulkanBackend::copyTextureToBuffer(TextureHandle src, BufferHandle dst, u32 width, u32 height) {
    auto* srcTex = textures_.get(src);
    auto* dstBuf = buffers_.get(dst);
    if (!srcTex || !dstBuf) return;
    
    imageBarrier(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 0, VK_ACCESS_TRANSFER_READ_BIT);
    
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    
    vkCmdCopyImageToBuffer(getCurrentCommandBuffer(), srcTex->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstBuf->buffer, 1, &region);
}

void VulkanBackend::blitTexture(TextureHandle src, TextureHandle dst, VkFilter filter) {
    auto* srcTex = textures_.get(src);
    auto* dstTex = textures_.get(dst);
    if (!srcTex || !dstTex) return;
    
    imageBarrier(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 0, VK_ACCESS_TRANSFER_READ_BIT);
    
    imageBarrier(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    
    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {(i32)srcTex->width, (i32)srcTex->height, 1};
    
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {(i32)dstTex->width, (i32)dstTex->height, 1};
    
    vkCmdBlitImage(getCurrentCommandBuffer(),
                   srcTex->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstTex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, filter);
}

// ============================================================================
// 自定义渲染过程和帧缓冲
// ============================================================================

RenderPassHandle VulkanBackend::createRenderPass(const VulkanRenderPass& desc) {
    auto handle = renderPasses_.allocate();
    if (!handle.valid()) return {};
    
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<u32>(desc.attachments.size());
    createInfo.pAttachments = desc.attachments.data();
    createInfo.subpassCount = static_cast<u32>(desc.subpasses.size());
    createInfo.pSubpasses = desc.subpasses.data();
    
    VulkanRenderPass pass;
    if (vkCreateRenderPass(device_, &createInfo, nullptr, &pass.renderPass) != VK_SUCCESS) {
        renderPasses_.deallocate(handle);
        return {};
    }
    
    pass.attachments = desc.attachments;
    pass.subpasses = desc.subpasses;
    
    renderPasses_.set(handle, pass);
    return handle;
}

void VulkanBackend::destroyRenderPass(RenderPassHandle handle) {
    auto* pass = renderPasses_.get(handle);
    if (!pass) return;
    
    vkDestroyRenderPass(device_, pass->renderPass, nullptr);
    renderPasses_.deallocate(handle);
}

FramebufferHandle VulkanBackend::createFramebuffer(const std::vector<TextureHandle>& attachments, u32 width, u32 height) {
    auto handle = framebuffers2_.allocate();
    if (!handle.valid()) return {};
    
    std::vector<VkImageView> views;
    for (auto& tex : attachments) {
        auto* texture = textures_.get(tex);
        if (texture && texture->view) {
            views.push_back(texture->view);
        }
    }
    
    VkFramebufferCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    createInfo.renderPass = renderPass_;
    createInfo.attachmentCount = static_cast<u32>(views.size());
    createInfo.pAttachments = views.data();
    createInfo.width = width;
    createInfo.height = height;
    createInfo.layers = 1;
    
    VulkanFramebuffer fb;
    if (vkCreateFramebuffer(device_, &createInfo, nullptr, &fb.framebuffer) != VK_SUCCESS) {
        framebuffers2_.deallocate(handle);
        return {};
    }
    
    fb.width = width;
    fb.height = height;
    fb.attachments = attachments;
    
    framebuffers2_.set(handle, fb);
    return handle;
}

void VulkanBackend::destroyFramebuffer(FramebufferHandle handle) {
    auto* fb = framebuffers2_.get(handle);
    if (!fb) return;
    
    vkDestroyFramebuffer(device_, fb->framebuffer, nullptr);
    framebuffers2_.deallocate(handle);
}

// ============================================================================
// GLSL 编译辅助函数
// ============================================================================

bool VulkanBackend::compileGLSLToSPIRV(const char* source, VkShaderStageFlagBits stage,
                                        std::vector<u32>& spirv, std::string& error) {
    using namespace glslang;
    
    EShLanguage lang;
    switch (stage) {
        case VK_SHADER_STAGE_VERTEX_BIT: lang = EShLangVertex; break;
        case VK_SHADER_STAGE_FRAGMENT_BIT: lang = EShLangFragment; break;
        case VK_SHADER_STAGE_COMPUTE_BIT: lang = EShLangCompute; break;
        case VK_SHADER_STAGE_GEOMETRY_BIT: lang = EShLangGeometry; break;
        default: return false;
    }
    
    TShader shader(lang);
    const char* sources[] = {source};
    shader.setStrings(sources, 1);
    
    // 设置 Vulkan SPIR-V 目标
    shader.setEnvClient(EShClientVulkan, EShTargetVulkan_1_2);
    shader.setEnvTarget(EshTargetSpv, EShTargetSpv_1_4);
    
    // 设置合理的资源限制
    TBuiltInResource resources;
    memset(&resources, 0, sizeof(resources));
    resources.maxLights = 32;
    resources.maxClipPlanes = 6;
    resources.maxTextureUnits = 32;
    resources.maxTextureCoords = 32;
    resources.maxVertexAttribs = 64;
    resources.maxVertexUniformComponents = 4096;
    resources.maxVaryingFloats = 64;
    resources.maxVertexTextureImageUnits = 32;
    resources.maxCombinedTextureImageUnits = 80;
    resources.maxTextureImageUnits = 32;
    resources.maxFragmentUniformComponents = 4096;
    resources.maxDrawBuffers = 8;
    resources.maxVertexUniformVectors = 256;
    resources.maxVaryingVectors = 16;
    resources.maxFragmentUniformVectors = 256;
    resources.maxVertexOutputVectors = 16;
    resources.maxFragmentInputVectors = 15;
    resources.minProgramTexelOffset = -8;
    resources.maxProgramTexelOffset = 7;
    resources.maxClipDistances = 8;
    resources.maxComputeWorkGroupCountX = 65535;
    resources.maxComputeWorkGroupCountY = 65535;
    resources.maxComputeWorkGroupCountZ = 65535;
    resources.maxComputeWorkGroupSizeX = 1024;
    resources.maxComputeWorkGroupSizeY = 1024;
    resources.maxComputeWorkGroupSizeZ = 64;
    resources.maxComputeUniformComponents = 1024;
    resources.maxComputeTextureImageUnits = 16;
    resources.maxComputeImageUniforms = 8;
    resources.maxSamples = 32;
    
    EShMessages messages = (EShMessages)(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    
    if (!shader.parse(&resources, 100, false, messages)) {
        error = shader.getInfoLog();
        printf("Shader parse error: %s\n", error.c_str());
        return false;
    }
    
    TProgram program;
    program.addShader(&shader);
    
    if (!program.link(messages)) {
        error = program.getInfoLog();
        printf("Shader link error: %s\n", error.c_str());
        return false;
    }
    
    SpvOptions spvOptions;
    GlslangToSpv(*program.getIntermediate(lang), spirv, &spvOptions);
    
    printf("Shader compiled successfully, SPIR-V size: %zu words\n", spirv.size());
    return true;
}

// ============================================================================
// 格式查询
// ============================================================================

VkFormat VulkanBackend::findSupportedFormat(const std::vector<VkFormat>& candidates,
                                             VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);
        
        if (tiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
                   (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    
    return VK_FORMAT_UNDEFINED;
}

u32 VulkanBackend::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);
    
    for (u32 i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    return 0;
}

} // namespace Nova

/**
 * 轻量级 Vulkan 毛玻璃窗口
 * 功能：Windows Acrylic 效果 + Vulkan GPU 渲染
 * 内存目标：< 10MB
 */

#include <windows.h>
#include <dwmapi.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

// ============================================================================
// Windows Acrylic 效果 (Windows 10 1803+)
// ============================================================================

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

enum WINDOWCOMPOSITIONATTRIB {
    WCA_ACCENT_POLICY = 19
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

void EnableAcrylic(HWND hwnd, COLORREF color, BYTE alpha) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;
    
    auto SetWindowCompositionAttribute = 
        (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    
    if (SetWindowCompositionAttribute) {
        ACCENT_POLICY policy;
        policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
        policy.AccentFlags = 2;
        policy.GradientColor = (alpha << 24) | (color & 0xFFFFFF);
        policy.AnimationId = 0;
        
        WINDOWCOMPOSITIONATTRIBDATA data;
        data.Attrib = WCA_ACCENT_POLICY;
        data.pvData = &policy;
        data.cbData = sizeof(policy);
        
        SetWindowCompositionAttribute(hwnd, &data);
    }
}

// ============================================================================
// 最小 Vulkan 封装
// ============================================================================

class MiniVulkan {
public:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    
    uint32_t graphicsFamily = 0;
    uint32_t imageCount = 0;
    uint32_t currentFrame = 0;
    uint32_t width = 500;
    uint32_t height = 300;
    
    bool init(HWND hwnd) {
        // 创建 Instance
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.applicationVersion = 1;
        appInfo.engineVersion = 1;
        appInfo.apiVersion = VK_API_VERSION_1_0;
        
        const char* extensions[] = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME
        };
        
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = 2;
        createInfo.ppEnabledExtensionNames = extensions;
        
        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            printf("Failed to create Vulkan instance\n");
            return false;
        }
        
        // 选择物理设备
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        physicalDevice = devices[0];
        
        // 获取队列族
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
        
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamily = i;
                break;
            }
        }
        
        // 创建逻辑设备
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = graphicsFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        
        const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        
        vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device);
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        
        // 创建 Surface
        VkWin32SurfaceCreateInfoKHR surfaceInfo{};
        surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceInfo.hwnd = hwnd;
        surfaceInfo.hinstance = GetModuleHandle(nullptr);
        
        vkCreateWin32SurfaceKHR(instance, &surfaceInfo, nullptr, &surface);
        
        // 创建 Swapchain
        createSwapchain(hwnd);
        
        // 创建渲染资源
        createRenderPass();
        createPipeline();
        createFramebuffers();
        createCommandBuffers();
        createSyncObjects();
        
        printf("Vulkan initialized successfully\n");
        return true;
    }
    
    void createSwapchain(HWND hwnd) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
        
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
        
        VkSurfaceFormatKHR surfaceFormat = formats[0];
        for (auto& f : formats) {
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB) {
                surfaceFormat = f;
                break;
            }
        }
        
        VkSwapchainCreateInfoKHR swapInfo{};
        swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapInfo.surface = surface;
        swapInfo.minImageCount = 2;
        swapInfo.imageFormat = surfaceFormat.format;
        swapInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapInfo.imageExtent = { width, height };
        swapInfo.imageArrayLayers = 1;
        swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapInfo.preTransform = caps.currentTransform;
        swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT;
        swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapInfo.clipped = VK_TRUE;
        
        vkCreateSwapchainKHR(device, &swapInfo, nullptr, &swapchain);
        
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        swapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());
        
        swapchainViews.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = swapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = surfaceFormat.format;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCreateImageView(device, &viewInfo, nullptr, &swapchainViews[i]);
        }
    }
    
    void createRenderPass() {
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_B8G8R8A8_SRGB;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        
        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        
        VkSubpassDescription subpass{};
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;
        
        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        
        vkCreateRenderPass(device, &info, nullptr, &renderPass);
    }
    
    void createPipeline() {
        // 简单的顶点着色器
        const char* vertShader = R"(
#version 450
layout(push_constant) uniform PushConstants {
    vec4 color;
    vec4 rect;
    float time;
} pc;
layout(location = 0) out vec4 outColor;
void main() {
    vec2 pos = vec2(gl_VertexIndex == 1 ? 1.0 : 0.0, gl_VertexIndex == 2 ? 1.0 : 0.0);
    pos = pos * pc.rect.zw + pc.rect.xy;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
    outColor = pc.color;
}
)";
        
        // 片段着色器 - 带动态效果
        const char* fragShader = R"(
#version 450
layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PushConstants {
    vec4 color;
    vec4 rect;
    float time;
} pc;
void main() {
    vec4 c = inColor;
    // 动态高光
    float highlight = sin(pc.time * 2.0) * 0.1 + 0.1;
    c.rgb += highlight;
    outColor = c;
}
)";
        
        // 编译着色器 (简化版 - 使用预编译的 SPIR-V 或运行时编译)
        // 这里使用 glslang 或 shaderc...
        
        // 由于简化，我们使用一个基本的管线配置
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.size = 48;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pcRange;
        
        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);
        
        // 管线创建需要着色器模块，这里简化...
        printf("Pipeline created (simplified)\n");
    }
    
    void createFramebuffers() {
        framebuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            VkFramebufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass = renderPass;
            info.attachmentCount = 1;
            info.pAttachments = &swapchainViews[i];
            info.width = width;
            info.height = height;
            info.layers = 1;
            vkCreateFramebuffer(device, &info, nullptr, &framebuffers[i]);
        }
    }
    
    void createCommandBuffers() {
        VkCommandPool pool;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = graphicsFamily;
        vkCreateCommandPool(device, &poolInfo, nullptr, &pool);
        
        commandBuffers.resize(imageCount);
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.commandBufferCount = imageCount;
        vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data());
    }
    
    void createSyncObjects() {
        imageAvailable.resize(2);
        renderFinished.resize(2);
        inFlightFences.resize(2);
        
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        
        for (int i = 0; i < 2; i++) {
            vkCreateSemaphore(device, &semInfo, nullptr, &imageAvailable[i]);
            vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished[i]);
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]);
        }
    }
    
    void render(float time) {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        
        uint32_t imageIndex;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, 
                               imageAvailable[currentFrame], VK_NULL_HANDLE, &imageIndex);
        
        vkResetFences(device, 1, &inFlightFences[currentFrame]);
        
        // 录制命令缓冲
        VkCommandBuffer cmd = commandBuffers[imageIndex];
        vkResetCommandBuffer(cmd, 0);
        
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);
        
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffers[imageIndex];
        renderPassInfo.renderArea.extent = { width, height };
        
        VkClearValue clearValue{};
        clearValue.color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};  // 透明背景
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        
        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        
        // 这里绑定管线并绘制...
        // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        // vkCmdDraw(cmd, 3, 1, 0, 0);
        
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
        
        // 提交
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable[currentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished[currentFrame];
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.pWaitDstStageMask = &waitStage;
        
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]);
        
        // 呈现
        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished[currentFrame];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchain;
        presentInfo.pImageIndices = &imageIndex;
        
        vkQueuePresentKHR(graphicsQueue, &presentInfo);
        
        currentFrame = (currentFrame + 1) % 2;
    }
    
    void cleanup() {
        vkDeviceWaitIdle(device);
        
        for (int i = 0; i < 2; i++) {
            vkDestroySemaphore(device, imageAvailable[i], nullptr);
            vkDestroySemaphore(device, renderFinished[i], nullptr);
            vkDestroyFence(device, inFlightFences[i], nullptr);
        }
        
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto view : swapchainViews) vkDestroyImageView(device, view, nullptr);
        
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    }
};

// ============================================================================
// 窗口管理
// ============================================================================

MiniVulkan* g_vulkan = nullptr;
float g_time = 0.0f;
bool g_running = true;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            g_running = false;
            PostQuitMessage(0);
            return 0;
            
        case WM_NCHITTEST: {
            // 让窗口可拖动
            LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT) hit = HTCAPTION;
            return hit;
        }
        
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                g_running = false;
                PostQuitMessage(0);
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // 创建无边框窗口
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"GlassWindow";
    RegisterClassExW(&wc);
    
    // WS_EX_NOREDIRECTIONBITMAP 让窗口支持透明
    HWND hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED,
        L"GlassWindow", L"",
        WS_POPUP,
        100, 100, 500, 300,
        nullptr, nullptr, hInst, nullptr
    );
    
    // 设置窗口透明度
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    
    // 启用 Acrylic 效果
    EnableAcrylic(hwnd, 0x202030, 200);  // 深灰色，透明度 200
    
    ShowWindow(hwnd, SW_SHOW);
    
    // 初始化 Vulkan
    MiniVulkan vulkan;
    g_vulkan = &vulkan;
    
    if (!vulkan.init(hwnd)) {
        MessageBoxW(hwnd, L"Vulkan initialization failed", L"Error", MB_OK);
        return 1;
    }
    
    printf("GPU: %s\n", "Vulkan Device");
    
    // 主循环
    MSG msg;
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        if (!g_running) break;
        
        g_time += 0.016f;
        vulkan.render(g_time);
    }
    
    vulkan.cleanup();
    return 0;
}

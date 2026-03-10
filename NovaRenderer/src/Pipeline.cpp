/**
 * Nova Renderer - Pipeline System Implementation
 */

#include "Nova/Pipeline.h"
#include "Nova/Shader.h"
#include <fstream>
#include <unordered_map>

namespace Nova {

// ============================================================================
// GraphicsPipelineDesc 快捷方法
// ============================================================================

GraphicsPipelineDesc& GraphicsPipelineDesc::setBlendMode(BlendMode mode) {
    blendStates.clear();
    
    AttachmentBlendState state;
    switch (mode) {
        case BlendMode::None:
            state = AttachmentBlendState::opaque();
            break;
        case BlendMode::Alpha:
            state = AttachmentBlendState::alphaBlend();
            break;
        case BlendMode::Additive:
            state = AttachmentBlendState::additive();
            break;
        case BlendMode::Multiply:
            state = AttachmentBlendState::multiply();
            break;
        case BlendMode::Premultiplied:
            state = AttachmentBlendState::premultiplied();
            break;
        default:
            break;
    }
    blendStates.push_back(state);
    return *this;
}

GraphicsPipelineDesc& GraphicsPipelineDesc::setDepthTest(bool enable, bool write) {
    depthStencil.depthTestEnable = enable;
    depthStencil.depthWriteEnable = write;
    return *this;
}

GraphicsPipelineDesc& GraphicsPipelineDesc::setCullMode(CullMode mode) {
    rasterization.cullMode = mode;
    return *this;
}

GraphicsPipelineDesc& GraphicsPipelineDesc::setWireframe(bool enable) {
    rasterization.polygonMode = enable ? PolygonMode::Line : PolygonMode::Fill;
    return *this;
}

// ============================================================================
// PipelineCache Implementation
// ============================================================================

PipelineCache::PipelineCache() = default;

PipelineCache::~PipelineCache() {
    shutdown();
}

bool PipelineCache::initialize(VkDevice device, const char* cachePath) {
    device_ = device;
    
    VkPipelineCacheCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    
    // 如果有缓存文件，加载它
    std::vector<char> cacheData;
    if (cachePath) {
        std::ifstream file(cachePath, std::ios::binary | std::ios::ate);
        if (file.is_open()) {
            size_t size = file.tellg();
            file.seekg(0);
            cacheData.resize(size);
            file.read(cacheData.data(), size);
            
            createInfo.initialDataSize = size;
            createInfo.pInitialData = cacheData.data();
        }
    }
    
    return vkCreatePipelineCache(device_, &createInfo, nullptr, &cache_) == VK_SUCCESS;
}

void PipelineCache::shutdown() {
    if (device_ && cache_) {
        vkDestroyPipelineCache(device_, cache_, nullptr);
        cache_ = VK_NULL_HANDLE;
    }
}

bool PipelineCache::saveToFile(const char* path) {
    if (!device_ || !cache_) return false;
    
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, cache_, &size, nullptr) != VK_SUCCESS) {
        return false;
    }
    
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device_, cache_, &size, data.data()) != VK_SUCCESS) {
        return false;
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    
    file.write(data.data(), size);
    return true;
}

bool PipelineCache::loadFromFile(const char* path) {
    // 重新初始化并加载缓存
    shutdown();
    return initialize(device_, path);
}

// ============================================================================
// 管线数据存储
// ============================================================================

struct PipelineData {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorLayouts;
    GraphicsPipelineDesc desc;
};

static std::unordered_map<u32, PipelineData> s_pipelineData;
static u32 s_nextPipelineId = 1;

// 全局实例
PipelineManager* PipelineManager::s_instance = nullptr;

// ============================================================================
// PipelineManager Implementation
// ============================================================================

PipelineManager::PipelineManager() {
    s_instance = this;
}

PipelineManager::~PipelineManager() {
    if (s_instance == this) {
        s_instance = nullptr;
    }
    shutdown();
}

bool PipelineManager::initialize(VkDevice device, VkRenderPass renderPass) {
    device_ = device;
    renderPass_ = renderPass;
    
    if (!cache_.initialize(device)) {
        return false;
    }
    
    return true;
}

void PipelineManager::shutdown() {
    // 销毁所有管线
    for (auto& [id, data] : s_pipelineData) {
        if (data.pipeline) {
            vkDestroyPipeline(device_, data.pipeline, nullptr);
        }
        if (data.layout) {
            vkDestroyPipelineLayout(device_, data.layout, nullptr);
        }
        for (auto layout : data.descriptorLayouts) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        }
    }
    s_pipelineData.clear();
    namedPipelines_.clear();
    
    cache_.shutdown();
    device_ = VK_NULL_HANDLE;
}

PipelineHandle PipelineManager::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    // 获取着色器数据
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    
    // 从 ShaderManager 获取着色器模块
    if (desc.shader.valid()) {
        // 简化：假设着色器已经编译好
        // 实际应该从 ShaderManager 获取 VkShaderModule
    }
    
    // 着色器阶段
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    
    // TODO: 从 ShaderHandle 获取实际的着色器模块
    // 这里简化实现
    
    // 顶点输入状态
    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = desc.vertexLayout.stride;
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::vector<VkVertexInputAttributeDescription> attributeDescs;
    for (const auto& attr : desc.vertexLayout.inputs) {
        VkVertexInputAttributeDescription attrDesc{};
        attrDesc.location = attr.location;
        attrDesc.binding = 0;
        attrDesc.format = getVkFormat(attr.format);
        attrDesc.offset = attr.offset;
        attributeDescs.push_back(attrDesc);
    }
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescs.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescs.data();
    
    // 输入装配状态
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = getVkTopology(desc.topology);
    inputAssembly.primitiveRestartEnable = desc.primitiveRestartEnable;
    
    // 光栅化状态
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = desc.rasterization.depthClampEnable;
    rasterizer.rasterizerDiscardEnable = desc.rasterization.rasterizerDiscardEnable;
    rasterizer.polygonMode = static_cast<VkPolygonMode>(desc.rasterization.polygonMode);
    rasterizer.cullMode = getVkCullMode(desc.rasterization.cullMode);
    rasterizer.frontFace = static_cast<VkFrontFace>(desc.rasterization.frontFace);
    rasterizer.depthBiasEnable = desc.rasterization.depthBiasEnable;
    rasterizer.depthBiasConstantFactor = desc.rasterization.depthBiasConstantFactor;
    rasterizer.depthBiasClamp = desc.rasterization.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.rasterization.depthBiasSlopeFactor;
    rasterizer.lineWidth = desc.rasterization.lineWidth;
    
    // 多重采样状态
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.multisample.rasterizationSamples);
    multisampling.sampleShadingEnable = desc.multisample.sampleShadingEnable;
    multisampling.minSampleShading = desc.multisample.minSampleShading;
    VkSampleMask sampleMask = desc.multisample.sampleMask;
    multisampling.pSampleMask = &sampleMask;
    multisampling.alphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;
    multisampling.alphaToOneEnable = desc.multisample.alphaToOneEnable;
    
    // 深度模板状态
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable;
    depthStencil.depthCompareOp = getVkCompareOp(desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable;
    depthStencil.front.failOp = getVkStencilOp(desc.depthStencil.frontFailOp);
    depthStencil.front.passOp = getVkStencilOp(desc.depthStencil.frontPassOp);
    depthStencil.front.depthFailOp = getVkStencilOp(desc.depthStencil.frontDepthFailOp);
    depthStencil.front.compareOp = getVkCompareOp(desc.depthStencil.frontCompareOp);
    depthStencil.front.compareMask = desc.depthStencil.frontCompareMask;
    depthStencil.front.writeMask = desc.depthStencil.frontWriteMask;
    depthStencil.front.reference = desc.depthStencil.frontReference;
    depthStencil.back.failOp = getVkStencilOp(desc.depthStencil.backFailOp);
    depthStencil.back.passOp = getVkStencilOp(desc.depthStencil.backPassOp);
    depthStencil.back.depthFailOp = getVkStencilOp(desc.depthStencil.backDepthFailOp);
    depthStencil.back.compareOp = getVkCompareOp(desc.depthStencil.backCompareOp);
    depthStencil.back.compareMask = desc.depthStencil.backCompareMask;
    depthStencil.back.writeMask = desc.depthStencil.backWriteMask;
    depthStencil.back.reference = desc.depthStencil.backReference;
    depthStencil.minDepthBounds = desc.depthStencil.minDepthBounds;
    depthStencil.maxDepthBounds = desc.depthStencil.maxDepthBounds;
    
    // 混合状态
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    for (const auto& blendState : desc.blendStates) {
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = blendState.blendEnable;
        attachment.srcColorBlendFactor = getVkBlendFactor(blendState.srcColorBlendFactor);
        attachment.dstColorBlendFactor = getVkBlendFactor(blendState.dstColorBlendFactor);
        attachment.colorBlendOp = getVkBlendOp(blendState.colorBlendOp);
        attachment.srcAlphaBlendFactor = getVkBlendFactor(blendState.srcAlphaBlendFactor);
        attachment.dstAlphaBlendFactor = getVkBlendFactor(blendState.dstAlphaBlendFactor);
        attachment.alphaBlendOp = getVkBlendOp(blendState.alphaBlendOp);
        attachment.colorWriteMask = blendState.colorWriteMask;
        colorBlendAttachments.push_back(attachment);
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<u32>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    
    // 动态状态
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<u32>(desc.dynamicStates.size());
    dynamicState.pDynamicStates = desc.dynamicStates.data();
    
    // 管线布局 (简化版)
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    VkPipelineLayout layout;
    if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        return {};
    }
    
    // 创建管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<u32>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = nullptr;  // 动态设置
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = desc.subpass;
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(device_, cache_.getHandle(), 1, &pipelineInfo, nullptr, &pipeline);
    
    // 清理着色器模块
    if (vertModule) vkDestroyShaderModule(device_, vertModule, nullptr);
    if (fragModule) vkDestroyShaderModule(device_, fragModule, nullptr);
    
    if (result != VK_SUCCESS) {
        vkDestroyPipelineLayout(device_, layout, nullptr);
        return {};
    }
    
    // 存储管线数据
    u32 id = s_nextPipelineId++;
    PipelineData data;
    data.pipeline = pipeline;
    data.layout = layout;
    data.desc = desc;
    s_pipelineData[id] = std::move(data);
    
    if (!desc.name.empty()) {
        namedPipelines_[desc.name] = PipelineHandle{id, 0};
    }
    
    return PipelineHandle{id, 0};
}

PipelineHandle PipelineManager::createComputePipeline(const ComputePipelineDesc& desc) {
    // 简化实现
    // TODO: 实现计算管线创建
    return {};
}

PipelineHandle PipelineManager::getPipeline(const std::string& name) {
    auto it = namedPipelines_.find(name);
    if (it != namedPipelines_.end()) {
        return it->second;
    }
    return {};
}

void PipelineManager::destroyPipeline(PipelineHandle handle) {
    auto it = s_pipelineData.find(handle.index);
    if (it == s_pipelineData.end()) return;
    
    if (it->second.pipeline) {
        vkDestroyPipeline(device_, it->second.pipeline, nullptr);
    }
    if (it->second.layout) {
        vkDestroyPipelineLayout(device_, it->second.layout, nullptr);
    }
    for (auto layout : it->second.descriptorLayouts) {
        vkDestroyDescriptorSetLayout(device_, layout, nullptr);
    }
    
    s_pipelineData.erase(it);
}

VkPipeline PipelineManager::getVkPipeline(PipelineHandle handle) const {
    auto it = s_pipelineData.find(handle.index);
    return it != s_pipelineData.end() ? it->second.pipeline : VK_NULL_HANDLE;
}

VkPipelineLayout PipelineManager::getVkPipelineLayout(PipelineHandle handle) const {
    auto it = s_pipelineData.find(handle.index);
    return it != s_pipelineData.end() ? it->second.layout : VK_NULL_HANDLE;
}

VkPipeline PipelineManager::sGetVkPipeline(PipelineHandle handle) {
    auto it = s_pipelineData.find(handle.index);
    return it != s_pipelineData.end() ? it->second.pipeline : VK_NULL_HANDLE;
}

VkPipelineLayout PipelineManager::sGetVkPipelineLayout(PipelineHandle handle) {
    auto it = s_pipelineData.find(handle.index);
    return it != s_pipelineData.end() ? it->second.layout : VK_NULL_HANDLE;
}

} // namespace Nova
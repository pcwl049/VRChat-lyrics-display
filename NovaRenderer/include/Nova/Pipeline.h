/**
 * Nova Renderer - Pipeline System
 * 管线系统
 */

#pragma once

#include "Types.h"
#include "Shader.h"
#include <vulkan/vulkan.h>
#include <vector>

namespace Nova {

// 混合模式
enum class BlendMode : u32 {
    None = 0,          // 无混合
    Alpha = 1,         // Alpha 混合
    Additive = 2,      // 加法混合
    Multiply = 3,      // 乘法混合
    Screen = 4,        // 屏幕混合
    Premultiplied = 5, // 预乘 Alpha
    Custom = 6         // 自定义
};

// 多边形模式
enum class PolygonMode : u32 {
    Fill = 0,
    Line = 1,
    Point = 2
};

// 拓扑模式
enum class Topology : u32 {
    PointList = 0,
    LineList = 1,
    LineStrip = 2,
    TriangleList = 3,
    TriangleStrip = 4,
    TriangleFan = 5
};

inline VkPrimitiveTopology getVkTopology(Topology topology) {
    switch (topology) {
        case Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case Topology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case Topology::TriangleFan: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

// 面剔除模式
enum class CullMode : u32 {
    None = 0,
    Front = 1,
    Back = 2,
    FrontAndBack = 3
};

inline VkCullModeFlags getVkCullMode(CullMode mode) {
    switch (mode) {
        case CullMode::None: return VK_CULL_MODE_NONE;
        case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
        case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
        case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
        default: return VK_CULL_MODE_NONE;
    }
}

// 正面方向
enum class FrontFace : u32 {
    CounterClockwise = 0,
    Clockwise = 1
};

// 比较操作
enum class CompareOp : u32 {
    Never = 0,
    Less = 1,
    Equal = 2,
    LessOrEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterOrEqual = 6,
    Always = 7
};

inline VkCompareOp getVkCompareOp(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_ALWAYS;
    }
}

// 模板操作
enum class StencilOp : u32 {
    Keep = 0,
    Zero = 1,
    Replace = 2,
    IncrementClamp = 3,
    DecrementClamp = 4,
    Invert = 5,
    IncrementWrap = 6,
    DecrementWrap = 7
};

inline VkStencilOp getVkStencilOp(StencilOp op) {
    switch (op) {
        case StencilOp::Keep: return VK_STENCIL_OP_KEEP;
        case StencilOp::Zero: return VK_STENCIL_OP_ZERO;
        case StencilOp::Replace: return VK_STENCIL_OP_REPLACE;
        case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case StencilOp::Invert: return VK_STENCIL_OP_INVERT;
        case StencilOp::IncrementWrap: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case StencilOp::DecrementWrap: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default: return VK_STENCIL_OP_KEEP;
    }
}

// 混合因子
enum class BlendFactor : u32 {
    Zero = 0,
    One = 1,
    SrcColor = 2,
    OneMinusSrcColor = 3,
    DstColor = 4,
    OneMinusDstColor = 5,
    SrcAlpha = 6,
    OneMinusSrcAlpha = 7,
    DstAlpha = 8,
    OneMinusDstAlpha = 9,
    ConstantColor = 10,
    OneMinusConstantColor = 11,
    ConstantAlpha = 12,
    OneMinusConstantAlpha = 13,
    SrcAlphaSaturate = 14
};

inline VkBlendFactor getVkBlendFactor(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::ConstantColor: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::OneMinusConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::ConstantAlpha: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::OneMinusConstantAlpha: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        default: return VK_BLEND_FACTOR_ZERO;
    }
}

// 混合操作
enum class BlendOp : u32 {
    Add = 0,
    Subtract = 1,
    ReverseSubtract = 2,
    Min = 3,
    Max = 4
};

inline VkBlendOp getVkBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return VK_BLEND_OP_ADD;
        case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min: return VK_BLEND_OP_MIN;
        case BlendOp::Max: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

// 附件混合状态
struct AttachmentBlendState {
    bool blendEnable = false;
    BlendFactor srcColorBlendFactor = BlendFactor::SrcAlpha;
    BlendFactor dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
    BlendOp alphaBlendOp = BlendOp::Add;
    u32 colorWriteMask = 0xF;  // RGBA
    
    static AttachmentBlendState opaque() {
        return {};
    }
    
    static AttachmentBlendState alphaBlend() {
        AttachmentBlendState state;
        state.blendEnable = true;
        return state;
    }
    
    static AttachmentBlendState additive() {
        AttachmentBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = BlendFactor::One;
        state.dstColorBlendFactor = BlendFactor::One;
        state.srcAlphaBlendFactor = BlendFactor::One;
        state.dstAlphaBlendFactor = BlendFactor::One;
        return state;
    }
    
    static AttachmentBlendState multiply() {
        AttachmentBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = BlendFactor::DstColor;
        state.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
        return state;
    }
    
    static AttachmentBlendState premultiplied() {
        AttachmentBlendState state;
        state.blendEnable = true;
        state.srcColorBlendFactor = BlendFactor::One;
        state.dstColorBlendFactor = BlendFactor::OneMinusSrcAlpha;
        state.srcAlphaBlendFactor = BlendFactor::One;
        state.dstAlphaBlendFactor = BlendFactor::OneMinusSrcAlpha;
        return state;
    }
};

// 深度模板状态
struct DepthStencilState {
    bool depthTestEnable = false;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool depthBoundsTestEnable = false;
    bool stencilTestEnable = false;
    
    // 前面模板
    StencilOp frontFailOp = StencilOp::Keep;
    StencilOp frontPassOp = StencilOp::Keep;
    StencilOp frontDepthFailOp = StencilOp::Keep;
    CompareOp frontCompareOp = CompareOp::Always;
    u32 frontCompareMask = 0xFF;
    u32 frontWriteMask = 0xFF;
    u32 frontReference = 0;
    
    // 背面模板
    StencilOp backFailOp = StencilOp::Keep;
    StencilOp backPassOp = StencilOp::Keep;
    StencilOp backDepthFailOp = StencilOp::Keep;
    CompareOp backCompareOp = CompareOp::Always;
    u32 backCompareMask = 0xFF;
    u32 backWriteMask = 0xFF;
    u32 backReference = 0;
    
    f32 minDepthBounds = 0.0f;
    f32 maxDepthBounds = 1.0f;
    
    static DepthStencilState none() {
        return {};
    }
    
    static DepthStencilState depthRead() {
        DepthStencilState state;
        state.depthTestEnable = true;
        state.depthWriteEnable = false;
        return state;
    }
    
    static DepthStencilState depthReadWrite() {
        DepthStencilState state;
        state.depthTestEnable = true;
        return state;
    }
};

// 光栅化状态
struct RasterizationState {
    bool depthClampEnable = false;
    bool rasterizerDiscardEnable = false;
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool depthBiasEnable = false;
    f32 depthBiasConstantFactor = 0.0f;
    f32 depthBiasClamp = 0.0f;
    f32 depthBiasSlopeFactor = 0.0f;
    f32 lineWidth = 1.0f;
    
    static RasterizationState defaults() {
        return {};
    }
    
    static RasterizationState wireframe() {
        RasterizationState state;
        state.polygonMode = PolygonMode::Line;
        return state;
    }
    
    static RasterizationState noCull() {
        RasterizationState state;
        state.cullMode = CullMode::None;
        return state;
    }
};

// 多重采样状态
struct MultisampleState {
    u32 rasterizationSamples = 1;
    bool sampleShadingEnable = false;
    f32 minSampleShading = 1.0f;
    u32 sampleMask = 0xFFFFFFFF;
    bool alphaToCoverageEnable = false;
    bool alphaToOneEnable = false;
    
    static MultisampleState defaults() {
        return {};
    }
    
    static MultisampleState msaa4x() {
        MultisampleState state;
        state.rasterizationSamples = 4;
        return state;
    }
};

// 图形管线描述
struct GraphicsPipelineDesc {
    std::string name = "Pipeline";
    
    // 着色器
    ShaderHandle shader;
    
    // 顶点布局
    VertexLayout vertexLayout;
    
    // 渲染状态
    Topology topology = Topology::TriangleList;
    bool primitiveRestartEnable = false;
    
    RasterizationState rasterization;
    MultisampleState multisample;
    DepthStencilState depthStencil;
    
    // 混合状态 (每个颜色附件)
    std::vector<AttachmentBlendState> blendStates;
    
    // 动态状态
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    // 渲染过程
    VkRenderPass renderPass = VK_NULL_HANDLE;
    u32 subpass = 0;
    
    // 快捷设置
    GraphicsPipelineDesc& setShader(ShaderHandle sh) { shader = sh; return *this; }
    GraphicsPipelineDesc& setTopology(Topology t) { topology = t; return *this; }
    GraphicsPipelineDesc& setBlendMode(BlendMode mode);
    GraphicsPipelineDesc& setDepthTest(bool enable, bool write = true);
    GraphicsPipelineDesc& setCullMode(CullMode mode);
    GraphicsPipelineDesc& setWireframe(bool enable);
};

// 计算管线描述
struct ComputePipelineDesc {
    std::string name = "ComputePipeline";
    ShaderHandle shader;
};

// 管线缓存
class PipelineCache {
public:
    PipelineCache();
    ~PipelineCache();
    
    // 创建管线缓存
    bool initialize(VkDevice device, const char* cachePath = nullptr);
    void shutdown();
    
    // 保存/加载
    bool saveToFile(const char* path);
    bool loadFromFile(const char* path);
    
    // 获取原生对象
    VkPipelineCache getHandle() const { return cache_; }
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
};

// 管线管理器
class PipelineManager {
public:
    PipelineManager();
    ~PipelineManager();
    
    bool initialize(VkDevice device, VkRenderPass renderPass);
    void shutdown();
    
    // 创建管线
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc);
    PipelineHandle createComputePipeline(const ComputePipelineDesc& desc);
    
    // 获取管线
    PipelineHandle getPipeline(const std::string& name);
    
    // 销毁管线
    void destroyPipeline(PipelineHandle handle);
    
    // 获取原生对象 (实例方法)
    VkPipeline getVkPipeline(PipelineHandle handle) const;
    VkPipelineLayout getVkPipelineLayout(PipelineHandle handle) const;
    
    // 静态快捷方法 (使用全局实例)
    static VkPipeline sGetVkPipeline(PipelineHandle handle);
    static VkPipelineLayout sGetVkPipelineLayout(PipelineHandle handle);
    
    // 全局实例
    static PipelineManager* get() { return s_instance; }
    
private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    
    PipelineCache cache_;
    std::unordered_map<std::string, PipelineHandle> namedPipelines_;
    
    static PipelineManager* s_instance;
};

} // namespace Nova

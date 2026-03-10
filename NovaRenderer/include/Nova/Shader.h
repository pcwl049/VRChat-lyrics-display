/**
 * Nova Renderer - Shader System
 * 着色器系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace Nova {

// 着色器阶段
enum class ShaderStage : u32 {
    Vertex = 0,
    Fragment = 1,
    Compute = 2,
    Geometry = 3,
    TessControl = 4,
    TessEval = 5
};

// 顶点格式
enum class VertexFormat : u32 {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Int2,
    Int3,
    Int4,
    Byte4,
    Byte4Norm,
    UByte4,
    UByte4Norm,
    Short2,
    Short2Norm,
    Short4,
    Short4Norm
};

// 获取顶点格式大小
inline u32 getVertexFormatSize(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float: return 4;
        case VertexFormat::Float2: return 8;
        case VertexFormat::Float3: return 12;
        case VertexFormat::Float4: return 16;
        case VertexFormat::Int: return 4;
        case VertexFormat::Int2: return 8;
        case VertexFormat::Int3: return 12;
        case VertexFormat::Int4: return 16;
        case VertexFormat::Byte4: return 4;
        case VertexFormat::Byte4Norm: return 4;
        case VertexFormat::UByte4: return 4;
        case VertexFormat::UByte4Norm: return 4;
        case VertexFormat::Short2: return 4;
        case VertexFormat::Short2Norm: return 4;
        case VertexFormat::Short4: return 8;
        case VertexFormat::Short4Norm: return 8;
        default: return 0;
    }
}

// 获取 Vulkan 格式
inline VkFormat getVkFormat(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::Int: return VK_FORMAT_R32_SINT;
        case VertexFormat::Int2: return VK_FORMAT_R32G32_SINT;
        case VertexFormat::Int3: return VK_FORMAT_R32G32B32_SINT;
        case VertexFormat::Int4: return VK_FORMAT_R32G32B32A32_SINT;
        case VertexFormat::Byte4: return VK_FORMAT_R8G8B8A8_SINT;
        case VertexFormat::Byte4Norm: return VK_FORMAT_R8G8B8A8_SNORM;
        case VertexFormat::UByte4: return VK_FORMAT_R8G8B8A8_UINT;
        case VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::Short2: return VK_FORMAT_R16G16_SINT;
        case VertexFormat::Short2Norm: return VK_FORMAT_R16G16_SNORM;
        case VertexFormat::Short4: return VK_FORMAT_R16G16B16A16_SINT;
        case VertexFormat::Short4Norm: return VK_FORMAT_R16G16B16A16_SNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

// 顶点输入描述
struct VertexInput {
    u32 location;
    VertexFormat format;
    u32 offset;
    const char* semantic = "";  // 语义名称
};

// 顶点布局
struct VertexLayout {
    u32 binding = 0;
    u32 stride = 0;
    bool perInstance = false;
    std::vector<VertexInput> inputs;
    
    VertexLayout& addInput(u32 location, VertexFormat format, const char* semantic = "") {
        inputs.push_back({location, format, 0, semantic});
        stride += getVertexFormatSize(format);
        // 更新偏移
        u32 offset = 0;
        for (auto& input : inputs) {
            input.offset = offset;
            offset += getVertexFormatSize(input.format);
        }
        return *this;
    }
};

// 描述符类型
enum class DescriptorType : u32 {
    UniformBuffer = 0,
    StorageBuffer = 1,
    SampledImage = 2,
    Sampler = 3,
    CombinedImageSampler = 4,
    StorageImage = 5,
    UniformTexelBuffer = 6,
    StorageTexelBuffer = 7,
    UniformBufferDynamic = 8,
    StorageBufferDynamic = 9,
    InputAttachment = 10
};

inline VkDescriptorType getVkDescriptorType(DescriptorType type) {
    switch (type) {
        case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        case DescriptorType::UniformTexelBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        case DescriptorType::StorageTexelBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        case DescriptorType::UniformBufferDynamic: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::StorageBufferDynamic: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        case DescriptorType::InputAttachment: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
        default: return VK_DESCRIPTOR_TYPE_MAX_ENUM;
    }
}

// 描述符绑定
struct DescriptorBinding {
    u32 binding;
    DescriptorType type;
    u32 count = 1;
    ShaderStage stage;
    const char* name = "";
    
    bool operator==(const DescriptorBinding& o) const {
        return binding == o.binding && type == o.type && 
               count == o.count && stage == o.stage;
    }
};

// 描述符集布局
struct DescriptorSetLayout {
    u32 set = 0;
    std::vector<DescriptorBinding> bindings;
    
    DescriptorSetLayout& addBinding(u32 binding, DescriptorType type, ShaderStage stage, u32 count = 1, const char* name = "") {
        bindings.push_back({binding, type, count, stage, name});
        return *this;
    }
};

// 推送常量范围
struct PushConstantRange {
    ShaderStage stage;
    u32 offset;
    u32 size;
    const char* name = "";
};

// 着色器程序描述
struct ShaderProgramDesc {
    const char* name = "";
    
    // GLSL 源码
    std::string vertexSource;
    std::string fragmentSource;
    std::string computeSource;
    std::string geometrySource;
    
    // 或 SPIR-V 二进制
    std::vector<u32> vertexSpirv;
    std::vector<u32> fragmentSpirv;
    std::vector<u32> computeSpirv;
    
    // 顶点布局
    VertexLayout vertexLayout;
    
    // 描述符布局
    std::vector<DescriptorSetLayout> descriptorLayouts;
    
    // 推送常量
    std::vector<PushConstantRange> pushConstants;
    
    // 从文件加载
    static ShaderProgramDesc fromFile(const char* vertPath, const char* fragPath);
    static ShaderProgramDesc fromComputeFile(const char* compPath);
    static ShaderProgramDesc fromSource(const char* vertSrc, const char* fragSrc);
    static ShaderProgramDesc fromComputeSource(const char* compSrc);
};

// 着色器程序 (内部使用)
struct ShaderProgram {
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    VkShaderModule computeModule = VK_NULL_HANDLE;
    VkShaderModule geometryModule = VK_NULL_HANDLE;
    
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
    
    VertexLayout vertexLayout;
    std::vector<PushConstantRange> pushConstants;
    
    std::string name;
};

// 着色器编译器
class ShaderCompiler {
public:
    static bool initialize();
    static void shutdown();
    
    // 编译 GLSL 到 SPIR-V
    static bool compileGLSL(const char* source, ShaderStage stage, 
                            std::vector<u32>& outSpirv, std::string& outError);
    
    // 从文件编译
    static bool compileFromFile(const char* path, ShaderStage stage,
                                std::vector<u32>& outSpirv, std::string& outError);
    
    // 反射 SPIR-V (提取描述符信息)
    static bool reflect(const std::vector<u32>& spirv, 
                       std::vector<DescriptorSetLayout>& outLayouts,
                       std::vector<PushConstantRange>& outPushConstants);
};

// 内置着色器源码
namespace BuiltinShaders {
    
    // 基础顶点着色器
    extern const char* BASIC_VERTEX;
    
    // 纯色片段着色器
    extern const char* SOLID_COLOR_FRAGMENT;
    
    // 纹理片段着色器
    extern const char* TEXTURE_FRAGMENT;
    
    // 圆角矩形 SDF 片段着色器
    extern const char* ROUNDED_RECT_FRAGMENT;
    
    // MSDF 文本片段着色器
    extern const char* MSDF_TEXT_FRAGMENT;
    
    // 高斯模糊计算着色器
    extern const char* GAUSSIAN_BLUR_COMPUTE;
    
    // 粒子更新计算着色器
    extern const char* PARTICLE_UPDATE_COMPUTE;
    
    // 获取完整的着色器程序描述
    ShaderProgramDesc getBasicShader();
    ShaderProgramDesc getTextureShader();
    ShaderProgramDesc getRoundRectShader();
    ShaderProgramDesc getMSDFTextShader();
    ShaderProgramDesc getGaussianBlurShader();
    ShaderProgramDesc getParticleUpdateShader();
}

} // namespace Nova

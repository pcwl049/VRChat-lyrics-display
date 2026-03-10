/**
 * Nova Renderer - Shader System Implementation
 */

#include "Nova/Shader.h"
#include "Nova/VulkanBackend.h"

#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include "../third_party/spirv-reflect/spirv_reflect.h"

#include <fstream>
#include <sstream>

namespace Nova {

// ============================================================================
// 内置着色器源码
// ============================================================================

namespace BuiltinShaders {

const char* BASIC_VERTEX = R"GLSL(
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec2 uResolution;
} pc;

void main() {
    vec2 pos = pc.uTransform * vec3(inPosition, 1.0);
    vec2 ndc = (pos / pc.uResolution) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outTexCoord = inTexCoord;
    outColor = inColor;
}
)GLSL";

const char* SOLID_COLOR_FRAGMENT = R"GLSL(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor;
}
)GLSL";

const char* TEXTURE_FRAGMENT = R"GLSL(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, inTexCoord) * inColor;
}
)GLSL";

const char* ROUNDED_RECT_FRAGMENT = R"GLSL(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec4 inRect;    // xy = position, zw = size
layout(location = 3) in float inRadius;

layout(location = 0) out vec4 outColor;

// SDF for rounded rectangle
float roundedRectSDF(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    float exterior = length(max(d, 0.0)) - radius;
    float interior = min(max(d.x, d.y), 0.0);
    return exterior + interior;
}

void main() {
    vec2 halfSize = inRect.zw * 0.5;
    vec2 center = inRect.xy + halfSize;
    vec2 localPos = gl_FragCoord.xy - center;
    
    float dist = roundedRectSDF(localPos, halfSize, inRadius);
    
    // Anti-aliasing
    float aa = fwidth(dist);
    float alpha = 1.0 - smoothstep(-aa, aa, dist);
    
    outColor = vec4(inColor.rgb, inColor.a * alpha);
}
)GLSL";

const char* MSDF_TEXT_FRAGMENT = R"GLSL(
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inPxRange;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uMSDF;

// MSDF median
float median(vec3 msdf) {
    return max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
}

// Screen pixel range
float screenPxRange() {
    vec2 unitRange = vec2(inPxRange) / vec2(textureSize(uMSDF, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(inTexCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msdf = texture(uMSDF, inTexCoord).rgb;
    float sigDist = median(msdf) - 0.5;
    
    float screenRange = screenPxRange();
    float opacity = clamp(screenRange * sigDist + 0.5, 0.0, 1.0);
    
    outColor = vec4(inColor.rgb, inColor.a * opacity);
}
)GLSL";

const char* GAUSSIAN_BLUR_COMPUTE = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform Params {
    vec2 uDirection;
    float uRadius;
    uint uPass;
} params;

const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(uInput);
    
    if (texel.x >= size.x || texel.y >= size.y) return;
    
    vec4 color = imageLoad(uInput, texel) * weights[0];
    
    for (int i = 1; i < 5; i++) {
        float offset = float(i) * params.uRadius;
        ivec2 offsetCoord = ivec2(params.uDirection * offset);
        
        color += imageLoad(uInput, texel + offsetCoord) * weights[i];
        color += imageLoad(uInput, texel - offsetCoord) * weights[i];
    }
    
    imageStore(uOutput, texel, color);
}
)GLSL";

const char* PARTICLE_UPDATE_COMPUTE = R"GLSL(
#version 450

struct Particle {
    vec3 position;
    float life;
    vec3 velocity;
    float size;
    vec4 color;
    vec3 acceleration;
    float rotation;
    float angularVelocity;
    float drag;
    uint flags;
    uint padding[2];
};

layout(std430, binding = 0) buffer ParticleBufferIn {
    Particle particlesIn[];
};

layout(std430, binding = 1) buffer ParticleBufferOut {
    Particle particlesOut[];
};

layout(push_constant) uniform Params {
    float uDeltaTime;
    float uGravity;
    uint uCount;
    uint uPadding;
} params;

const uint FLAG_DEAD = 1u;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= params.uCount) return;
    
    Particle p = particlesIn[index];
    
    if ((p.flags & FLAG_DEAD) != 0u) {
        particlesOut[index] = p;
        return;
    }
    
    p.life -= params.uDeltaTime;
    if (p.life <= 0.0) {
        p.flags |= FLAG_DEAD;
        particlesOut[index] = p;
        return;
    }
    
    p.velocity += p.acceleration * params.uDeltaTime;
    p.velocity.y -= params.uGravity * params.uDeltaTime;
    p.velocity *= (1.0 - p.drag * params.uDeltaTime);
    
    p.position += p.velocity * params.uDeltaTime;
    p.rotation += p.angularVelocity * params.uDeltaTime;
    
    particlesOut[index] = p;
}
)GLSL";

ShaderProgramDesc getBasicShader() {
    ShaderProgramDesc desc;
    desc.name = "Basic";
    desc.vertexSource = BASIC_VERTEX;
    desc.fragmentSource = SOLID_COLOR_FRAGMENT;
    
    desc.vertexLayout.addInput(0, VertexFormat::Float2, "POSITION");
    desc.vertexLayout.addInput(1, VertexFormat::Float2, "TEXCOORD");
    desc.vertexLayout.addInput(2, VertexFormat::Float4, "COLOR");
    
    desc.pushConstants.push_back({ShaderStage::Vertex, 0, 64, "PushConstants"});
    
    return desc;
}

ShaderProgramDesc getTextureShader() {
    ShaderProgramDesc desc;
    desc.name = "Texture";
    desc.vertexSource = BASIC_VERTEX;
    desc.fragmentSource = TEXTURE_FRAGMENT;
    
    desc.vertexLayout.addInput(0, VertexFormat::Float2, "POSITION");
    desc.vertexLayout.addInput(1, VertexFormat::Float2, "TEXCOORD");
    desc.vertexLayout.addInput(2, VertexFormat::Float4, "COLOR");
    
    desc.pushConstants.push_back({ShaderStage::Vertex, 0, 64, "PushConstants"});
    
    desc.descriptorLayouts.push_back(DescriptorSetLayout{}
        .addBinding(0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1, "uTexture"));
    
    return desc;
}

ShaderProgramDesc getRoundRectShader() {
    ShaderProgramDesc desc;
    desc.name = "RoundRect";
    desc.vertexSource = BASIC_VERTEX;
    desc.fragmentSource = ROUNDED_RECT_FRAGMENT;
    
    desc.vertexLayout.addInput(0, VertexFormat::Float2, "POSITION");
    desc.vertexLayout.addInput(1, VertexFormat::Float2, "TEXCOORD");
    desc.vertexLayout.addInput(2, VertexFormat::Float4, "COLOR");
    desc.vertexLayout.addInput(3, VertexFormat::Float4, "RECT");
    desc.vertexLayout.addInput(4, VertexFormat::Float, "RADIUS");
    
    return desc;
}

ShaderProgramDesc getMSDFTextShader() {
    ShaderProgramDesc desc;
    desc.name = "MSDFText";
    desc.vertexSource = BASIC_VERTEX;
    desc.fragmentSource = MSDF_TEXT_FRAGMENT;
    
    desc.vertexLayout.addInput(0, VertexFormat::Float2, "POSITION");
    desc.vertexLayout.addInput(1, VertexFormat::Float2, "TEXCOORD");
    desc.vertexLayout.addInput(2, VertexFormat::Float4, "COLOR");
    desc.vertexLayout.addInput(3, VertexFormat::Float, "PXRANGE");
    
    desc.descriptorLayouts.push_back(DescriptorSetLayout{}
        .addBinding(0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1, "uMSDF"));
    
    return desc;
}

ShaderProgramDesc getGaussianBlurShader() {
    ShaderProgramDesc desc;
    desc.name = "GaussianBlur";
    desc.computeSource = GAUSSIAN_BLUR_COMPUTE;
    
    desc.descriptorLayouts.push_back(DescriptorSetLayout{}
        .addBinding(0, DescriptorType::StorageImage, ShaderStage::Compute, 1, "uInput")
        .addBinding(1, DescriptorType::StorageImage, ShaderStage::Compute, 1, "uOutput"));
    
    desc.pushConstants.push_back({ShaderStage::Compute, 0, 16, "Params"});
    
    return desc;
}

ShaderProgramDesc getParticleUpdateShader() {
    ShaderProgramDesc desc;
    desc.name = "ParticleUpdate";
    desc.computeSource = PARTICLE_UPDATE_COMPUTE;
    
    desc.descriptorLayouts.push_back(DescriptorSetLayout{}
        .addBinding(0, DescriptorType::StorageBuffer, ShaderStage::Compute, 1, "ParticleBufferIn")
        .addBinding(1, DescriptorType::StorageBuffer, ShaderStage::Compute, 1, "ParticleBufferOut"));
    
    desc.pushConstants.push_back({ShaderStage::Compute, 0, 16, "Params"});
    
    return desc;
}

} // namespace BuiltinShaders

// ============================================================================
// ShaderCompiler Implementation
// ============================================================================

static bool s_glslangInitialized = false;
static TBuiltInResource s_defaultResources = {};

bool ShaderCompiler::initialize() {
    if (s_glslangInitialized) return true;
    
    // 初始化默认资源限制
    s_defaultResources = *GetDefaultResources();
    
    s_glslangInitialized = glslang::InitializeProcess();
    return s_glslangInitialized;
}

void ShaderCompiler::shutdown() {
    if (s_glslangInitialized) {
        glslang::FinalizeProcess();
        s_glslangInitialized = false;
    }
}

bool ShaderCompiler::compileGLSL(const char* source, ShaderStage stage,
                                  std::vector<u32>& outSpirv, std::string& outError) {
    if (!s_glslangInitialized) {
        outError = "Shader compiler not initialized";
        return false;
    }
    
    EShLanguage language;
    switch (stage) {
        case ShaderStage::Vertex: language = EShLangVertex; break;
        case ShaderStage::Fragment: language = EShLangFragment; break;
        case ShaderStage::Compute: language = EShLangCompute; break;
        case ShaderStage::Geometry: language = EShLangGeometry; break;
        case ShaderStage::TessControl: language = EShLangTessControl; break;
        case ShaderStage::TessEval: language = EShLangTessEvaluation; break;
        default:
            outError = "Unknown shader stage";
            return false;
    }
    
    glslang::TShader shader(language);
    
    // 设置源码
    const char* sources[] = {source};
    shader.setStrings(sources, 1);
    
    // 设置版本和选项
    shader.setEnvInput(glslang::EShSourceGlsl, language, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
    
    // 编译
    EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(&s_defaultResources, 450, false, messages)) {
        outError = shader.getInfoLog();
        return false;
    }
    
    // 链接
    glslang::TProgram program;
    program.addShader(&shader);
    
    if (!program.link(messages)) {
        outError = program.getInfoLog();
        return false;
    }
    
    // 生成 SPIR-V
    glslang::GlslangToSpv(*program.getIntermediate(language), outSpirv);
    
    return true;
}

bool ShaderCompiler::compileFromFile(const char* path, ShaderStage stage,
                                      std::vector<u32>& outSpirv, std::string& outError) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        outError = "Failed to open file: " + std::string(path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    return compileGLSL(source.c_str(), stage, outSpirv, outError);
}

bool ShaderCompiler::reflect(const std::vector<u32>& spirv,
                              std::vector<DescriptorSetLayout>& outLayouts,
                              std::vector<PushConstantRange>& outPushConstants) {
    SpvReflectShaderModule module;
    if (spvReflectCreateShaderModule(spirv.size() * 4, spirv.data(), &module) != SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }
    
    // 提取描述符绑定
    u32 count = 0;
    spvReflectEnumerateDescriptorBindings(&module, &count, nullptr);
    
    if (count > 0) {
        std::vector<SpvReflectDescriptorBinding*> bindings(count);
        spvReflectEnumerateDescriptorBindings(&module, &count, bindings.data());
        
        // 按 set 分组
        std::unordered_map<u32, std::vector<DescriptorBinding>> setBindings;
        
        for (auto* binding : bindings) {
            ShaderStage stage;
            switch (module.shader_stage) {
                case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: stage = ShaderStage::Vertex; break;
                case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: stage = ShaderStage::Fragment; break;
                case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT: stage = ShaderStage::Compute; break;
                default: stage = ShaderStage::Vertex; break;
            }
            
            DescriptorType type;
            switch (binding->descriptor_type) {
                case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                    type = DescriptorType::UniformBuffer; break;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                    type = DescriptorType::StorageBuffer; break;
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                    type = DescriptorType::SampledImage; break;
                case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
                    type = DescriptorType::Sampler; break;
                case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    type = DescriptorType::CombinedImageSampler; break;
                case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                    type = DescriptorType::StorageImage; break;
                default: continue;
            }
            
            setBindings[binding->set].push_back({
                binding->binding,
                type,
                binding->count,
                stage,
                binding->name
            });
        }
        
        // 转换为布局
        for (auto& [set, bindings] : setBindings) {
            DescriptorSetLayout layout;
            layout.set = set;
            layout.bindings = std::move(bindings);
            outLayouts.push_back(std::move(layout));
        }
    }
    
    // 提取推送常量
    count = 0;
    spvReflectEnumeratePushConstantBlocks(&module, &count, nullptr);
    
    if (count > 0) {
        std::vector<SpvReflectBlockVariable*> blocks(count);
        spvReflectEnumeratePushConstantBlocks(&module, &count, blocks.data());
        
        for (auto* block : blocks) {
            ShaderStage stage;
            switch (module.shader_stage) {
                case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT: stage = ShaderStage::Vertex; break;
                case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT: stage = ShaderStage::Fragment; break;
                case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT: stage = ShaderStage::Compute; break;
                default: stage = ShaderStage::Vertex; break;
            }
            
            outPushConstants.push_back({
                stage,
                block->offset,
                block->size,
                block->name
            });
        }
    }
    
    spvReflectDestroyShaderModule(&module);
    return true;
}

// ============================================================================
// ShaderProgramDesc 文件加载
// ============================================================================

ShaderProgramDesc ShaderProgramDesc::fromFile(const char* vertPath, const char* fragPath) {
    ShaderProgramDesc desc;
    
    std::ifstream vertFile(vertPath);
    std::ifstream fragFile(fragPath);
    
    if (vertFile.is_open()) {
        std::stringstream buffer;
        buffer << vertFile.rdbuf();
        desc.vertexSource = buffer.str();
    }
    
    if (fragFile.is_open()) {
        std::stringstream buffer;
        buffer << fragFile.rdbuf();
        desc.fragmentSource = buffer.str();
    }
    
    return desc;
}

ShaderProgramDesc ShaderProgramDesc::fromComputeFile(const char* compPath) {
    ShaderProgramDesc desc;
    
    std::ifstream compFile(compPath);
    if (compFile.is_open()) {
        std::stringstream buffer;
        buffer << compFile.rdbuf();
        desc.computeSource = buffer.str();
    }
    
    return desc;
}

ShaderProgramDesc ShaderProgramDesc::fromSource(const char* vertSrc, const char* fragSrc) {
    ShaderProgramDesc desc;
    desc.vertexSource = vertSrc;
    desc.fragmentSource = fragSrc;
    return desc;
}

ShaderProgramDesc ShaderProgramDesc::fromComputeSource(const char* compSrc) {
    ShaderProgramDesc desc;
    desc.computeSource = compSrc;
    return desc;
}

} // namespace Nova

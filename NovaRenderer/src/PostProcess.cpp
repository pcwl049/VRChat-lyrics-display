/**
 * Nova Renderer - Post Processing Effects Implementation
 */

#include "Nova/PostProcess.h"
#include "Nova/VulkanBackend.h"

namespace Nova {

// ============================================================================
// 高斯模糊实现
// ============================================================================

bool GaussianBlur::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    const char* blurShader = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D inputTex;
layout(binding = 1, rgba16f) uniform image2D outputTex;

layout(push_constant) uniform BlurParams {
    vec2 direction;
    vec2 resolution;
    float sigma;
    int radius;
} params;

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= params.resolution.x || pixel.y >= params.resolution.y) return;
    
    vec2 texelSize = 1.0 / params.resolution;
    vec2 uv = (vec2(pixel) + 0.5) * texelSize;
    
    vec4 color = vec4(0.0);
    float weightSum = 0.0;
    
    for (int i = -params.radius; i <= params.radius; i++) {
        float weight = gaussian(float(i), params.sigma);
        vec2 offset = params.direction * float(i) * texelSize;
        color += texture(inputTex, uv + offset) * weight;
        weightSum += weight;
    }
    
    color /= weightSum;
    imageStore(outputTex, pixel, color);
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = blurShader;
    shader_ = backend_->createShader(shaderDesc);
    
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = shader_;
    pipeline_ = backend_->createPipeline(pipelineDesc);
    
    // Create sampler
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ = backend_->createSampler(samplerDesc);
    
    return true;
}

void GaussianBlur::shutdown() {
    if (backend_) {
        if (pipeline_.valid()) backend_->destroyPipeline(pipeline_);
        if (shader_.valid()) backend_->destroyShader(shader_);
        if (tempTexture_.valid()) backend_->destroyTexture(tempTexture_);
        if (sampler_.valid()) backend_->destroySampler(sampler_);
    }
}

void GaussianBlur::resize(u32 width, u32 height) {
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    
    // 重新创建临时纹理
    if (tempTexture_.valid()) {
        backend_->destroyTexture(tempTexture_);
    }
    
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = static_cast<Format>(VK_FORMAT_R16G16B16A16_SFLOAT);
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    tempTexture_ = backend_->createTexture(desc);
}

void GaussianBlur::setParams(const GaussianBlurParams& params) {
    params_ = params;
}

void GaussianBlur::apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    if (!enabled_) return;
    
    // 确保临时纹理存在
    if (!tempTexture_.valid()) return;
    
    // �?pass 模糊
    for (u32 pass = 0; pass < params_.passes; pass++) {
        // 水平模糊
        blurHorizontal(cmd, input, tempTexture_);
        
        // 垂直模糊
        blurVertical(cmd, tempTexture_, output);
        
        // 后续 pass 使用上一次的输出作为输入
        input = output;
    }
}

void GaussianBlur::blurHorizontal(VkCommandBuffer cmd, TextureHandle input, TextureHandle temp) {
    params_.direction = Vec2(1.0f, 0.0f);
    
    // 转换布局
    backend_->imageBarrier(input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_READ_BIT);
    backend_->imageBarrier(temp, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_WRITE_BIT);
    
    backend_->setComputePipeline(pipeline_);
    
    // Push constants
    struct PushData {
        f32 directionX, directionY;
        f32 resolutionX, resolutionY;
        f32 sigma;
        i32 radius;
        f32 padding[2];
    } pushData;
    pushData.directionX = 1.0f;
    pushData.directionY = 0.0f;
    pushData.resolutionX = (f32)width_;
    pushData.resolutionY = (f32)height_;
    pushData.sigma = params_.sigma;
    pushData.radius = (i32)params_.radius;
    
    // backend_->pushConstants(shader_, 0, sizeof(pushData), &pushData);
    
    u32 groupX = (width_ + 15) / 16;
    u32 groupY = (height_ + 15) / 16;
    backend_->dispatchCompute(groupX, groupY, 1);
}

void GaussianBlur::blurVertical(VkCommandBuffer cmd, TextureHandle temp, TextureHandle output) {
    params_.direction = Vec2(0.0f, 1.0f);
    
    backend_->imageBarrier(temp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    backend_->imageBarrier(output, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_WRITE_BIT);
    
    backend_->setComputePipeline(pipeline_);
    
    u32 groupX = (width_ + 15) / 16;
    u32 groupY = (height_ + 15) / 16;
    backend_->dispatchCompute(groupX, groupY, 1);
}

// ============================================================================
// 泛光效果实现
// ============================================================================

bool BloomEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    // Create shader and pipeline
    const char* thresholdShader = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D inputTex;
layout(binding = 1, rgba16f) uniform image2D outputTex;

layout(push_constant) uniform ThresholdParams {
    float threshold;
    float knee;
    vec2 resolution;
} params;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= params.resolution.x || pixel.y >= params.resolution.y) return;
    
    vec2 uv = (vec2(pixel) + 0.5) / params.resolution;
    vec3 color = texture(inputTex, uv).rgb;
    
    // 亮度阈�?    float brightness = max(max(color.r, color.g), color.b);
    float softThreshold = params.threshold - params.knee;
    float contribution = max(0, brightness - softThreshold);
    contribution /= (brightness + 0.00001);
    contribution = clamp(contribution, 0, 1);
    
    vec3 result = color * contribution;
    imageStore(outputTex, pixel, vec4(result, 1.0));
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = thresholdShader;
    thresholdShader_ = backend_->createShader(shaderDesc);
    
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = thresholdShader_;
    thresholdPipeline_ = backend_->createPipeline(pipelineDesc);
    
    // Sampler
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    samplerDesc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerDesc.maxAnisotropy = 1.0f;
    sampler_ = backend_->createSampler(samplerDesc);
    
    return true;
}

void BloomEffect::shutdown() {
    if (backend_) {
        if (thresholdPipeline_.valid()) backend_->destroyPipeline(thresholdPipeline_);
        if (downsamplePipeline_.valid()) backend_->destroyPipeline(downsamplePipeline_);
        if (upsamplePipeline_.valid()) backend_->destroyPipeline(upsamplePipeline_);
        if (combinePipeline_.valid()) backend_->destroyPipeline(combinePipeline_);
        if (thresholdShader_.valid()) backend_->destroyShader(thresholdShader_);
        if (blurShader_.valid()) backend_->destroyShader(blurShader_);
        if (combineShader_.valid()) backend_->destroyShader(combineShader_);
        if (thresholdTexture_.valid()) backend_->destroyTexture(thresholdTexture_);
        for (auto& tex : bloomPyramid_) {
            if (tex.valid()) backend_->destroyTexture(tex);
        }
        if (sampler_.valid()) backend_->destroySampler(sampler_);
    }
}

void BloomEffect::resize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    
    // 重新创建纹理
    if (thresholdTexture_.valid()) {
        backend_->destroyTexture(thresholdTexture_);
    }
    for (auto& tex : bloomPyramid_) {
        if (tex.valid()) backend_->destroyTexture(tex);
    }
    bloomPyramid_.clear();
    
    // Create threshold texture
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = static_cast<Format>(VK_FORMAT_R16G16B16A16_SFLOAT);
    desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    thresholdTexture_ = backend_->createTexture(desc);
    
    // Create downsample pyramid
    u32 w = width;
    u32 h = height;
    for (u32 i = 0; i < params_.mipLevels; i++) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
        
        TextureDesc mipDesc;
        mipDesc.width = w;
        mipDesc.height = h;
        mipDesc.format = static_cast<Format>(VK_FORMAT_R16G16B16A16_SFLOAT);
        mipDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        
        bloomPyramid_.push_back(backend_->createTexture(mipDesc));
    }
}

void BloomEffect::setParams(const BloomParams& params) {
    params_ = params;
}

void BloomEffect::apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    if (!enabled_) return;
    
    // 1. Extract bright areas
    extractBright(cmd, input, thresholdTexture_);
    
    // 2. Downsample
    TextureHandle current = thresholdTexture_;
    for (u32 i = 0; i < params_.mipLevels; i++) {
        downsample(cmd, current, bloomPyramid_[i]);
        current = bloomPyramid_[i];
    }
    
    // 3. Upsample and accumulate
    for (i32 i = params_.mipLevels - 2; i >= 0; i--) {
        upsample(cmd, bloomPyramid_[i + 1], bloomPyramid_[i], bloomPyramid_[i]);
    }
    
    // 4. Combine to output
    combine(cmd, input, bloomPyramid_[0], output);
}

void BloomEffect::extractBright(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    // Set compute pipeline and dispatch
}

void BloomEffect::downsample(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    // Downsample
}

void BloomEffect::upsample(VkCommandBuffer cmd, TextureHandle lowRes, TextureHandle highRes, TextureHandle output) {
    // Upsample
}

void BloomEffect::combine(VkCommandBuffer cmd, TextureHandle original, TextureHandle bloom, TextureHandle output) {
    // Combine original and bloom
}

// ============================================================================
// Tonemap Effect
// ============================================================================

bool TonemapEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    const char* tonemapShader = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D inputTex;
layout(binding = 1, rgba8) uniform image2D outputTex;

layout(push_constant) uniform TonemapParams {
    int method;
    float exposure;
    float gamma;
    float contrast;
    float saturation;
    vec3 colorFilter;
} params;

// ACES 色调映射
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Reinhard 色调映射
vec3 Reinhard(vec3 x) {
    return x / (x + 1.0);
}

// Uncharted 2 色调映射
vec3 Uncharted2Tonemap(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTex);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    
    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    vec3 color = texture(inputTex, uv).rgb;
    
    // 应用曝光
    color *= params.exposure;
    
    // 应用颜色滤镜
    color *= params.colorFilter;
    
    // 色调映射
    switch (params.method) {
        case 0: // Linear
            color = clamp(color, 0.0, 1.0);
            break;
        case 1: // Reinhard
            color = Reinhard(color);
            break;
        case 2: // ACES
            color = ACESFilm(color);
            break;
        case 3: // Filmic
            color = pow(color, vec3(0.8));
            break;
        case 4: // Uncharted2
            color = Uncharted2Tonemap(color * 2.0);
            color = color * (1.0f / Uncharted2Tonemap(vec3(11.2)));
            break;
    }
    
    // 对比�?    color = (color - 0.5) * params.contrast + 0.5;
    
    // 饱和�?    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, params.saturation);
    
    // Gamma 校正
    color = pow(color, vec3(1.0 / params.gamma));
    
    imageStore(outputTex, pixel, vec4(color, 1.0));
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = tonemapShader;
    shader_ = backend_->createShader(shaderDesc);
    
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = shader_;
    pipeline_ = backend_->createPipeline(pipelineDesc);
    
    SamplerDesc samplerDesc;
    samplerDesc.minFilter = VK_FILTER_LINEAR;
    samplerDesc.magFilter = VK_FILTER_LINEAR;
    sampler_ = backend_->createSampler(samplerDesc);
    
    return true;
}

void TonemapEffect::shutdown() {
    if (backend_) {
        if (pipeline_.valid()) backend_->destroyPipeline(pipeline_);
        if (shader_.valid()) backend_->destroyShader(shader_);
        if (sampler_.valid()) backend_->destroySampler(sampler_);
    }
}

void TonemapEffect::setParams(const TonemapParams& params) {
    params_ = params;
}

void TonemapEffect::apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    if (!enabled_) return;
    
    // 转换布局
    backend_->imageBarrier(input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_READ_BIT);
    backend_->imageBarrier(output, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_WRITE_BIT);
    
    backend_->setComputePipeline(pipeline_);
    
    // 推送常量并派发
    // 实际需要获取图像尺�?    backend_->dispatchCompute(80, 45, 1);  // 假设 1280x720
}

// ============================================================================
// FXAA 实现和其他效�?..
// ============================================================================

bool FXAAEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    
    const char* fxaaShader = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D inputTex;
layout(binding = 1, rgba8) uniform image2D outputTex;

layout(push_constant) uniform FXAAParams {
    vec2 resolution;
    float subpixel;
    float edgeThreshold;
} params;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outputTex);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    
    vec2 uv = (vec2(pixel) + 0.5) / params.resolution;
    
    // FXAA 算法
    // 简化版本，实际实现更复�?    
    vec3 color = texture(inputTex, uv).rgb;
    
    // 边缘检�?    vec3 colorN = textureOffset(inputTex, uv, ivec2(0, 1)).rgb;
    vec3 colorS = textureOffset(inputTex, uv, ivec2(0, -1)).rgb;
    vec3 colorE = textureOffset(inputTex, uv, ivec2(1, 0)).rgb;
    vec3 colorW = textureOffset(inputTex, uv, ivec2(-1, 0)).rgb;
    
    float lumaM = dot(color, vec3(0.299, 0.587, 0.114));
    float lumaN = dot(colorN, vec3(0.299, 0.587, 0.114));
    float lumaS = dot(colorS, vec3(0.299, 0.587, 0.114));
    float lumaE = dot(colorE, vec3(0.299, 0.587, 0.114));
    float lumaW = dot(colorW, vec3(0.299, 0.587, 0.114));
    
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    
    float lumaRange = lumaMax - lumaMin;
    
    if (lumaRange < max(params.edgeThreshold, lumaMax * 0.125)) {
        imageStore(outputTex, pixel, vec4(color, 1.0));
        return;
    }
    
    // 边缘方向检测和混合
    float blend = lumaRange / lumaMax;
    blend = smoothstep(0.0, 1.0, blend);
    blend *= params.subpixel;
    
    vec3 avg = (colorN + colorS + colorE + colorW) * 0.25;
    color = mix(color, avg, blend * 0.5);
    
    imageStore(outputTex, pixel, vec4(color, 1.0));
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = fxaaShader;
    shader_ = backend_->createShader(shaderDesc);
    
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = shader_;
    pipeline_ = backend_->createPipeline(pipelineDesc);
    
    return true;
}

void FXAAEffect::shutdown() {
    if (backend_) {
        if (pipeline_.valid()) backend_->destroyPipeline(pipeline_);
        if (shader_.valid()) backend_->destroyShader(shader_);
    }
}

void FXAAEffect::setParams(const FXAAParams& params) {
    params_ = params;
}

void FXAAEffect::apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    if (!enabled_) return;
    
    backend_->imageBarrier(input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_READ_BIT);
    backend_->imageBarrier(output, VK_IMAGE_LAYOUT_GENERAL,
                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            0, VK_ACCESS_SHADER_WRITE_BIT);
    
    backend_->setComputePipeline(pipeline_);
    backend_->dispatchCompute(80, 45, 1);
}

// Other effects implementations...
// ColorGradingEffect, MotionBlurEffect, DOFEffect omitted

bool ColorGradingEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    // 实现...
    return true;
}

void ColorGradingEffect::shutdown() {}
void ColorGradingEffect::setParams(const ColorGradingParams&) {}
void ColorGradingEffect::apply(VkCommandBuffer, TextureHandle, TextureHandle) {}

bool MotionBlurEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void MotionBlurEffect::shutdown() {}
void MotionBlurEffect::setParams(const MotionBlurParams&) {}
void MotionBlurEffect::apply(VkCommandBuffer, TextureHandle, TextureHandle) {}

bool DOFEffect::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void DOFEffect::shutdown() {}
void DOFEffect::resize(u32, u32) {}
void DOFEffect::setParams(const DOFParams&) {}
void DOFEffect::apply(VkCommandBuffer, TextureHandle, TextureHandle) {}

// ============================================================================
// PostProcessStack Implementation
// ============================================================================

PostProcessStack::PostProcessStack() = default;

PostProcessStack::~PostProcessStack() {
    shutdown();
}

bool PostProcessStack::initialize(VulkanBackend* backend) {
    backend_ = backend;
    return true;
}

void PostProcessStack::shutdown() {
    effects_.clear();
    
    for (auto& tex : tempTextures_) {
        if (tex.valid()) {
            backend_->destroyTexture(tex);
        }
    }
    tempTextures_.clear();
    
    backend_ = nullptr;
}

void PostProcessStack::resize(u32 width, u32 height) {
    width_ = width;
    height_ = height;
    
    // 重新创建临时纹理
    for (auto& tex : tempTextures_) {
        if (tex.valid()) {
            backend_->destroyTexture(tex);
        }
    }
    tempTextures_.clear();
    
    // Create temp texture pool
    for (u32 i = 0; i < 4; i++) {
        TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.format = static_cast<Format>(VK_FORMAT_R16G16B16A16_SFLOAT);
        desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | 
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        tempTextures_.push_back(backend_->createTexture(desc));
    }
    
    // Resize all effects
    for (auto& effect : effects_) {
        effect->resize(width, height);
    }
}

void PostProcessStack::addEffect(std::unique_ptr<PostProcessEffect> effect) {
    effect->initialize(backend_);
    effect->resize(width_, height_);
    effects_.push_back(std::move(effect));
}

void PostProcessStack::removeEffect(const char* name) {
    effects_.erase(
        std::remove_if(effects_.begin(), effects_.end(),
                       [name](const std::unique_ptr<PostProcessEffect>& e) {
                           return strcmp(e->getName(), name) == 0;
                       }),
        effects_.end());
}

PostProcessEffect* PostProcessStack::getEffect(const char* name) {
    for (auto& effect : effects_) {
        if (strcmp(effect->getName(), name) == 0) {
            return effect.get();
        }
    }
    return nullptr;
}

TextureHandle PostProcessStack::getTempTexture() {
    TextureHandle tex = tempTextures_[currentTemp_];
    currentTemp_ = (currentTemp_ + 1) % tempTextures_.size();
    return tex;
}

void PostProcessStack::process(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) {
    if (effects_.empty()) return;
    
    TextureHandle currentInput = input;
    TextureHandle currentOutput;
    
    currentTemp_ = 0;
    
    for (size_t i = 0; i < effects_.size(); i++) {
        if (!effects_[i]->isEnabled()) continue;
        
        // Last effect outputs to final target
        if (i == effects_.size() - 1) {
            currentOutput = output;
        } else {
            currentOutput = getTempTexture();
        }
        
        effects_[i]->apply(cmd, currentInput, currentOutput);
        currentInput = currentOutput;
    }
}

GaussianBlur* PostProcessStack::getGaussianBlur() {
    return dynamic_cast<GaussianBlur*>(getEffect("GaussianBlur"));
}

BloomEffect* PostProcessStack::getBloom() {
    return dynamic_cast<BloomEffect*>(getEffect("Bloom"));
}

TonemapEffect* PostProcessStack::getTonemap() {
    return dynamic_cast<TonemapEffect*>(getEffect("Tonemap"));
}

ColorGradingEffect* PostProcessStack::getColorGrading() {
    return dynamic_cast<ColorGradingEffect*>(getEffect("ColorGrading"));
}

FXAAEffect* PostProcessStack::getFXAA() {
    return dynamic_cast<FXAAEffect*>(getEffect("FXAA"));
}

} // namespace Nova

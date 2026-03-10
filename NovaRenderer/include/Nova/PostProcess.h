/**
 * Nova Renderer - Post Processing Effects
 * 后处理效果系统
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 后处理效果基类
// ============================================================================

class PostProcessEffect {
public:
    virtual ~PostProcessEffect() = default;
    
    virtual bool initialize(VulkanBackend* backend) = 0;
    virtual void shutdown() = 0;
    virtual void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) = 0;
    
    virtual void resize(u32 width, u32 height) {}
    virtual const char* getName() const = 0;
    
    bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    
protected:
    VulkanBackend* backend_ = nullptr;
    bool enabled_ = true;
};

// ============================================================================
// 高斯模糊
// ============================================================================

struct GaussianBlurParams {
    f32 radius = 5.0f;
    f32 sigma = 2.0f;
    u32 passes = 1;
    Vec2 direction = {1.0f, 0.0f};  // 水平或垂直
};

class GaussianBlur : public PostProcessEffect {
public:
    GaussianBlur() = default;
    ~GaussianBlur() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    void resize(u32 width, u32 height) override;
    const char* getName() const override { return "GaussianBlur"; }
    
    void setParams(const GaussianBlurParams& params);
    const GaussianBlurParams& getParams() const { return params_; }
    
    // 分离模糊 (两 pass)
    void blurHorizontal(VkCommandBuffer cmd, TextureHandle input, TextureHandle temp);
    void blurVertical(VkCommandBuffer cmd, TextureHandle temp, TextureHandle output);
    
private:
    GaussianBlurParams params_;
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    
    TextureHandle tempTexture_;
    SamplerHandle sampler_;
    
    u32 width_ = 0;
    u32 height_ = 0;
};

// ============================================================================
// 泛光效果 (Bloom)
// ============================================================================

struct BloomParams {
    f32 threshold = 1.0f;       // 亮度阈值
    f32 intensity = 0.5f;       // 泛光强度
    f32 radius = 5.0f;          // 模糊半径
    u32 mipLevels = 5;          // Mip 层数
    f32 knee = 0.1f;            // 过渡区域
};

class BloomEffect : public PostProcessEffect {
public:
    BloomEffect() = default;
    ~BloomEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    void resize(u32 width, u32 height) override;
    const char* getName() const override { return "Bloom"; }
    
    void setParams(const BloomParams& params);
    const BloomParams& getParams() const { return params_; }
    
private:
    void extractBright(VkCommandBuffer cmd, TextureHandle input, TextureHandle output);
    void downsample(VkCommandBuffer cmd, TextureHandle input, TextureHandle output);
    void upsample(VkCommandBuffer cmd, TextureHandle lowRes, TextureHandle highRes, TextureHandle output);
    void combine(VkCommandBuffer cmd, TextureHandle original, TextureHandle bloom, TextureHandle output);
    
    BloomParams params_;
    
    // 管线
    PipelineHandle thresholdPipeline_;
    PipelineHandle downsamplePipeline_;
    PipelineHandle upsamplePipeline_;
    PipelineHandle combinePipeline_;
    
    // 着色器
    ShaderHandle thresholdShader_;
    ShaderHandle blurShader_;
    ShaderHandle combineShader_;
    
    // 临时纹理
    std::vector<TextureHandle> bloomPyramid_;  // 下采样金字塔
    TextureHandle thresholdTexture_;
    
    SamplerHandle sampler_;
    
    u32 width_ = 0;
    u32 height_ = 0;
};

// ============================================================================
// 色调映射 (Tonemapping)
// ============================================================================

enum class TonemapMethod : u32 {
    Linear = 0,
    Reinhard = 1,
    ACES = 2,
    Filmic = 3,
    Uncharted2 = 4
};

struct TonemapParams {
    TonemapMethod method = TonemapMethod::ACES;
    f32 exposure = 1.0f;
    f32 gamma = 2.2f;
    f32 contrast = 1.0f;
    f32 saturation = 1.0f;
    Vec3 colorFilter = {1.0f, 1.0f, 1.0f};
};

class TonemapEffect : public PostProcessEffect {
public:
    TonemapEffect() = default;
    ~TonemapEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    const char* getName() const override { return "Tonemap"; }
    
    void setParams(const TonemapParams& params);
    const TonemapParams& getParams() const { return params_; }
    
private:
    TonemapParams params_;
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    SamplerHandle sampler_;
};

// ============================================================================
// 色彩校正 (Color Grading)
// ============================================================================

struct ColorGradingParams {
    f32 brightness = 0.0f;      // -1 to 1
    f32 contrast = 1.0f;        // 0 to 2
    f32 saturation = 1.0f;      // 0 to 2
    f32 vibrance = 0.0f;        // -1 to 1
    f32 hue = 0.0f;             // 0 to 360
    f32 temperature = 0.0f;     // -1 (warm) to 1 (cool)
    f32 tint = 0.0f;            // -1 (green) to 1 (magenta)
    f32 sharpening = 0.0f;      // 0 to 1
    f32 vignette = 0.0f;        // 0 to 1
    f32 vignetteRadius = 0.8f;
    f32 vignetteSoftness = 0.3f;
    
    // LUT 查找表
    TextureHandle lutTexture;
    f32 lutIntensity = 0.0f;    // 0 to 1
};

class ColorGradingEffect : public PostProcessEffect {
public:
    ColorGradingEffect() = default;
    ~ColorGradingEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    const char* getName() const override { return "ColorGrading"; }
    
    void setParams(const ColorGradingParams& params);
    const ColorGradingParams& getParams() const { return params_; }
    
private:
    ColorGradingParams params_;
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    SamplerHandle sampler_;
};

// ============================================================================
// FXAA 抗锯齿
// ============================================================================

struct FXAAParams {
    f32 subpixelAliasingRemoval = 0.75f;   // 子像素混合
    f32 edgeThreshold = 0.125f;            // 边缘检测阈值
    f32 minEdgeThreshold = 0.0417f;        // 最小阈值
};

class FXAAEffect : public PostProcessEffect {
public:
    FXAAEffect() = default;
    ~FXAAEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    const char* getName() const override { return "FXAA"; }
    
    void setParams(const FXAAParams& params);
    const FXAAParams& getParams() const { return params_; }
    
private:
    FXAAParams params_;
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    SamplerHandle sampler_;
};

// ============================================================================
// 运动模糊
// ============================================================================

struct MotionBlurParams {
    f32 intensity = 0.5f;       // 模糊强度
    u32 samples = 16;           // 采样数
    f32 minVelocity = 1.0f;     // 最小速度阈值
};

class MotionBlurEffect : public PostProcessEffect {
public:
    MotionBlurEffect() = default;
    ~MotionBlurEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    const char* getName() const override { return "MotionBlur"; }
    
    void setParams(const MotionBlurParams& params);
    const MotionBlurParams& getParams() const { return params_; }
    
    void setVelocityBuffer(TextureHandle velocity) { velocityBuffer_ = velocity; }
    void setDepthBuffer(TextureHandle depth) { depthBuffer_ = depth; }
    
private:
    MotionBlurParams params_;
    PipelineHandle pipeline_;
    ShaderHandle shader_;
    SamplerHandle sampler_;
    
    TextureHandle velocityBuffer_;
    TextureHandle depthBuffer_;
    Mat4 prevViewProj_;
};

// ============================================================================
// 景深 (Depth of Field)
// ============================================================================

struct DOFParams {
    f32 focusDistance = 10.0f;  // 焦点距离
    f32 focusRange = 5.0f;      // 焦点范围
    f32 bokehRadius = 5.0f;     // 散景半径
    f32 maxCoC = 10.0f;         // 最大混淆圆
};

class DOFEffect : public PostProcessEffect {
public:
    DOFEffect() = default;
    ~DOFEffect() override = default;
    
    bool initialize(VulkanBackend* backend) override;
    void shutdown() override;
    void apply(VkCommandBuffer cmd, TextureHandle input, TextureHandle output) override;
    void resize(u32 width, u32 height) override;
    const char* getName() const override { return "DOF"; }
    
    void setParams(const DOFParams& params);
    const DOFParams& getParams() const { return params_; }
    
    void setDepthBuffer(TextureHandle depth) { depthBuffer_ = depth; }
    
private:
    void calculateCoC(VkCommandBuffer cmd, TextureHandle depth, TextureHandle cocOutput);
    void blurNear(VkCommandBuffer cmd, TextureHandle input, TextureHandle coc, TextureHandle output);
    void blurFar(VkCommandBuffer cmd, TextureHandle input, TextureHandle coc, TextureHandle output);
    void composite(VkCommandBuffer cmd, TextureHandle original, 
                   TextureHandle nearBlur, TextureHandle farBlur, 
                   TextureHandle coc, TextureHandle output);
    
    DOFParams params_;
    
    PipelineHandle cocPipeline_;
    PipelineHandle blurPipeline_;
    PipelineHandle compositePipeline_;
    
    ShaderHandle cocShader_;
    ShaderHandle blurShader_;
    ShaderHandle compositeShader_;
    
    TextureHandle cocTexture_;
    TextureHandle nearBlurTexture_;
    TextureHandle farBlurTexture_;
    TextureHandle depthBuffer_;
    
    SamplerHandle sampler_;
    
    u32 width_ = 0;
    u32 height_ = 0;
};

// ============================================================================
// 后处理栈
// ============================================================================

class PostProcessStack {
public:
    PostProcessStack();
    ~PostProcessStack();
    
    bool initialize(VulkanBackend* backend);
    void shutdown();
    
    void resize(u32 width, u32 height);
    
    // 添加效果
    void addEffect(std::unique_ptr<PostProcessEffect> effect);
    void removeEffect(const char* name);
    PostProcessEffect* getEffect(const char* name);
    
    // 执行所有效果
    void process(VkCommandBuffer cmd, TextureHandle input, TextureHandle output);
    
    // 常用效果快捷访问
    GaussianBlur* getGaussianBlur();
    BloomEffect* getBloom();
    TonemapEffect* getTonemap();
    ColorGradingEffect* getColorGrading();
    FXAAEffect* getFXAA();
    
private:
    VulkanBackend* backend_ = nullptr;
    std::vector<std::unique_ptr<PostProcessEffect>> effects_;
    
    // 临时纹理
    std::vector<TextureHandle> tempTextures_;
    u32 currentTemp_ = 0;
    
    u32 width_ = 0;
    u32 height_ = 0;
    
    TextureHandle getTempTexture();
};

} // namespace Nova

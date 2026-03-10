/**
 * Nova Renderer - Liquid Glass Effect
 * 完整的液体玻璃/毛玻璃效果实现
 * 
 * 包含：
 * - 背景模糊 (高斯模糊)
 * - 折射/位移效果
 * - 动态光斑
 * - 边缘发光
 * - SDF 圆角裁剪
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 液体玻璃参数
// ============================================================================

struct LiquidGlassParams {
    // === 模糊参数 ===
    f32 blurRadius = 15.0f;         // 模糊半径 (像素)
    f32 blurSigma = 8.0f;           // 高斯分布标准差
    u32 blurPasses = 2;             // 模糊迭代次数
    
    // === 折射参数 ===
    f32 refractionStrength = 0.02f; // 折射强度 (UV 偏移量)
    f32 refractionScale = 50.0f;    // 折射纹理缩放
    f32 refractionSpeed = 0.5f;     // 折射动画速度
    
    // === 颜色参数 ===
    Color tintColor = {0.9f, 0.95f, 1.0f, 1.0f};  // 色调
    f32 tintStrength = 0.15f;       // 色调强度
    f32 brightness = 1.0f;          // 亮度
    f32 saturation = 0.9f;          // 饱和度
    f32 contrast = 1.05f;           // 对比度
    
    // === 边缘参数 ===
    f32 borderWidth = 1.0f;         // 边框宽度
    Color borderColor = {0.4f, 0.6f, 1.0f, 1.0f};  // 边框颜色
    f32 borderGlow = 0.5f;          // 边框发光强度
    f32 borderGlowSpeed = 2.0f;     // 边框发光动画速度
    
    // === 光斑参数 ===
    f32 spotIntensity = 0.8f;       // 光斑强度
    f32 spotRadius = 100.0f;        // 光斑半径
    f32 spotSpeed = 1.5f;           // 光斑动画速度
    Vec2 spotPosition = {0.5f, 0.3f}; // 光斑位置 (归一化)
    
    // === 形状参数 ===
    f32 cornerRadius = 20.0f;       // 圆角半径
    f32 opacity = 0.95f;            // 整体不透明度
    
    // === 高级参数 ===
    bool enableRefraction = true;   // 启用折射
    bool enableBlur = true;         // 启用模糊
    bool enableSpot = true;         // 启用光斑
    bool enableBorder = true;       // 启用边框
};

// ============================================================================
// 液体玻璃渲染器
// ============================================================================

class LiquidGlassRenderer {
public:
    LiquidGlassRenderer() = default;
    ~LiquidGlassRenderer() { shutdown(); }
    
    // 初始化
    bool initialize(VulkanBackend* backend, u32 width, u32 height);
    void shutdown();
    
    // 调整大小
    void resize(u32 width, u32 height);
    
    // 设置参数
    void setParams(const LiquidGlassParams& params) { params_ = params; }
    const LiquidGlassParams& getParams() const { return params_; }
    
    // === 渲染流程 ===
    
    // 1. 开始捕获背景 (渲染到离屏缓冲)
    void beginBackgroundCapture(VkCommandBuffer cmd);
    
    // 2. 结束背景捕获
    void endBackgroundCapture(VkCommandBuffer cmd);
    
    // 3. 执行模糊
    void applyBlur(VkCommandBuffer cmd);
    
    // 4. 应用折射效果
    void applyRefraction(VkCommandBuffer cmd, f32 time);
    
    // 5. 渲染最终玻璃层
    void renderGlass(VkCommandBuffer cmd, f32 time, const Rect& screenRect);
    
    // 6. 合成到屏幕
    void composite(VkCommandBuffer cmd, TextureHandle output);
    
    // === 单步渲染 (简化接口) ===
    void render(VkCommandBuffer cmd, TextureHandle background, 
                TextureHandle output, f32 time, const Rect& screenRect);
    
    // 获取纹理 (用于调试)
    TextureHandle getBackgroundTexture() const { return backgroundTexture_; }
    TextureHandle getBlurredTexture() const { return blurredTexture_; }
    TextureHandle getRefractionTexture() const { return refractionTexture_; }
    
private:
    // 创建资源
    bool createTextures(u32 width, u32 height);
    bool createShaders();
    bool createPipelines();
    bool createSampler();
    
    // 销毁资源
    void destroyTextures();
    void destroyShaders();
    void destroyPipelines();
    
    // 辅助函数
    void imageBarrier(TextureHandle texture, VkImageLayout newLayout,
                      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                      VkAccessFlags srcAccess, VkAccessFlags dstAccess);
    
    VulkanBackend* backend_ = nullptr;
    LiquidGlassParams params_;
    
    u32 width_ = 0;
    u32 height_ = 0;
    
    // === 纹理资源 ===
    TextureHandle backgroundTexture_;    // 原始背景
    TextureHandle blurredTexture_;       // 模糊后的背景
    TextureHandle tempBlurTexture_;      // 模糊中间纹理
    TextureHandle refractionTexture_;    // 折射/位移纹理
    TextureHandle outputTexture_;        // 输出纹理
    TextureHandle noiseTexture_;         // 噪声纹理 (用于折射)
    
    // === 着色器 ===
    ShaderHandle blurShader_;            // 高斯模糊
    ShaderHandle refractionShader_;      // 折射效果
    ShaderHandle glassShader_;           // 最终玻璃合成
    ShaderHandle noiseShader_;           // 噪声生成
    
    // === 管线 ===
    PipelineHandle blurPipeline_;
    PipelineHandle blurPipelineV_;       // 垂直模糊
    PipelineHandle refractionPipeline_;
    PipelineHandle glassPipeline_;
    PipelineHandle noisePipeline_;
    
    // === 采样器 ===
    SamplerHandle linearSampler_;
    SamplerHandle pointSampler_;
    
    // === 顶点缓冲 ===
    BufferHandle quadVertexBuffer_;
    BufferHandle quadIndexBuffer_;
    
    // === 帧缓冲 ===
    FramebufferHandle backgroundFBO_;
    FramebufferHandle blurFBO_;
    FramebufferHandle outputFBO_;
};

// ============================================================================
// 液体玻璃着色器源码
// ============================================================================

namespace LiquidGlassShaders {

// 高斯模糊计算着色器
const char* GaussianBlurCS = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D inputTex;
layout(binding = 1, rgba16f) uniform image2D outputTex;

layout(push_constant) uniform PC {
    vec2 direction;      // (1,0) 或 (0,1)
    vec2 resolution;
    float sigma;
    int radius;
    float padding;
} pc;

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= pc.resolution.x || pixel.y >= pc.resolution.y) return;
    
    vec2 texelSize = 1.0 / pc.resolution;
    vec2 uv = (vec2(pixel) + 0.5) * texelSize;
    
    vec4 color = vec4(0.0);
    float weightSum = 0.0;
    
    // 计算权重
    float weights[33]; // 最大半径 32
    float totalWeight = 0.0;
    
    for (int i = -pc.radius; i <= pc.radius; i++) {
        float w = gaussian(float(i), pc.sigma);
        weights[i + pc.radius] = w;
        totalWeight += w;
    }
    
    // 采样
    for (int i = -pc.radius; i <= pc.radius; i++) {
        float w = weights[i + pc.radius] / totalWeight;
        vec2 offset = pc.direction * float(i) * texelSize;
        color += texture(inputTex, uv + offset) * w;
        weightSum += w;
    }
    
    color /= weightSum;
    imageStore(outputTex, pixel, color);
}
)";

// 折射/位移片段着色器
const char* RefractionFS = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 transform;
    vec4 rectParams;    // x, y, width, height
    vec4 glassParams;   // cornerRadius, refractionStrength, refractionScale, time
    vec4 colorParams;   // tintColor.rgb, tintStrength
    vec4 advParams;     // brightness, saturation, contrast, opacity
    vec4 spotParams;    // spotIntensity, spotRadius, spotSpeed, spotPosition.x
    vec4 spotParams2;   // spotPosition.y, borderGlow, borderGlowSpeed, borderWidth
    vec4 borderColor;   // border color rgba
    vec4 flags;         // enableRefraction, enableBlur, enableSpot, enableBorder
} pc;

layout(binding = 0) uniform sampler2D backgroundTex;
layout(binding = 1) uniform sampler2D blurredTex;
layout(binding = 2) uniform sampler2D noiseTex;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// SDF 圆角矩形
float roundedRectSDF(vec2 p, vec2 size, float r) {
    vec2 d = abs(p) - size + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

// 噪声函数
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    
    // 转换到矩形局部坐标
    vec2 localPos = fragCoord - pc.rectParams.xy - pc.rectParams.zw * 0.5;
    vec2 halfSize = pc.rectParams.zw * 0.5;
    
    // SDF 计算
    float dist = roundedRectSDF(localPos, halfSize, pc.glassParams.x);
    
    // 裁剪
    if (dist > 1.0) discard;
    
    // 平滑边缘
    float edgeAlpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    
    // UV 坐标
    vec2 uv = fragCoord / pc.rectParams.zw;
    
    // === 折射效果 ===
    vec2 refractionOffset = vec2(0.0);
    
    if (pc.flags.x > 0.5) { // enableRefraction
        // 计算噪声偏移
        float time = pc.glassParams.w;
        vec2 noiseCoord = localPos / pc.glassParams.z + time * 0.1;
        float n = fbm(noiseCoord * 0.5);
        
        // 动态波动
        float wave = sin(localPos.x * 0.05 + time * 2.0) * cos(localPos.y * 0.05 + time * 1.5);
        wave = wave * 0.5 + 0.5;
        
        // 组合偏移
        float strength = pc.glassParams.y * (0.5 + wave * 0.5);
        refractionOffset = vec2(
            cos(n * 6.28 + time) * strength,
            sin(n * 6.28 + time * 0.7) * strength
        );
        
        // 边缘减弱
        float edgeFactor = smoothstep(0.0, pc.glassParams.x, -dist);
        refractionOffset *= edgeFactor;
    }
    
    // === 采样背景 ===
    vec2 screenUV = fragCoord / vec2(pc.rectParams.z, pc.rectParams.w);
    vec2 refractedUV = screenUV + refractionOffset;
    
    vec4 bgColor;
    if (pc.flags.y > 0.5) { // enableBlur
        bgColor = texture(blurredTex, refractedUV);
    } else {
        bgColor = texture(backgroundTex, refractedUV);
    }
    
    // === 颜色处理 ===
    vec3 color = bgColor.rgb;
    
    // 色调
    color = mix(color, color * pc.colorParams.rgb, pc.colorParams.a);
    
    // 亮度
    color *= pc.advParams.x;
    
    // 饱和度
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, pc.advParams.y);
    
    // 对比度
    color = (color - 0.5) * pc.advParams.z + 0.5;
    
    // === 光斑效果 ===
    if (pc.flags.z > 0.5) { // enableSpot
        vec2 spotPos = pc.rectParams.xy + pc.rectParams.zw * vec2(pc.spotParams.w, pc.spotParams2.x);
        spotPos += vec2(sin(pc.glassParams.w * pc.spotParams.z) * 30.0,
                        cos(pc.glassParams.w * pc.spotParams.z * 0.7) * 20.0);
        
        float spotDist = length(fragCoord - spotPos);
        float spotRadius = pc.spotParams.y;
        float spot = 1.0 - smoothstep(spotRadius * 0.5, spotRadius, spotDist);
        spot *= pc.spotParams.x;
        
        // 光斑颜色渐变
        vec3 spotColor = mix(vec3(1.0, 0.95, 0.9), vec3(0.9, 0.95, 1.0), 
                            sin(pc.glassParams.w * 0.5) * 0.5 + 0.5);
        color += spotColor * spot * 0.2;
    }
    
    // === 边框效果 ===
    float borderAlpha = 0.0;
    if (pc.flags.w > 0.5) { // enableBorder
        float borderWidth = pc.spotParams2.w;
        float innerDist = abs(dist + borderWidth);
        
        // 发光边框
        float glow = exp(-innerDist * 0.1) * pc.spotParams2.y;
        glow *= 0.5 + 0.5 * sin(pc.glassParams.w * pc.spotParams2.z);
        
        borderAlpha = glow;
        color = mix(color, pc.borderColor.rgb, glow * pc.borderColor.a);
    }
    
    // === 输出 ===
    float alpha = edgeAlpha * pc.advParams.w;
    outColor = vec4(color, alpha);
}
)";

// 简化版玻璃片段着色器 (用于演示)
const char* SimpleGlassFS = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 transform;
    vec4 rectParams;    // x, y, width, height
    vec4 color;         // rgba
    float radius;
    float time;
    float refractionStrength;
    float blurStrength;
} pc;

layout(binding = 0) uniform sampler2D backgroundTex;
layout(binding = 1) uniform sampler2D blurTex;

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

// SDF 圆角矩形
float roundedRectSDF(vec2 p, vec2 size, float r) {
    vec2 d = abs(p) - size + r;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - r;
}

// 噪声
float noise(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 fragCoord = gl_FragCoord.xy;
    
    // 局部坐标
    vec2 localPos = fragCoord - pc.rectParams.xy - pc.rectParams.zw * 0.5;
    vec2 halfSize = pc.rectParams.zw * 0.5;
    
    // SDF
    float dist = roundedRectSDF(localPos, halfSize, pc.radius);
    
    // 裁剪
    if (dist > 1.0) discard;
    
    // 边缘平滑
    float edgeAlpha = 1.0 - smoothstep(-1.0, 1.0, dist);
    
    // 折射偏移
    float time = pc.time;
    float n = noise(localPos * 0.1 + time * 0.5);
    vec2 refractOffset = vec2(
        cos(n * 6.28318 + time) * pc.refractionStrength,
        sin(n * 6.28318 + time * 0.7) * pc.refractionStrength
    );
    
    // 边缘减弱折射
    float edgeFade = smoothstep(0.0, pc.radius, -dist);
    refractOffset *= edgeFade;
    
    // 采样
    vec2 uv = fragCoord / pc.rectParams.zw;
    vec2 refractedUV = uv + refractOffset;
    
    // 混合原始和模糊背景
    vec4 bgOriginal = texture(backgroundTex, refractedUV);
    vec4 bgBlurred = texture(blurTex, refractedUV);
    vec4 bgColor = mix(bgOriginal, bgBlurred, pc.blurStrength);
    
    // 应用颜色
    vec3 color = bgColor.rgb;
    color = mix(color, color * pc.color.rgb, pc.color.a * 0.3);
    
    // 简单光斑
    vec2 spotPos = pc.rectParams.xy + pc.rectParams.zw * vec2(0.5, 0.3);
    spotPos += vec2(sin(time * 1.5) * 20.0, cos(time) * 15.0);
    float spotDist = length(fragCoord - spotPos);
    float spot = exp(-spotDist * 0.02) * 0.15;
    color += vec3(spot);
    
    // 边缘发光
    float borderGlow = exp(-abs(dist) * 0.1) * 0.3;
    borderGlow *= 0.7 + 0.3 * sin(time * 2.0);
    color += vec3(0.3, 0.5, 0.8) * borderGlow;
    
    outColor = vec4(color, edgeAlpha * pc.color.a);
}
)";

// 顶点着色器
const char* GlassVS = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 transform;
} pc;

layout(location = 0) in vec2 inPosition;

void main() {
    gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
}
)";

// 完整版顶点着色器 (带 UV)
const char* GlassUVS = R"(
#version 450

layout(push_constant) uniform PC {
    mat4 transform;
    vec4 rectParams;
    vec4 glassParams;
    vec4 colorParams;
    vec4 advParams;
    vec4 spotParams;
    vec4 spotParams2;
    vec4 borderColor;
    vec4 flags;
} pc;

layout(location = 0) in vec2 inPosition;

layout(location = 0) out vec2 vUV;

void main() {
    gl_Position = pc.transform * vec4(inPosition, 0.0, 1.0);
    vUV = inPosition;
}
)";

// 噪声生成计算着色器
const char* NoiseCS = R"(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform image2D outputTex;

layout(push_constant) uniform PC {
    vec2 resolution;
    float scale;
    float time;
} pc;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= pc.resolution.x || pixel.y >= pc.resolution.y) return;
    
    vec2 uv = vec2(pixel) / pc.resolution;
    float n = fbm(uv * pc.scale + pc.time * 0.1);
    
    // 存储为法线贴图格式 (XY 分量)
    vec2 dx = vec2(fbm(uv * pc.scale + vec2(0.01, 0.0)) - n, 0.0);
    vec2 dy = vec2(0.0, fbm(uv * pc.scale + vec2(0.0, 0.01)) - n);
    vec3 normal = normalize(vec3(dx * 10.0, 1.0));
    
    imageStore(outputTex, pixel, vec4(normal * 0.5 + 0.5, n));
}
)";

} // namespace LiquidGlassShaders

} // namespace Nova

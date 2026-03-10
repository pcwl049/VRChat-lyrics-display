/**
 * Nova Renderer - Liquid Glass Effect Implementation
 * 完整的液体玻璃效果实现
 */

#include "Nova/LiquidGlass.h"
#include "Nova/VulkanBackend.h"
#include <cmath>

namespace Nova {

// ============================================================================
// 初始化和清理
// ============================================================================

bool LiquidGlassRenderer::initialize(VulkanBackend* backend, u32 width, u32 height) {
    backend_ = backend;
    width_ = width;
    height_ = height;
    
    if (!createSampler()) return false;
    if (!createShaders()) return false;
    if (!createPipelines()) return false;
    if (!createTextures(width, height)) return false;
    
    printf("LiquidGlassRenderer initialized: %ux%u\n", width, height);
    return true;
}

void LiquidGlassRenderer::shutdown() {
    if (!backend_) return;
    
    destroyTextures();
    destroyPipelines();
    destroyShaders();
    
    if (linearSampler_.valid()) backend_->destroySampler(linearSampler_);
    if (pointSampler_.valid()) backend_->destroySampler(pointSampler_);
    
    backend_ = nullptr;
}

void LiquidGlassRenderer::resize(u32 width, u32 height) {
    if (width == width_ && height == height_) return;
    
    width_ = width;
    height_ = height;
    
    destroyTextures();
    createTextures(width, height);
    
    printf("LiquidGlassRenderer resized: %ux%u\n", width, height);
}

// ============================================================================
// 资源创建
// ============================================================================

bool LiquidGlassRenderer::createTextures(u32 width, u32 height) {
    // 背景纹理
    TextureDesc bgDesc;
    bgDesc.width = width;
    bgDesc.height = height;
    bgDesc.format = Format::RGBA16F;
    bgDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | 
                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    backgroundTexture_ = backend_->createTexture(bgDesc);
    if (!backgroundTexture_.valid()) {
        printf("Failed to create background texture\n");
        return false;
    }
    
    // 模糊纹理
    TextureDesc blurDesc = bgDesc;
    blurredTexture_ = backend_->createTexture(blurDesc);
    tempBlurTexture_ = backend_->createTexture(blurDesc);
    
    // 输出纹理
    outputTexture_ = backend_->createTexture(bgDesc);
    
    // 折射噪声纹理
    TextureDesc noiseDesc;
    noiseDesc.width = 256;
    noiseDesc.height = 256;
    noiseDesc.format = Format::RGBA8_UNORM;
    noiseDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    noiseTexture_ = backend_->createTexture(noiseDesc);
    
    printf("Textures created\n");
    return true;
}

bool LiquidGlassRenderer::createShaders() {
    using namespace LiquidGlassShaders;
    
    ShaderDesc shaderDesc;
    
    // 模糊着色器
    shaderDesc.computeSource = GaussianBlurCS;
    shaderDesc.debugName = "BlurShader";
    blurShader_ = backend_->createShader(shaderDesc);
    if (!blurShader_.valid()) {
        printf("Failed to create blur shader\n");
        return false;
    }
    
    // 玻璃着色器
    shaderDesc.computeSource = nullptr;
    shaderDesc.vertexSource = GlassUVS;
    shaderDesc.fragmentSource = RefractionFS;
    shaderDesc.debugName = "GlassShader";
    glassShader_ = backend_->createShader(shaderDesc);
    if (!glassShader_.valid()) {
        printf("Failed to create glass shader\n");
        return false;
    }
    
    // 噪声着色器
    shaderDesc.vertexSource = nullptr;
    shaderDesc.fragmentSource = nullptr;
    shaderDesc.computeSource = NoiseCS;
    shaderDesc.debugName = "NoiseShader";
    noiseShader_ = backend_->createShader(shaderDesc);
    
    printf("Shaders created\n");
    return true;
}

bool LiquidGlassRenderer::createPipelines() {
    PipelineDesc pipelineDesc;
    
    // 模糊管线 (计算)
    pipelineDesc.shader = blurShader_;
    pipelineDesc.debugName = "BlurPipeline";
    blurPipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 玻璃管线
    pipelineDesc.shader = glassShader_;
    pipelineDesc.debugName = "GlassPipeline";
    pipelineDesc.blendMode = 1;  // Alpha blending
    
    // 顶点属性
    pipelineDesc.bindings.clear();
    pipelineDesc.attributes.clear();
    pipelineDesc.bindings.push_back({0, sizeof(f32) * 2, false});
    pipelineDesc.attributes.push_back({0, VK_FORMAT_R32G32_SFLOAT, 0});
    
    glassPipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 噪声管线
    pipelineDesc.shader = noiseShader_;
    pipelineDesc.debugName = "NoisePipeline";
    pipelineDesc.bindings.clear();
    pipelineDesc.attributes.clear();
    noisePipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 创建四边形顶点缓冲
    struct Vertex { f32 x, y; };
    Vertex quadVertices[] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    };
    
    BufferDesc vbDesc;
    vbDesc.size = sizeof(quadVertices);
    vbDesc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    vbDesc.mapped = true;
    quadVertexBuffer_ = backend_->createBuffer(vbDesc);
    backend_->updateBuffer(quadVertexBuffer_, quadVertices, sizeof(quadVertices));
    
    // 索引缓冲
    u16 indices[] = {0, 1, 2, 0, 2, 3};
    BufferDesc ibDesc;
    ibDesc.size = sizeof(indices);
    ibDesc.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    ibDesc.mapped = true;
    quadIndexBuffer_ = backend_->createBuffer(ibDesc);
    backend_->updateBuffer(quadIndexBuffer_, indices, sizeof(indices));
    
    printf("Pipelines created\n");
    return true;
}

bool LiquidGlassRenderer::createSampler() {
    SamplerDesc desc;
    desc.minFilter = VK_FILTER_LINEAR;
    desc.magFilter = VK_FILTER_LINEAR;
    desc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.maxAnisotropy = 1.0f;
    
    linearSampler_ = backend_->createSampler(desc);
    
    desc.minFilter = VK_FILTER_NEAREST;
    desc.magFilter = VK_FILTER_NEAREST;
    pointSampler_ = backend_->createSampler(desc);
    
    return linearSampler_.valid();
}

void LiquidGlassRenderer::destroyTextures() {
    if (!backend_) return;
    
    if (backgroundTexture_.valid()) backend_->destroyTexture(backgroundTexture_);
    if (blurredTexture_.valid()) backend_->destroyTexture(blurredTexture_);
    if (tempBlurTexture_.valid()) backend_->destroyTexture(tempBlurTexture_);
    if (outputTexture_.valid()) backend_->destroyTexture(outputTexture_);
    if (noiseTexture_.valid()) backend_->destroyTexture(noiseTexture_);
    
    backgroundTexture_ = {};
    blurredTexture_ = {};
    tempBlurTexture_ = {};
    outputTexture_ = {};
    noiseTexture_ = {};
}

void LiquidGlassRenderer::destroyShaders() {
    if (!backend_) return;
    
    if (blurShader_.valid()) backend_->destroyShader(blurShader_);
    if (glassShader_.valid()) backend_->destroyShader(glassShader_);
    if (noiseShader_.valid()) backend_->destroyShader(noiseShader_);
    
    blurShader_ = {};
    glassShader_ = {};
    noiseShader_ = {};
}

void LiquidGlassRenderer::destroyPipelines() {
    if (!backend_) return;
    
    if (blurPipeline_.valid()) backend_->destroyPipeline(blurPipeline_);
    if (glassPipeline_.valid()) backend_->destroyPipeline(glassPipeline_);
    if (noisePipeline_.valid()) backend_->destroyPipeline(noisePipeline_);
    if (quadVertexBuffer_.valid()) backend_->destroyBuffer(quadVertexBuffer_);
    if (quadIndexBuffer_.valid()) backend_->destroyBuffer(quadIndexBuffer_);
    
    blurPipeline_ = {};
    glassPipeline_ = {};
    noisePipeline_ = {};
    quadVertexBuffer_ = {};
    quadIndexBuffer_ = {};
}

// ============================================================================
// 渲染流程
// ============================================================================

void LiquidGlassRenderer::beginBackgroundCapture(VkCommandBuffer cmd) {
    // 转换背景纹理到颜色附件布局
    backend_->imageBarrier(backgroundTexture_, 
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
}

void LiquidGlassRenderer::endBackgroundCapture(VkCommandBuffer cmd) {
    // 转换到着色器读取布局
    backend_->imageBarrier(backgroundTexture_,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT);
}

void LiquidGlassRenderer::applyBlur(VkCommandBuffer cmd) {
    if (!params_.enableBlur) return;
    
    // 高斯模糊 - 多 pass
    TextureHandle input = backgroundTexture_;
    TextureHandle output = blurredTexture_;
    
    for (u32 pass = 0; pass < params_.blurPasses; pass++) {
        // 水平模糊: input -> temp
        backend_->imageBarrier(input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT);
        
        backend_->imageBarrier(tempBlurTexture_, VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, VK_ACCESS_SHADER_WRITE_BIT);
        
        backend_->setComputePipeline(blurPipeline_);
        
        // Push constants
        struct BlurPC {
            f32 dirX, dirY;
            f32 resX, resY;
            f32 sigma;
            i32 radius;
            f32 pad[2];
        } blurPC;
        blurPC.dirX = 1.0f;
        blurPC.dirY = 0.0f;
        blurPC.resX = (f32)width_;
        blurPC.resY = (f32)height_;
        blurPC.sigma = params_.blurSigma;
        blurPC.radius = (i32)params_.blurRadius;
        
        backend_->pushConstants(blurShader_, 0, sizeof(blurPC), &blurPC);
        backend_->dispatchCompute((width_ + 15) / 16, (height_ + 15) / 16, 1);
        
        // 垂直模糊: temp -> output
        backend_->imageBarrier(tempBlurTexture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT);
        
        backend_->imageBarrier(output, VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, VK_ACCESS_SHADER_WRITE_BIT);
        
        blurPC.dirX = 0.0f;
        blurPC.dirY = 1.0f;
        
        backend_->pushConstants(blurShader_, 0, sizeof(blurPC), &blurPC);
        backend_->dispatchCompute((width_ + 15) / 16, (height_ + 15) / 16, 1);
        
        // 下一 pass 使用上一次输出作为输入
        input = output;
    }
    
    // 最终转换到着色器读取
    backend_->imageBarrier(output, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT);
}

void LiquidGlassRenderer::applyRefraction(VkCommandBuffer cmd, f32 time) {
    // 生成噪声纹理
    if (noisePipeline_.valid()) {
        backend_->imageBarrier(noiseTexture_, VK_IMAGE_LAYOUT_GENERAL,
                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               0, VK_ACCESS_SHADER_WRITE_BIT);
        
        backend_->setComputePipeline(noisePipeline_);
        
        struct NoisePC {
            f32 resX, resY;
            f32 scale;
            f32 time;
        } noisePC;
        noisePC.resX = 256.0f;
        noisePC.resY = 256.0f;
        noisePC.scale = params_.refractionScale;
        noisePC.time = time;
        
        backend_->pushConstants(noiseShader_, 0, sizeof(noisePC), &noisePC);
        backend_->dispatchCompute(16, 16, 1);
        
        backend_->imageBarrier(noiseTexture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_ACCESS_SHADER_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT);
    }
}

void LiquidGlassRenderer::renderGlass(VkCommandBuffer cmd, f32 time, const Rect& screenRect) {
    // 转换纹理布局
    backend_->imageBarrier(backgroundTexture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0, VK_ACCESS_SHADER_READ_BIT);
    backend_->imageBarrier(blurredTexture_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           0, VK_ACCESS_SHADER_READ_BIT);
    
    // 设置管线和缓冲
    backend_->setPipeline(glassPipeline_);
    backend_->setVertexBuffer(quadVertexBuffer_, 0);
    backend_->setIndexBuffer(quadIndexBuffer_);
    
    // 构建变换矩阵
    Mat4 transform = Mat4::identity();
    transform.m[0] = screenRect.width;
    transform.m[5] = screenRect.height;
    transform.m[12] = screenRect.x;
    transform.m[13] = screenRect.y;
    
    // 应用正交投影
    Mat4 projection = Mat4::ortho(0, (f32)width_, (f32)height_, 0, -1, 1);
    Mat4 mvp = projection * transform;
    
    // Push constants - 与着色器结构完全匹配
    struct GlassPC {
        f32 transform[16];
        f32 rectParams[4];
        f32 glassParams[4];
        f32 colorParams[4];
        f32 advParams[4];
        f32 spotParams[4];
        f32 spotParams2[4];
        f32 borderColor[4];
        f32 flags[4];
    } pc;
    
    memcpy(pc.transform, mvp.m, sizeof(pc.transform));
    
    pc.rectParams[0] = screenRect.x;
    pc.rectParams[1] = screenRect.y;
    pc.rectParams[2] = screenRect.width;
    pc.rectParams[3] = screenRect.height;
    
    pc.glassParams[0] = params_.cornerRadius;
    pc.glassParams[1] = params_.refractionStrength;
    pc.glassParams[2] = params_.refractionScale;
    pc.glassParams[3] = time * params_.refractionSpeed;
    
    pc.colorParams[0] = params_.tintColor.r;
    pc.colorParams[1] = params_.tintColor.g;
    pc.colorParams[2] = params_.tintColor.b;
    pc.colorParams[3] = params_.tintStrength;
    
    pc.advParams[0] = params_.brightness;
    pc.advParams[1] = params_.saturation;
    pc.advParams[2] = params_.contrast;
    pc.advParams[3] = params_.opacity;
    
    pc.spotParams[0] = params_.spotIntensity;
    pc.spotParams[1] = params_.spotRadius;
    pc.spotParams[2] = params_.spotSpeed;
    pc.spotParams[3] = params_.spotPosition.x;
    
    pc.spotParams2[0] = params_.spotPosition.y;
    pc.spotParams2[1] = params_.borderGlow;
    pc.spotParams2[2] = params_.borderGlowSpeed;
    pc.spotParams2[3] = params_.borderWidth;
    
    pc.borderColor[0] = params_.borderColor.r;
    pc.borderColor[1] = params_.borderColor.g;
    pc.borderColor[2] = params_.borderColor.b;
    pc.borderColor[3] = params_.borderColor.a;
    
    pc.flags[0] = params_.enableRefraction ? 1.0f : 0.0f;
    pc.flags[1] = params_.enableBlur ? 1.0f : 0.0f;
    pc.flags[2] = params_.enableSpot ? 1.0f : 0.0f;
    pc.flags[3] = params_.enableBorder ? 1.0f : 0.0f;
    
    backend_->pushConstants(glassShader_, 0, sizeof(pc), &pc);
    
    // 绘制
    backend_->drawIndexed(6);
}

void LiquidGlassRenderer::composite(VkCommandBuffer cmd, TextureHandle output) {
    // 输出已经在 renderGlass 中直接渲染到屏幕
}

void LiquidGlassRenderer::render(VkCommandBuffer cmd, TextureHandle background, 
                                  TextureHandle output, f32 time, const Rect& screenRect) {
    // 1. 复制背景到内部纹理
    // TODO: 实现 texture copy
    
    // 2. 应用模糊
    applyBlur(cmd);
    
    // 3. 应用折射
    if (params_.enableRefraction) {
        applyRefraction(cmd, time);
    }
    
    // 4. 渲染玻璃层
    renderGlass(cmd, time, screenRect);
}

void LiquidGlassRenderer::imageBarrier(TextureHandle texture, VkImageLayout newLayout,
                                        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                        VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    if (backend_ && texture.valid()) {
        backend_->imageBarrier(texture, newLayout, srcStage, dstStage, srcAccess, dstAccess);
    }
}

} // namespace Nova

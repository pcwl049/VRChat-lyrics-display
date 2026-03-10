/**
 * Nova Renderer - GPU Particle System Implementation
 */

#include "Nova/ParticleSystem.h"
#include "Nova/VulkanBackend.h"
#include <cmath>
#include <random>

namespace Nova {

// ============================================================================
// 粒子发射器实现
// ============================================================================

ParticleEmitter::ParticleEmitter() = default;

ParticleEmitter::~ParticleEmitter() {
    shutdown();
}

bool ParticleEmitter::initialize(VulkanBackend* backend, u32 maxParticles) {
    backend_ = backend;
    maxParticles_ = maxParticles;
    
    // 创建粒子数据缓冲
    BufferDesc particleDesc;
    particleDesc.size = sizeof(ParticleData) * maxParticles;
    particleDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    particleDesc.mapped = false;
    
    particleBuffer_ = backend_->createBuffer(particleDesc);
    if (!particleBuffer_.valid()) return false;
    
    // 创建活跃粒子列表
    BufferDesc listDesc;
    listDesc.size = sizeof(u32) * maxParticles;
    listDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    listDesc.mapped = false;
    
    aliveList_ = backend_->createBuffer(listDesc);
    deadList_ = backend_->createBuffer(listDesc);
    
    // 创建计数器缓冲
    BufferDesc counterDesc;
    counterDesc.size = sizeof(u32) * 4;  // 活跃数、死亡数、发射数、保留
    counterDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    counterDesc.mapped = true;
    
    counterBuffer_ = backend_->createBuffer(counterDesc);
    
    // 初始化计数器
    u32 counters[4] = {0, maxParticles, 0, 0};
    backend_->updateBuffer(counterBuffer_, counters, sizeof(counters));
    
    return true;
}

void ParticleEmitter::shutdown() {
    if (backend_) {
        if (particleBuffer_.valid()) backend_->destroyBuffer(particleBuffer_);
        if (aliveList_.valid()) backend_->destroyBuffer(aliveList_);
        if (deadList_.valid()) backend_->destroyBuffer(deadList_);
        if (counterBuffer_.valid()) backend_->destroyBuffer(counterBuffer_);
    }
    backend_ = nullptr;
}

void ParticleEmitter::setParams(const ParticleEmitterParams& params) {
    params_ = params;
    if (params.maxParticles > 0 && params.maxParticles != maxParticles_) {
        // 需要重新创建缓冲
        shutdown();
        initialize(backend_, params.maxParticles);
    }
}

void ParticleEmitter::emit(u32 count) {
    emitCount_ += count;
}

// ============================================================================
// 粒子系统实现
// ============================================================================

ParticleSystem::ParticleSystem() = default;

ParticleSystem::~ParticleSystem() {
    shutdown();
}

bool ParticleSystem::initialize(VulkanBackend* backend, u32 maxParticles) {
    backend_ = backend;
    maxParticles_ = maxParticles;
    
    if (!createBuffers()) return false;
    if (!createPipelines()) return false;
    
    return true;
}

void ParticleSystem::shutdown() {
    emitters_.clear();
    
    if (backend_) {
        if (particleBuffer_.valid()) backend_->destroyBuffer(particleBuffer_);
        if (indirectBuffer_.valid()) backend_->destroyBuffer(indirectBuffer_);
        if (counterBuffer_.valid()) backend_->destroyBuffer(counterBuffer_);
        if (updatePipeline_.valid()) backend_->destroyPipeline(updatePipeline_);
        if (emitPipeline_.valid()) backend_->destroyPipeline(emitPipeline_);
        if (renderPipeline_.valid()) backend_->destroyPipeline(renderPipeline_);
        if (computeShader_.valid()) backend_->destroyShader(computeShader_);
        if (renderShader_.valid()) backend_->destroyShader(renderShader_);
        if (particleTexture_.valid()) backend_->destroyTexture(particleTexture_);
        if (particleSampler_.valid()) backend_->destroySampler(particleSampler_);
    }
    
    backend_ = nullptr;
}

bool ParticleSystem::createBuffers() {
    // 粒子数据缓冲
    BufferDesc particleDesc;
    particleDesc.size = sizeof(ParticleData) * maxParticles_;
    particleDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                          VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    particleBuffer_ = backend_->createBuffer(particleDesc);
    if (!particleBuffer_.valid()) return false;
    
    // 间接绘制参数
    struct IndirectDrawArgs {
        u32 vertexCount;
        u32 instanceCount;
        u32 firstVertex;
        u32 firstInstance;
    };
    
    BufferDesc indirectDesc;
    indirectDesc.size = sizeof(IndirectDrawArgs);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | 
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    indirectDesc.mapped = true;
    
    indirectBuffer_ = backend_->createBuffer(indirectDesc);
    if (!indirectBuffer_.valid()) return false;
    
    // 初始化间接绘制参数
    IndirectDrawArgs args = {6, 0, 0, 0};  // 6 个顶点 (四边形)
    backend_->updateBuffer(indirectBuffer_, &args, sizeof(args));
    
    // 计数器缓冲
    BufferDesc counterDesc;
    counterDesc.size = sizeof(u32) * 4;
    counterDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    counterDesc.mapped = true;
    
    counterBuffer_ = backend_->createBuffer(counterDesc);
    
    return true;
}

bool ParticleSystem::createPipelines() {
    // 粒子更新计算着色器
    const char* updateComputeShader = R"(
#version 450

layout(local_size_x = 256) in;

struct Particle {
    vec4 position;     // xyz = position, w = size
    vec4 velocity;     // xyz = velocity, w = life
    vec4 color;        // rgba
    vec4 properties;   // x = age, y = lifetime, z = rotation, w = reserved
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 1) buffer CounterBuffer {
    uint activeCount;
    uint deadCount;
    uint emitCount;
    uint padding;
};

layout(push_constant) uniform Params {
    float deltaTime;
    float time;
    uint maxParticles;
    uint padding2;
    vec4 gravity;
} params;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= params.maxParticles) return;
    
    Particle p = particles[index];
    
    // 检查粒子是否存活
    if (p.properties.x > 0.0 && p.properties.x < p.properties.y) {
        // 更新年龄
        p.properties.x += params.deltaTime;
        
        // 更新速度 (重力)
        p.velocity.xyz += params.gravity.xyz * params.deltaTime;
        
        // 更新位置
        p.position.xyz += p.velocity.xyz * params.deltaTime;
        
        // 更新颜色 (根据年龄淡出)
        float life = 1.0 - (p.properties.x / p.properties.y);
        p.color.a *= life;
        
        // 更新旋转
        p.properties.z += p.velocity.w * params.deltaTime;
        
        particles[index] = p;
    }
}
)";
    
    ShaderDesc shaderDesc;
    shaderDesc.computeSource = updateComputeShader;
    computeShader_ = backend_->createShader(shaderDesc);
    
    // 创建计算管线
    PipelineDesc pipelineDesc;
    pipelineDesc.shader = computeShader_;
    
    updatePipeline_ = backend_->createPipeline(pipelineDesc);
    
    // 粒子渲染着色器
    const char* particleVertexShader = R"(
#version 450

struct Particle {
    vec4 position;
    vec4 velocity;
    vec4 color;
    vec4 properties;
};

layout(std430, binding = 0) readonly buffer ParticleBuffer {
    Particle particles[];
};

layout(push_constant) uniform Transform {
    mat4 viewProj;
    vec2 screenSize;
} transform;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

void main() {
    Particle p = particles[gl_InstanceID];
    
    // 检查是否存活
    if (p.properties.x <= 0.0 || p.properties.x >= p.properties.y) {
        gl_Position = vec4(-2.0, -2.0, 0.0, 1.0);
        return;
    }
    
    // 四边形顶点
    vec2 corners[6] = vec2[](
        vec2(-0.5, -0.5),
        vec2(0.5, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5)
    );
    
    vec2 corner = corners[gl_VertexIndex];
    
    // 应用旋转
    float c = cos(p.properties.z);
    float s = sin(p.properties.z);
    vec2 rotated = vec2(
        corner.x * c - corner.y * s,
        corner.x * s + corner.y * c
    );
    
    // 应用大小
    vec2 screenPos = p.position.xy + rotated * p.position.w;
    
    gl_Position = transform.viewProj * vec4(screenPos, p.position.z, 1.0);
    outTexCoord = corner + 0.5;
    outColor = p.color;
}
)";

    const char* particleFragmentShader = R"(
#version 450

layout(binding = 1) uniform sampler2D particleTex;

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(particleTex, inTexCoord);
    
    // 圆形粒子
    vec2 center = inTexCoord - 0.5;
    float dist = length(center) * 2.0;
    float alpha = 1.0 - smoothstep(0.8, 1.0, dist);
    
    outColor = texColor * inColor;
    outColor.a *= alpha;
}
)";
    
    shaderDesc.vertexSource = particleVertexShader;
    shaderDesc.fragmentSource = particleFragmentShader;
    shaderDesc.computeSource = nullptr;
    
    renderShader_ = backend_->createShader(shaderDesc);
    
    pipelineDesc.shader = renderShader_;
    pipelineDesc.blendMode = 2;  // 加法混合
    
    renderPipeline_ = backend_->createPipeline(pipelineDesc);
    
    return true;
}

void ParticleSystem::update(VkCommandBuffer cmd, f32 deltaTime) {
    globalParams_.deltaTime = deltaTime;
    
    // 更新所有发射器
    totalParticles_ = 0;
    for (auto& emitter : emitters_) {
        totalParticles_ += emitter->getParticleCount();
    }
    
    // 派发计算着色器
    u32 groupCount = (maxParticles_ + 255) / 256;
    
    backend_->setComputePipeline(updatePipeline_);
    backend_->dispatchCompute(groupCount, 1, 1);
    
    // 内存屏障
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    
    vkCmdPipelineBarrier(cmd, 
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void ParticleSystem::render(VkCommandBuffer cmd) {
    if (totalParticles_ == 0) return;
    
    backend_->setPipeline(renderPipeline_);
    
    // 直接绘制（间接绘制需要获取原始 VkBuffer）
    // TODO: 实现 getVkBuffer 方法后改为 vkCmdDrawIndirect
    vkCmdDraw(cmd, 6, totalParticles_, 0, 0);
}

u32 ParticleSystem::addEmitter(const ParticleEmitterParams& params) {
    auto emitter = std::make_unique<ParticleEmitter>();
    
    if (!emitter->initialize(backend_, static_cast<u32>(params.maxParticles))) {
        return UINT32_MAX;
    }
    
    emitter->setParams(params);
    
    u32 index = static_cast<u32>(emitters_.size());
    emitters_.push_back(std::move(emitter));
    
    return index;
}

void ParticleSystem::removeEmitter(u32 index) {
    if (index < emitters_.size()) {
        emitters_.erase(emitters_.begin() + index);
    }
}

ParticleEmitter* ParticleSystem::getEmitter(u32 index) {
    if (index < emitters_.size()) {
        return emitters_[index].get();
    }
    return nullptr;
}

void ParticleSystem::setGlobalParams(const ParticleSystemParams& params) {
    globalParams_ = params;
}

bool ParticleSystem::loadShaders() {
    return true;
}

// ============================================================================
// 粒子效果预设实现
// ============================================================================

namespace ParticlePresets {

ParticleEmitterParams createFire(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.1f);
    params.velocity = Vec4(0.0f, 2.0f, 0.0f, 0.5f);
    params.color = Vec4(1.0f, 0.5f, 0.1f, 1.0f);
    params.colorVariation = Vec4(0.2f, 0.3f, 0.1f, 0.0f);
    params.size = 8.0f;
    params.sizeVariation = 4.0f;
    params.lifetime = 1.0f;
    params.lifetimeVariation = 0.5f;
    params.gravity = -0.5f;
    params.drag = 0.1f;
    params.emissionRate = 100.0f;
    params.maxParticles = 1000;
    params.shape = ParticleEmitterParams::Shape::Cone;
    params.shapeParams = Vec4(0.0f, 1.0f, 0.0f, 0.3f);
    return params;
}

ParticleEmitterParams createSmoke(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.2f);
    params.velocity = Vec4(0.0f, 1.0f, 0.0f, 0.3f);
    params.color = Vec4(0.5f, 0.5f, 0.5f, 0.8f);
    params.colorVariation = Vec4(0.1f, 0.1f, 0.1f, 0.2f);
    params.size = 16.0f;
    params.sizeVariation = 8.0f;
    params.lifetime = 3.0f;
    params.lifetimeVariation = 1.0f;
    params.gravity = -0.1f;
    params.drag = 0.05f;
    params.emissionRate = 30.0f;
    params.maxParticles = 500;
    params.shape = ParticleEmitterParams::Shape::Circle;
    return params;
}

ParticleEmitterParams createExplosion(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.0f);
    params.velocity = Vec4(0.0f, 0.0f, 0.0f, 10.0f);
    params.color = Vec4(1.0f, 0.8f, 0.2f, 1.0f);
    params.colorVariation = Vec4(0.2f, 0.2f, 0.1f, 0.0f);
    params.size = 12.0f;
    params.sizeVariation = 6.0f;
    params.lifetime = 0.8f;
    params.lifetimeVariation = 0.3f;
    params.gravity = 2.0f;
    params.drag = 0.3f;
    params.emissionRate = 0.0f;  // 一次性发射
    params.maxParticles = 200;
    params.shape = ParticleEmitterParams::Shape::Sphere;
    return params;
}

ParticleEmitterParams createFirework(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.0f);
    params.velocity = Vec4(0.0f, 5.0f, 0.0f, 8.0f);
    params.color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
    params.colorVariation = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
    params.size = 4.0f;
    params.sizeVariation = 2.0f;
    params.lifetime = 2.0f;
    params.lifetimeVariation = 0.5f;
    params.gravity = 1.0f;
    params.drag = 0.1f;
    params.emissionRate = 0.0f;
    params.maxParticles = 300;
    params.shape = ParticleEmitterParams::Shape::Sphere;
    return params;
}

ParticleEmitterParams createRain(const Vec3& position, f32 area) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, area);
    params.velocity = Vec4(0.0f, -15.0f, 0.0f, 2.0f);
    params.color = Vec4(0.7f, 0.8f, 1.0f, 0.6f);
    params.colorVariation = Vec4(0.1f, 0.1f, 0.1f, 0.1f);
    params.size = 2.0f;
    params.sizeVariation = 1.0f;
    params.lifetime = 2.0f;
    params.lifetimeVariation = 0.5f;
    params.gravity = 0.0f;
    params.drag = 0.0f;
    params.emissionRate = 500.0f;
    params.maxParticles = 2000;
    params.shape = ParticleEmitterParams::Shape::Box;
    params.shapeParams = Vec4(area, 0.1f, area, 0.0f);
    return params;
}

ParticleEmitterParams createSnow(const Vec3& position, f32 area) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, area);
    params.velocity = Vec4(0.0f, -1.0f, 0.0f, 0.5f);
    params.color = Vec4(1.0f, 1.0f, 1.0f, 0.9f);
    params.colorVariation = Vec4(0.05f, 0.05f, 0.05f, 0.1f);
    params.size = 6.0f;
    params.sizeVariation = 4.0f;
    params.lifetime = 10.0f;
    params.lifetimeVariation = 2.0f;
    params.gravity = 0.0f;
    params.drag = 0.02f;
    params.emissionRate = 100.0f;
    params.maxParticles = 1000;
    params.shape = ParticleEmitterParams::Shape::Box;
    params.shapeParams = Vec4(area, 0.1f, area, 0.0f);
    return params;
}

ParticleEmitterParams createSparks(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.05f);
    params.velocity = Vec4(0.0f, 5.0f, 0.0f, 3.0f);
    params.color = Vec4(1.0f, 0.9f, 0.5f, 1.0f);
    params.colorVariation = Vec4(0.1f, 0.2f, 0.2f, 0.0f);
    params.size = 3.0f;
    params.sizeVariation = 2.0f;
    params.lifetime = 0.5f;
    params.lifetimeVariation = 0.3f;
    params.gravity = 3.0f;
    params.drag = 0.5f;
    params.emissionRate = 200.0f;
    params.maxParticles = 500;
    params.shape = ParticleEmitterParams::Shape::Cone;
    return params;
}

ParticleEmitterParams createMagic(const Vec3& position) {
    ParticleEmitterParams params;
    params.position = Vec4(position.x, position.y, position.z, 0.2f);
    params.velocity = Vec4(0.0f, 0.0f, 0.0f, 2.0f);
    params.color = Vec4(0.5f, 0.2f, 1.0f, 1.0f);
    params.colorVariation = Vec4(0.3f, 0.2f, 0.2f, 0.0f);
    params.size = 5.0f;
    params.sizeVariation = 3.0f;
    params.lifetime = 1.5f;
    params.lifetimeVariation = 0.5f;
    params.gravity = 0.0f;
    params.drag = 0.02f;
    params.emissionRate = 50.0f;
    params.maxParticles = 200;
    params.shape = ParticleEmitterParams::Shape::Sphere;
    return params;
}

} // namespace ParticlePresets

// ============================================================================
// 粒子效果实例实现
// ============================================================================

ParticleEffect::ParticleEffect() = default;

ParticleEffect::~ParticleEffect() {
    if (system_ && emitterIndex_ != UINT32_MAX) {
        system_->removeEmitter(emitterIndex_);
    }
}

bool ParticleEffect::initialize(ParticleSystem* system, const ParticleEmitterParams& params) {
    system_ = system;
    emitterIndex_ = system->addEmitter(params);
    return emitterIndex_ != UINT32_MAX;
}

void ParticleEffect::update(f32 deltaTime) {
    if (!playing_) return;
    
    elapsed_ += deltaTime;
    
    if (duration_ > 0 && elapsed_ >= duration_) {
        stop();
    }
}

void ParticleEffect::play() {
    playing_ = true;
    finished_ = false;
    elapsed_ = 0.0f;
}

void ParticleEffect::stop() {
    playing_ = false;
    finished_ = true;
}

void ParticleEffect::setPosition(const Vec3& pos) {
    if (auto* emitter = system_->getEmitter(emitterIndex_)) {
        auto params = emitter->getParams();
        params.position = Vec4(pos.x, pos.y, pos.z, params.position.w);
        emitter->setParams(params);
    }
}

void ParticleEffect::setDuration(f32 duration) {
    duration_ = duration;
}

} // namespace Nova

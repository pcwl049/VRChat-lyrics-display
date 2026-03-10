/**
 * Nova Renderer - GPU Particle System
 * GPU 粒子系统 - 使用计算着色器进行粒子模拟
 */

#pragma once

#include "Types.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <functional>

namespace Nova {

class VulkanBackend;

// ============================================================================
// 粒子数据结构
// ============================================================================

// GPU 粒子数据 (对齐到 16 字节)
struct alignas(16) ParticleData {
    Vec4 position;     // xyz = position, w = size
    Vec4 velocity;     // xyz = velocity, w = life
    Vec4 color;        // rgba
    Vec4 properties;   // x = age, y = lifetime, z = rotation, w = reserved
};

// 粒子发射参数
struct alignas(16) ParticleEmitterParams {
    Vec4 position;          // xyz = 发射位置, w = 发射半径
    Vec4 velocity;          // xyz = 初始速度, w = 速度随机范围
    Vec4 color;             // rgba = 基础颜色
    Vec4 colorVariation;    // rgba = 颜色变化范围
    
    f32 size;               // 粒子大小
    f32 sizeVariation;      // 大小变化
    f32 lifetime;           // 生命周期
    f32 lifetimeVariation;  // 生命周期变化
    
    f32 gravity;            // 重力
    f32 drag;               // 阻力
    f32 emissionRate;       // 发射率 (每秒)
    f32 maxParticles;       // 最大粒子数
    
    // 发射形状
    enum class Shape : u32 {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Cone = 3,
        Circle = 4
    } shape = Shape::Point;
    
    Vec4 shapeParams;       // 发射形状参数
    
    u32 seed;               // 随机种子
    u32 padding[3];
};

// 粒子系统全局参数
struct alignas(16) ParticleSystemParams {
    f32 deltaTime;
    f32 time;
    u32 activeParticles;
    u32 maxParticles;
    
    Vec4 gravity;           // 全局重力方向
    Vec4 wind;              // 风力
    
    f32 globalDrag;         // 全局阻力
    f32 turbulenceStrength; // 湍流强度
    f32 turbulenceScale;    // 湍流缩放
    f32 attractorStrength;  // 吸引力强度
    
    Vec4 attractorPosition; // 吸引点位置
};

// ============================================================================
// 粒子发射器
// ============================================================================

class ParticleEmitter {
public:
    ParticleEmitter();
    ~ParticleEmitter();
    
    // 初始化
    bool initialize(VulkanBackend* backend, u32 maxParticles);
    void shutdown();
    
    // 更新参数
    void setParams(const ParticleEmitterParams& params);
    const ParticleEmitterParams& getParams() const { return params_; }
    
    // 发射粒子
    void emit(u32 count);
    
    // 资源句柄
    BufferHandle getParticleBuffer() const { return particleBuffer_; }
    u32 getParticleCount() const { return activeParticles_; }
    u32 getMaxParticles() const { return maxParticles_; }
    
private:
    VulkanBackend* backend_ = nullptr;
    ParticleEmitterParams params_;
    
    BufferHandle particleBuffer_;
    BufferHandle aliveList_;
    BufferHandle deadList_;
    BufferHandle counterBuffer_;
    
    u32 maxParticles_ = 10000;
    u32 activeParticles_ = 0;
    u32 emitCount_ = 0;  // 这一帧要发射的粒子数
};

// ============================================================================
// 粒子系统
// ============================================================================

class ParticleSystem {
public:
    ParticleSystem();
    ~ParticleSystem();
    
    // 初始化
    bool initialize(VulkanBackend* backend, u32 maxParticles = 100000);
    void shutdown();
    
    // 每帧更新
    void update(VkCommandBuffer cmd, f32 deltaTime);
    
    // 渲染
    void render(VkCommandBuffer cmd);
    
    // 发射器管理
    u32 addEmitter(const ParticleEmitterParams& params);
    void removeEmitter(u32 index);
    ParticleEmitter* getEmitter(u32 index);
    
    // 参数
    void setGlobalParams(const ParticleSystemParams& params);
    
    // 资源
    BufferHandle getParticleBuffer() const { return particleBuffer_; }
    u32 getTotalParticles() const { return totalParticles_; }
    
    // 着色器
    bool loadShaders();
    
private:
    bool createBuffers();
    bool createPipelines();
    
    VulkanBackend* backend_ = nullptr;
    
    // 全局资源
    BufferHandle particleBuffer_;      // 粒子数据
    BufferHandle indirectBuffer_;      // 间接绘制参数
    BufferHandle counterBuffer_;       // 原子计数器
    
    // 发射器
    std::vector<std::unique_ptr<ParticleEmitter>> emitters_;
    
    // 管线
    PipelineHandle updatePipeline_;    // 粒子更新计算管线
    PipelineHandle emitPipeline_;      // 粒子发射计算管线
    PipelineHandle renderPipeline_;    // 粒子渲染图形管线
    PipelineHandle cullPipeline_;      // 粒子剔除计算管线
    
    ShaderHandle computeShader_;
    ShaderHandle renderShader_;
    
    // 参数
    ParticleSystemParams globalParams_;
    u32 maxParticles_ = 100000;
    u32 totalParticles_ = 0;
    
    // 纹理
    TextureHandle particleTexture_;
    SamplerHandle particleSampler_;
};

// ============================================================================
// 粒子效果预设
// ============================================================================

namespace ParticlePresets {
    
    // 火焰
    ParticleEmitterParams createFire(const Vec3& position);
    
    // 烟雾
    ParticleEmitterParams createSmoke(const Vec3& position);
    
    // 爆炸
    ParticleEmitterParams createExplosion(const Vec3& position);
    
    // 烟花
    ParticleEmitterParams createFirework(const Vec3& position);
    
    // 雨
    ParticleEmitterParams createRain(const Vec3& position, f32 area);
    
    // 雪
    ParticleEmitterParams createSnow(const Vec3& position, f32 area);
    
    // 星星/火花
    ParticleEmitterParams createSparks(const Vec3& position);
    
    // 魔法效果
    ParticleEmitterParams createMagic(const Vec3& position);
    
}

// ============================================================================
// 粒子效果实例 (用于场景中的一次性效果)
// ============================================================================

class ParticleEffect {
public:
    ParticleEffect();
    ~ParticleEffect();
    
    bool initialize(ParticleSystem* system, const ParticleEmitterParams& params);
    void update(f32 deltaTime);
    
    void play();
    void stop();
    bool isPlaying() const { return playing_; }
    bool isFinished() const { return finished_; }
    
    void setPosition(const Vec3& pos);
    void setDuration(f32 duration);
    
private:
    ParticleSystem* system_ = nullptr;
    u32 emitterIndex_ = UINT32_MAX;
    
    bool playing_ = false;
    bool finished_ = false;
    f32 duration_ = -1.0f;  // -1 = 无限
    f32 elapsed_ = 0.0f;
};

} // namespace Nova

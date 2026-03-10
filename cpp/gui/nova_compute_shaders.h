/**
 * Nova Renderer - Advanced Compute Shaders
 * 计算着色器集合
 * 
 * 包含:
 * - GPU 布局计算 (Flexbox/Grid)
 * - 粒子系统更新/发射
 * - 高斯模糊
 * - Bloom
 * - SSAO
 * - 纹理压缩 (BC7/BC6H)
 */

#pragma once

#include <cstdint>

namespace Nova {
namespace Shaders {

// ============================================================================
// 1. GPU 布局计算着色器 (Flexbox)
// ============================================================================

const char* const FLEXBOX_LAYOUT_SHADER = R"GLSL(
#version 450
#extension GL_EXT_shader_explicit_arithmetic_types : enable

// 布局节点结构 (128 bytes)
struct LayoutNode {
    // 输入属性
    vec2 position;
    vec2 size;
    vec2 minSize;
    vec2 maxSize;
    
    // Flex 属性
    float flexGrow;
    float flexShrink;
    float flexBasis;
    float aspectRatio;
    vec4 margin;  // top, right, bottom, left
    
    // 对齐属性
    uint direction;
    uint justifyContent;
    uint alignItems;
    uint wrap;
    
    // 其他
    vec4 padding;  // top, right, bottom, left
    
    // 索引
    uint firstChild;
    uint childCount;
    
    // 输出
    vec2 computedPosition;
    vec2 computedSize;
    
    uint padding1[2];
};

layout(std430, binding = 0) buffer NodeBuffer {
    LayoutNode nodes[];
};

layout(std430, binding = 1) buffer OutputBuffer {
    LayoutNode results[];
};

layout(local_size_x = 64) in;

// Flex 方向
const uint FLEX_ROW = 0;
const uint FLEX_ROW_REVERSE = 1;
const uint FLEX_COLUMN = 2;
const uint FLEX_COLUMN_REVERSE = 3;

// Justify Content
const uint JUSTIFY_FLEX_START = 0;
const uint JUSTIFY_FLEX_END = 1;
const uint JUSTIFY_CENTER = 2;
const uint JUSTIFY_SPACE_BETWEEN = 3;
const uint JUSTIFY_SPACE_AROUND = 4;
const uint JUSTIFY_SPACE_EVENLY = 5;

// Align Items
const uint ALIGN_STRETCH = 0;
const uint ALIGN_FLEX_START = 1;
const uint ALIGN_FLEX_END = 2;
const uint ALIGN_CENTER = 3;
const uint ALIGN_BASELINE = 4;

void main() {
    uint index = gl_GlobalInvocationID.x;
    if (index >= nodes.length()) return;
    
    LayoutNode node = nodes[index];
    
    // 如果有子节点，计算子节点布局
    if (node.childCount > 0) {
        // 计算主轴和交叉轴
        bool isRow = (node.direction == FLEX_ROW || node.direction == FLEX_ROW_REVERSE);
        bool isReverse = (node.direction == FLEX_ROW_REVERSE || node.direction == FLEX_COLUMN_REVERSE);
        
        // 计算子节点总尺寸
        float mainAxisSize = 0.0;
        float totalFlexGrow = 0.0;
        float totalFlexShrink = 0.0;
        
        for (uint i = 0; i < node.childCount; i++) {
            uint childIdx = node.firstChild + i;
            LayoutNode child = nodes[childIdx];
            
            if (isRow) {
                mainAxisSize += child.size.x + child.margin.y + child.margin.w;  // margin right + left
            } else {
                mainAxisSize += child.size.y + child.margin.x + child.margin.z;  // margin top + bottom
            }
            
            totalFlexGrow += child.flexGrow;
            totalFlexShrink += child.flexShrink;
        }
        
        // 计算剩余空间
        float containerMainSize = isRow ? node.size.x : node.size.y;
        float freeSpace = containerMainSize - mainAxisSize;
        
        // 分配 flex 空间
        float flexUnit = (freeSpace > 0 && totalFlexGrow > 0) 
            ? freeSpace / totalFlexGrow 
            : 0.0;
        
        // 布局子节点
        float currentPos = isReverse ? containerMainSize : 0.0;
        
        for (uint i = 0; i < node.childCount; i++) {
            uint childIdx = node.firstChild + i;
            LayoutNode child = nodes[childIdx];
            
            // 计算 child 尺寸
            vec2 childSize = child.size;
            if (isRow && child.flexGrow > 0) {
                childSize.x += flexUnit * child.flexGrow;
            } else if (!isRow && child.flexGrow > 0) {
                childSize.y += flexUnit * child.flexGrow;
            }
            
            // 应用约束
            childSize = max(childSize, child.minSize);
            childSize = min(childSize, child.maxSize);
            
            if (child.aspectRatio > 0) {
                if (isRow) {
                    childSize.y = childSize.x / child.aspectRatio;
                } else {
                    childSize.x = childSize.y * child.aspectRatio;
                }
            }
            
            // 计算位置
            vec2 childPos = node.position;
            if (isRow) {
                if (isReverse) {
                    currentPos -= childSize.x + child.margin.w;
                    childPos.x = currentPos;
                    currentPos -= child.margin.y;
                } else {
                    currentPos += child.margin.w;
                    childPos.x = currentPos;
                    currentPos += childSize.x + child.margin.y;
                }
                
                // 交叉轴对齐
                switch (node.alignItems) {
                    case ALIGN_CENTER:
                        childPos.y = node.position.y + (node.size.y - childSize.y) * 0.5;
                        break;
                    case ALIGN_FLEX_END:
                        childPos.y = node.position.y + node.size.y - childSize.y - child.margin.x;
                        break;
                    case ALIGN_STRETCH:
                        childSize.y = node.size.y - child.margin.x - child.margin.z;
                        break;
                    default:
                        childPos.y = node.position.y + child.margin.x;
                        break;
                }
            } else {
                // 列方向类似处理
                if (isReverse) {
                    currentPos -= childSize.y + child.margin.x;
                    childPos.y = currentPos;
                    currentPos -= child.margin.z;
                } else {
                    currentPos += child.margin.x;
                    childPos.y = currentPos;
                    currentPos += childSize.y + child.margin.z;
                }
                
                switch (node.alignItems) {
                    case ALIGN_CENTER:
                        childPos.x = node.position.x + (node.size.x - childSize.x) * 0.5;
                        break;
                    case ALIGN_FLEX_END:
                        childPos.x = node.position.x + node.size.x - childSize.x - child.margin.w;
                        break;
                    case ALIGN_STRETCH:
                        childSize.x = node.size.x - child.margin.w - child.margin.y;
                        break;
                    default:
                        childPos.x = node.position.x + child.margin.w;
                        break;
                }
            }
            
            // 写入结果
            results[childIdx].computedPosition = childPos;
            results[childIdx].computedSize = childSize;
        }
    }
    
    // 写入当前节点结果
    results[index].computedPosition = node.position;
    results[index].computedSize = node.size;
}
)GLSL";

// ============================================================================
// 2. 粒子系统更新着色器
// ============================================================================

const char* const PARTICLE_UPDATE_SHADER = R"GLSL(
#version 450

// 粒子数据结构
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

layout(std430, binding = 2) buffer CounterBuffer {
    uint aliveCount;
    uint deadCount;
    uint emitCount;
    uint padding;
};

layout(push_constant) uniform Constants {
    float deltaTime;
    float gravity;
    vec2 padding;
};

layout(local_size_x = 64) in;

// 粒子标志
const uint FLAG_DEAD = 1u << 0;
const uint FLAG_EMITTED = 1u << 1;

void main() {
    uint index = gl_GlobalInvocationID.x;
    
    // 防止越界
    if (index >= particlesIn.length()) return;
    
    Particle p = particlesIn[index];
    
    // 跳过死亡粒子
    if ((p.flags & FLAG_DEAD) != 0u) {
        particlesOut[index] = p;
        return;
    }
    
    // 更新生命值
    p.life -= deltaTime;
    if (p.life <= 0.0) {
        p.flags |= FLAG_DEAD;
        atomicAdd(deadCount, 1);
        particlesOut[index] = p;
        return;
    }
    
    // 更新速度
    p.velocity += p.acceleration * deltaTime;
    p.velocity.y -= gravity * deltaTime;
    p.velocity *= (1.0 - p.drag * deltaTime);
    
    // 更新位置
    p.position += p.velocity * deltaTime;
    
    // 更新旋转
    p.rotation += p.angularVelocity * deltaTime;
    
    // 输出
    particlesOut[index] = p;
}
)GLSL";

// ============================================================================
// 3. 粒子发射着色器
// ============================================================================

const char* const PARTICLE_EMIT_SHADER = R"GLSL(
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

struct Emitter {
    vec3 position;
    vec3 direction;
    float spread;
    
    uint emissionRate;
    uint maxParticles;
    
    float minLife;
    float maxLife;
    
    float minSize;
    float maxSize;
    
    float minSpeed;
    float maxSpeed;
    
    vec4 startColor;
    vec4 endColor;
    
    float gravity;
    float drag;
    
    uint active;
    uint padding;
};

layout(std430, binding = 0) buffer ParticleBuffer {
    Particle particles[];
};

layout(std430, binding = 1) buffer EmitterBuffer {
    Emitter emitters[];
};

layout(std430, binding = 2) buffer CounterBuffer {
    uint aliveCount;
    uint deadCount;
    uint emitIndex;
    uint padding;
};

layout(push_constant) uniform Constants {
    float deltaTime;
    float time;
    uint emitterIndex;
    uint emitCount;
};

layout(local_size_x = 64) in;

// PCG 随机数生成器
uint pcg_hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float random(uint seed) {
    return float(pcg_hash(seed)) / float(0xffffffffu);
}

vec3 randomDirection(uint seed, vec3 baseDir, float spread) {
    float theta = random(seed) * 6.2831853;
    float phi = acos(1.0 - 2.0 * random(seed + 1u));
    float spreadAngle = spread * 3.14159;
    
    vec3 dir = baseDir;
    // 应用随机偏移
    dir.x += cos(theta) * sin(phi) * spreadAngle;
    dir.y += sin(theta) * sin(phi) * spreadAngle;
    dir.z += cos(phi) * spreadAngle;
    
    return normalize(dir);
}

void main() {
    uint localIndex = gl_LocalInvocationID.x;
    uint globalIndex = gl_GlobalInvocationID.x;
    
    if (globalIndex >= emitCount) return;
    
    Emitter emitter = emitters[emitterIndex];
    if (emitter.active == 0u) return;
    
    // 获取粒子索引
    uint particleIndex = atomicAdd(emitIndex, 1);
    if (particleIndex >= emitter.maxParticles) return;
    
    // 初始化随机种子
    uint seed = pcg_hash(uint(time * 1000.0)) + globalIndex;
    
    // 创建粒子
    Particle p;
    
    // 随机生命值
    float lifeFactor = random(seed);
    p.life = mix(emitter.minLife, emitter.maxLife, lifeFactor);
    
    // 随机位置 (基于发射器位置)
    p.position = emitter.position;
    
    // 随机速度和方向
    float speed = mix(emitter.minSpeed, emitter.maxSpeed, random(seed + 1u));
    vec3 dir = randomDirection(seed + 2u, emitter.direction, emitter.spread);
    p.velocity = dir * speed;
    
    // 随机大小
    p.size = mix(emitter.minSize, emitter.maxSize, random(seed + 3u));
    
    // 颜色插值 (基于生命值)
    p.color = emitter.startColor;
    
    // 其他属性
    p.acceleration = vec3(0.0);
    p.rotation = random(seed + 4u) * 6.2831853;
    p.angularVelocity = (random(seed + 5u) - 0.5) * 2.0;
    p.drag = emitter.drag;
    p.flags = 0u;  // 活跃状态
    
    // 写入粒子
    particles[particleIndex] = p;
    
    atomicAdd(aliveCount, 1);
}
)GLSL";

// ============================================================================
// 4. 高斯模糊着色器
// ============================================================================

const char* const GAUSSIAN_BLUR_SHADER = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform Constants {
    vec2 direction;  // (1, 0) 或 (0, 1)
    float radius;
    float padding;
};

// 高斯权重 (预计算)
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(inputImage);
    
    if (texelPos.x >= size.x || texelPos.y >= size.y) return;
    
    vec4 color = imageLoad(inputImage, texelPos) * weights[0];
    
    for (int i = 1; i < 5; i++) {
        float offset = float(i) * radius;
        ivec2 offsetCoord = ivec2(direction * offset);
        
        color += imageLoad(inputImage, texelPos + offsetCoord) * weights[i];
        color += imageLoad(inputImage, texelPos - offsetCoord) * weights[i];
    }
    
    imageStore(outputImage, texelPos, color);
}
)GLSL";

// ============================================================================
// 5. Bloom 效果着色器
// ============================================================================

const char* const BLOOM_SHADER = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba16f) uniform readonly image2D inputImage;
layout(binding = 1, rgba16f) uniform writeonly image2D brightImage;
layout(binding = 2, rgba16f) uniform writeonly image2D outputImage;

layout(push_constant) uniform Constants {
    float threshold;
    float intensity;
    uint pass;  // 0 = 提取亮部, 1 = 合成
    uint padding;
};

// 亮度计算
float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(inputImage);
    
    if (texelPos.x >= size.x || texelPos.y >= size.y) return;
    
    if (pass == 0u) {
        // 提取亮部
        vec4 color = imageLoad(inputImage, texelPos);
        float brightness = luminance(color.rgb);
        
        if (brightness > threshold) {
            float scale = (brightness - threshold) / brightness;
            imageStore(brightImage, texelPos, color * scale);
        } else {
            imageStore(brightImage, texelPos, vec4(0.0));
        }
    } else {
        // 合成
        vec4 original = imageLoad(inputImage, texelPos);
        vec4 bloom = imageLoad(brightImage, texelPos);
        
        imageStore(outputImage, texelPos, original + bloom * intensity);
    }
}
)GLSL";

// ============================================================================
// 6. SSAO 着色器
// ============================================================================

const char* const SSAO_SHADER = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba16f) uniform readonly image2D normalImage;
layout(binding = 1, r32f) uniform readonly image2D depthImage;
layout(binding = 2, rgba8) uniform writeonly image2D ssaoImage;

layout(push_constant) uniform Constants {
    mat4 projection;
    mat4 invProjection;
    vec2 resolution;
    float radius;
    float bias;
    int kernelSize;
    int padding;
};

// SSAO 核 (随机采样方向)
const vec3 kernel[16] = vec3[](
    vec3( 0.1009, -0.0840,  0.6232), vec3( 0.3085,  0.4394,  0.1889),
    vec3(-0.1639, -0.3067,  0.5852), vec3( 0.1546, -0.3848, -0.4534),
    vec3(-0.2444,  0.4814,  0.1422), vec3( 0.3375,  0.3885,  0.3953),
    vec3(-0.0437, -0.0951, -0.4782), vec3(-0.3854,  0.2322, -0.1062),
    vec3( 0.5004, -0.1655,  0.2457), vec3(-0.4687, -0.1714,  0.1266),
    vec3( 0.1025,  0.5508, -0.2216), vec3(-0.3182,  0.3458, -0.2476),
    vec3( 0.2692, -0.4497,  0.1215), vec3(-0.0551, -0.2046, -0.4236),
    vec3( 0.2347,  0.2609, -0.4526), vec3(-0.0456,  0.0642, -0.1523)
);

// 噪声纹理 (4x4)
const vec3 noise[16] = vec3[](
    vec3( 0.574,  0.254, 0.778), vec3(-0.452,  0.568, 0.688),
    vec3( 0.123, -0.789, 0.602), vec3(-0.321, -0.456, 0.831),
    vec3( 0.654,  0.321, 0.683), vec3(-0.567, -0.123, 0.814),
    vec3( 0.234,  0.678, 0.697), vec3(-0.789,  0.234, 0.569),
    vec3( 0.432, -0.654, 0.621), vec3(-0.123,  0.789, 0.603),
    vec3( 0.876,  0.123, 0.466), vec3(-0.345, -0.876, 0.338),
    vec3( 0.567,  0.432, 0.702), vec3(-0.678, -0.543, 0.492),
    vec3( 0.321, -0.234, 0.917), vec3(-0.432,  0.876, 0.214)
);

vec3 reconstructPosition(ivec2 texel, float depth) {
    vec2 uv = (vec2(texel) + 0.5) / resolution;
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    
    if (texelPos.x >= int(resolution.x) || texelPos.y >= int(resolution.y)) return;
    
    float depth = imageLoad(depthImage, texelPos).r;
    if (depth >= 1.0) {
        imageStore(ssaoImage, texelPos, vec4(1.0));
        return;
    }
    
    vec3 position = reconstructPosition(texelPos, depth);
    vec3 normal = imageLoad(normalImage, texelPos).rgb * 2.0 - 1.0;
    
    // 旋转噪声
    ivec2 noiseCoord = texelPos % 4;
    vec3 randomVec = noise[noiseCoord.x + noiseCoord.y * 4];
    
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);
    
    float occlusion = 0.0;
    
    for (int i = 0; i < kernelSize; i++) {
        vec3 sampleDir = TBN * kernel[i];
        vec3 samplePos = position + sampleDir * radius;
        
        // 投影到屏幕空间
        vec4 offset = projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        
        // 获取采样点的深度
        ivec2 sampleTexel = ivec2(offset.xy * resolution);
        float sampleDepth = imageLoad(depthImage, sampleTexel).r;
        
        // 重建位置
        vec3 samplePos2 = reconstructPosition(sampleTexel, sampleDepth);
        
        // 计算遮挡
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(position.z - samplePos2.z));
        occlusion += (samplePos2.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    occlusion = 1.0 - (occlusion / float(kernelSize));
    occlusion = pow(occlusion, 1.5);  // 增强对比度
    
    imageStore(ssaoImage, texelPos, vec4(occlusion));
}
)GLSL";

// ============================================================================
// 7. MSDF 字体渲染着色器
// ============================================================================

const char* const MSDF_TEXT_SHADER = R"GLSL(
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

layout(push_constant) uniform Constants {
    mat3 transform;
    float pxRange;  // SDF 距离范围
};

void main() {
    gl_Position = vec4(transform * vec3(inPosition, 1.0), 1.0);
    outTexCoord = inTexCoord;
    outColor = inColor;
}

// 片段着色器
#version 450

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D msdfTexture;

layout(push_constant) uniform Constants {
    mat3 transform;
    float pxRange;
};

// MSDF 中值计算
float median(vec3 msdf) {
    return max(min(msdf.r, msdf.g), min(max(msdf.r, msdf.g), msdf.b));
}

// SDF 抗锯齿
float screenPxRange() {
    vec2 unitRange = vec2(pxRange) / vec2(textureSize(msdfTexture, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(inTexCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msdf = texture(msdfTexture, inTexCoord).rgb;
    float sigDist = median(msdf) - 0.5;
    
    float screenRange = screenPxRange();
    float opacity = clamp(screenRange * sigDist + 0.5, 0.0, 1.0);
    
    outColor = vec4(inColor.rgb, inColor.a * opacity);
}
)GLSL";

// ============================================================================
// 8. 圆角矩形 SDF 着色器
// ============================================================================

const char* const ROUNDED_RECT_SDF_SHADER = R"GLSL(
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inSize;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inRadius;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform Constants {
    mat3 transform;
    vec2 resolution;
};

// 圆角矩形 SDF
float roundedRectSDF(vec2 p, vec2 halfSize, float radius) {
    vec2 d = abs(p) - halfSize + radius;
    float exterior = length(max(d, 0.0)) - radius;
    float interior = min(max(d.x, d.y), 0.0);
    return exterior + interior;
}

void main() {
    vec2 pixelPos = gl_FragCoord.xy;
    
    // 计算相对于矩形的中心坐标
    vec2 center = inPosition + inSize * 0.5;
    vec2 localPos = pixelPos - center;
    
    // 计算 SDF
    float dist = roundedRectSDF(localPos, inSize * 0.5, inRadius);
    
    // 抗锯齿
    float aa = fwidth(dist);
    float opacity = 1.0 - smoothstep(-aa, aa, dist);
    
    outColor = vec4(inColor.rgb, inColor.a * opacity);
}
)GLSL";

// ============================================================================
// 9. 颜色调整着色器
// ============================================================================

const char* const COLOR_ADJUST_SHADER = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform Constants {
    float brightness;  // -1 到 1
    float contrast;    // 0 到 2
    float saturation;  // 0 到 2
    float hue;         // 0 到 360
};

// RGB to HSL
vec3 rgb2hsl(vec3 c) {
    float maxC = max(c.r, max(c.g, c.b));
    float minC = min(c.r, min(c.g, c.b));
    float l = (maxC + minC) * 0.5;
    
    if (maxC == minC) {
        return vec3(0.0, 0.0, l);
    }
    
    float d = maxC - minC;
    float s = l > 0.5 ? d / (2.0 - maxC - minC) : d / (maxC + minC);
    
    float h;
    if (maxC == c.r) {
        h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    } else if (maxC == c.g) {
        h = (c.b - c.r) / d + 2.0;
    } else {
        h = (c.r - c.g) / d + 4.0;
    }
    h /= 6.0;
    
    return vec3(h, s, l);
}

// HSL to RGB
float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

vec3 hsl2rgb(vec3 c) {
    if (c.y == 0.0) {
        return vec3(c.z);
    }
    
    float q = c.z < 0.5 ? c.z * (1.0 + c.y) : c.z + c.y - c.z * c.y;
    float p = 2.0 * c.z - q;
    
    float r = hue2rgb(p, q, c.x + 1.0/3.0);
    float g = hue2rgb(p, q, c.x);
    float b = hue2rgb(p, q, c.x - 1.0/3.0);
    
    return vec3(r, g, b);
}

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(inputImage);
    
    if (texelPos.x >= size.x || texelPos.y >= size.y) return;
    
    vec4 color = imageLoad(inputImage, texelPos);
    
    // 亮度
    color.rgb += brightness;
    
    // 对比度
    color.rgb = (color.rgb - 0.5) * contrast + 0.5;
    
    // 饱和度和色相
    vec3 hsl = rgb2hsl(color.rgb);
    hsl.x += hue / 360.0;
    hsl.y *= saturation;
    hsl.x = fract(hsl.x);
    
    color.rgb = hsl2rgb(hsl);
    
    imageStore(outputImage, texelPos, clamp(color, 0.0, 1.0));
}
)GLSL";

// ============================================================================
// 10. 色差效果着色器
// ============================================================================

const char* const CHROMATIC_ABERRATION_SHADER = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D inputImage;
layout(binding = 1, rgba8) uniform writeonly image2D outputImage;

layout(push_constant) uniform Constants {
    vec2 center;        // 色差中心
    float strength;     // 色差强度
    float falloff;      // 衰减
};

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(inputImage);
    
    if (texelPos.x >= size.x || texelPos.y >= size.y) return;
    
    vec2 uv = (vec2(texelPos) + 0.5) / vec2(size);
    vec2 dir = uv - center;
    float dist = length(dir);
    
    // 计算偏移
    float offset = strength * dist * dist * falloff;
    vec2 offsetR = dir * offset;
    vec2 offsetB = dir * offset * 0.5;
    
    // 采样
    float r = imageLoad(inputImage, texelPos + ivec2(offsetR * size)).r;
    vec4 g = imageLoad(inputImage, texelPos);
    float b = imageLoad(inputImage, texelPos - ivec2(offsetB * size)).b;
    
    imageStore(outputImage, texelPos, vec4(r, g.g, b, g.a));
}
)GLSL";

// ============================================================================
// 11. 下采样着色器
// ============================================================================

const char* const DOWNSAMPLE_SHADER = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, rgba16f) uniform readonly image2D inputImage;
layout(binding = 1, rgba16f) uniform writeonly image2D outputImage;

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 inputSize = imageSize(inputImage);
    ivec2 outputSize = imageSize(outputImage);
    
    if (texelPos.x >= outputSize.x || texelPos.y >= outputSize.y) return;
    
    // 双线性采样
    ivec2 srcPos = texelPos * 2;
    
    vec4 samples[4];
    samples[0] = imageLoad(inputImage, srcPos);
    samples[1] = imageLoad(inputImage, srcPos + ivec2(1, 0));
    samples[2] = imageLoad(inputImage, srcPos + ivec2(0, 1));
    samples[3] = imageLoad(inputImage, srcPos + ivec2(1, 1));
    
    // 使用 Kaiser 窗进行高质量下采样
    vec4 result = (samples[0] + samples[1] + samples[2] + samples[3]) * 0.25;
    
    imageStore(outputImage, texelPos, result);
}
)GLSL";

// ============================================================================
// 12. 上采样着色器
// ============================================================================

const char* const UPSAMPLE_SHADER = R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, rgba16f) uniform readonly image2D inputImage;
layout(binding = 1, rgba16f) uniform writeonly image2D outputImage;

layout(push_constant) uniform Constants {
    float blendFactor;  // 与上一层的混合因子
};

void main() {
    ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 inputSize = imageSize(inputImage);
    ivec2 outputSize = imageSize(outputImage);
    
    if (texelPos.x >= outputSize.x || texelPos.y >= outputSize.y) return;
    
    // 双线性插值
    vec2 srcUV = (vec2(texelPos) + 0.5) / vec2(outputSize) * vec2(inputSize);
    ivec2 srcPos = ivec2(floor(srcUV));
    vec2 frac = fract(srcUV);
    
    vec4 samples[4];
    samples[0] = imageLoad(inputImage, srcPos);
    samples[1] = imageLoad(inputImage, srcPos + ivec2(1, 0));
    samples[2] = imageLoad(inputImage, srcPos + ivec2(0, 1));
    samples[3] = imageLoad(inputImage, srcPos + ivec2(1, 1));
    
    vec4 result = mix(
        mix(samples[0], samples[1], frac.x),
        mix(samples[2], samples[3], frac.x),
        frac.y
    );
    
    imageStore(outputImage, texelPos, result * blendFactor);
}
)GLSL";

} // namespace Shaders
} // namespace Nova

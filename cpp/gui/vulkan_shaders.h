/**
 * Nova Renderer - Shader Library
 * 
 * 内置着色器源码 (GLSL -> SPIR-V)
 * 
 * 包含:
 * - 基础形状着色器 (矩形、圆角矩形、渐变)
 * - MSDF 文字着色器
 * - 计算着色器 (模糊、Bloom等)
 */

#pragma once

#include <string>

namespace Nova {
namespace Shaders {

// ============================================================================
// 通用顶点着色器
// ============================================================================

const char* const VertexShader_Basic = R"GLSL(
#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
} pc;

void main() {
    vec3 pos = pc.uTransform * vec3(inPosition, 1.0);
    gl_Position = vec4(pos.xy, 0.0, 1.0);
    vTexCoord = inTexCoord;
    vColor = inColor;
}
)GLSL";

// ============================================================================
// 矩形填充
// ============================================================================

const char* const FragmentShader_FillRect = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vColor;
}
)GLSL";

// ============================================================================
// 圆角矩形 (SDF)
// ============================================================================

const char* const FragmentShader_RoundRect = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec4 uRect;        // xy = position, zw = size
    float uRadius;
    float uEdgeSoftness;
} pc;

// 圆角矩形 SDF
float roundedRectSDF(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    vec2 halfSize = pc.uRect.zw * 0.5;
    vec2 p = vTexCoord * pc.uRect.zw - halfSize;
    
    float dist = roundedRectSDF(p, halfSize, pc.uRadius);
    float alpha = 1.0 - smoothstep(-pc.uEdgeSoftness, pc.uEdgeSoftness, dist);
    
    outColor = vec4(vColor.rgb, vColor.a * alpha);
}
)GLSL";

// ============================================================================
// 渐变填充
// ============================================================================

const char* const FragmentShader_Gradient = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec4 uColor1;
    vec4 uColor2;
    int uVertical;  // 0 = horizontal, 1 = vertical
} pc;

void main() {
    float t = pc.uVertical != 0 ? vTexCoord.y : vTexCoord.x;
    outColor = mix(pc.uColor1, pc.uColor2, t) * vColor;
}
)GLSL";

// ============================================================================
// 圆角矩形渐变
// ============================================================================

const char* const FragmentShader_RoundRectGradient = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec4 uRect;
    float uRadius;
    float uEdgeSoftness;
    vec4 uColor1;
    vec4 uColor2;
    int uVertical;
} pc;

float roundedRectSDF(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    vec2 halfSize = pc.uRect.zw * 0.5;
    vec2 p = vTexCoord * pc.uRect.zw - halfSize;
    
    float dist = roundedRectSDF(p, halfSize, pc.uRadius);
    float alpha = 1.0 - smoothstep(-pc.uEdgeSoftness, pc.uEdgeSoftness, dist);
    
    float t = pc.uVertical != 0 ? vTexCoord.y : vTexCoord.x;
    vec4 color = mix(pc.uColor1, pc.uColor2, t);
    
    outColor = vec4(color.rgb, color.a * alpha);
}
)GLSL";

// ============================================================================
// MSDF 文字渲染
// ============================================================================

const char* const FragmentShader_TextMSDF = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uFontAtlas;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    float uPxRange;    // MSDF pixel range
    float uThreshold;  // 0.5 for standard
} pc;

// MSDF distance estimation
float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

float screenPxRange() {
    vec2 unitRange = vec2(pc.uPxRange) / vec2(textureSize(uFontAtlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(vTexCoord);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msdf = texture(uFontAtlas, vTexCoord).rgb;
    float sigDist = median(msdf.r, msdf.g, msdf.b) - pc.uThreshold;
    
    float screenRange = screenPxRange();
    float opacity = clamp(sigDist * screenRange + 0.5, 0.0, 1.0);
    
    // 可选：添加发光效果
    // float glow = exp(-sigDist * 0.5) * 0.3;
    // opacity = max(opacity, glow);
    
    outColor = vec4(vColor.rgb, vColor.a * opacity);
}
)GLSL";

// ============================================================================
// SDF 文字渲染 (单通道)
// ============================================================================

const char* const FragmentShader_TextSDF = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uFontAtlas;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    float uPxRange;
    float uThreshold;
} pc;

void main() {
    float dist = texture(uFontAtlas, vTexCoord).r;
    
    vec2 unitRange = vec2(pc.uPxRange) / vec2(textureSize(uFontAtlas, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(vTexCoord);
    float screenRange = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    
    float opacity = clamp((dist - pc.uThreshold) * screenRange + 0.5, 0.0, 1.0);
    
    outColor = vec4(vColor.rgb, vColor.a * opacity);
}
)GLSL";

// ============================================================================
// 纹理采样
// ============================================================================

const char* const FragmentShader_Texture = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, vTexCoord) * vColor;
}
)GLSL";

// ============================================================================
// 纹理 + Alpha 混合
// ============================================================================

const char* const FragmentShader_TextureAlpha = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    outColor = vec4(texColor.rgb, texColor.a * vColor.a);
}
)GLSL";

// ============================================================================
// 阴影
// ============================================================================

const char* const FragmentShader_Shadow = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec4 uRect;
    float uRadius;
    float uBlur;
    vec4 uShadowColor;
} pc;

float roundedRectSDF(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0) - radius;
}

void main() {
    vec2 halfSize = pc.uRect.zw * 0.5;
    vec2 p = vTexCoord * pc.uRect.zw - halfSize;
    
    float dist = roundedRectSDF(p, halfSize, pc.uRadius);
    float shadow = exp(-dist / pc.uBlur);
    
    outColor = vec4(pc.uShadowColor.rgb, pc.uShadowColor.a * shadow);
}
)GLSL";

// ============================================================================
// 高斯模糊 (计算着色器)
// ============================================================================

const char* const ComputeShader_GaussianBlur = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform PushConstants {
    vec2 uDirection;  // (1, 0) for horizontal, (0, 1) for vertical
    float uRadius;
} pc;

// 高斯权重 (预计算)
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    vec2 texelSize = 1.0 / vec2(imageSize(uInput));
    
    vec4 result = imageLoad(uInput, texelCoord) * weights[0];
    
    for (int i = 1; i < 5; i++) {
        vec2 offset = pc.uDirection * texelSize * float(i) * pc.uRadius;
        result += imageLoad(uInput, texelCoord + ivec2(offset)) * weights[i];
        result += imageLoad(uInput, texelCoord - ivec2(offset)) * weights[i];
    }
    
    imageStore(uOutput, texelCoord, result);
}
)GLSL";

// ============================================================================
// Bloom (计算着色器)
// ============================================================================

const char* const ComputeShader_Bloom = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform PushConstants {
    float uThreshold;  // 亮度阈值
    float uIntensity;  // Bloom 强度
    float uRadius;     // 模糊半径
} pc;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    vec4 color = imageLoad(uInput, texelCoord);
    
    // 亮度提取
    float lum = luminance(color.rgb);
    if (lum > pc.uThreshold) {
        color.rgb = (color.rgb - pc.uThreshold) / (1.0 - pc.uThreshold);
    } else {
        color.rgb = vec3(0.0);
    }
    
    imageStore(uOutput, texelCoord, color * pc.uIntensity);
}
)GLSL";

// ============================================================================
// 色彩调整 (计算着色器)
// ============================================================================

const char* const ComputeShader_ColorAdjust = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform PushConstants {
    float uBrightness;  // -1.0 to 1.0
    float uContrast;    // 0.0 to 2.0, 1.0 = normal
    float uSaturation;  // 0.0 to 2.0, 1.0 = normal
    float uHue;         // -180 to 180 degrees
} pc;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    vec4 color = imageLoad(uInput, texelCoord);
    
    // 亮度
    color.rgb += pc.uBrightness;
    
    // 对比度
    color.rgb = (color.rgb - 0.5) * pc.uContrast + 0.5;
    
    // 饱和度 (通过 HSV)
    vec3 hsv = rgb2hsv(color.rgb);
    hsv.y *= pc.uSaturation;
    
    // 色调偏移
    hsv.x += pc.uHue / 360.0;
    hsv.x = fract(hsv.x);
    
    color.rgb = hsv2rgb(hsv);
    
    imageStore(uOutput, texelCoord, clamp(color, 0.0, 1.0));
}
)GLSL";

// ============================================================================
// 色差效果 (计算着色器)
// ============================================================================

const char* const ComputeShader_ChromaticAberration = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform PushConstants {
    float uStrength;
    vec2 uCenter;  // 扭曲中心
} pc;

void main() {
    ivec2 texelCoord = ivec2(gl_GlobalInvocationID.xy);
    vec2 size = vec2(imageSize(uInput));
    vec2 uv = vec2(texelCoord) / size;
    
    vec2 dir = uv - pc.uCenter;
    float dist = length(dir);
    
    float r = imageLoad(uInput, ivec2((uv + dir * pc.uStrength * dist) * size)).r;
    float g = imageLoad(uInput, texelCoord).g;
    float b = imageLoad(uInput, ivec2((uv - dir * pc.uStrength * dist) * size)).b;
    float a = imageLoad(uInput, texelCoord).a;
    
    imageStore(uOutput, texelCoord, vec4(r, g, b, a));
}
)GLSL";

// ============================================================================
// 下采样 (计算着色器)
// ============================================================================

const char* const ComputeShader_Downsample = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 inCoord = outCoord * 2;
    
    vec4 result = vec4(0.0);
    result += imageLoad(uInput, inCoord + ivec2(0, 0));
    result += imageLoad(uInput, inCoord + ivec2(1, 0));
    result += imageLoad(uInput, inCoord + ivec2(0, 1));
    result += imageLoad(uInput, inCoord + ivec2(1, 1));
    
    imageStore(uOutput, outCoord, result * 0.25);
}
)GLSL";

// ============================================================================
// 上采样 (计算着色器)
// ============================================================================

const char* const ComputeShader_Upsample = R"GLSL(
#version 450

layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0, rgba8) uniform readonly image2D uInput;
layout(binding = 1, rgba8) uniform writeonly image2D uOutput;

layout(push_constant) uniform PushConstants {
    float uWeight;  // 混合权重
} pc;

void main() {
    ivec2 outCoord = ivec2(gl_GlobalInvocationID.xy);
    ivec2 inCoord = outCoord / 2;
    
    // 双线性插值
    vec4 result = imageLoad(uInput, inCoord);
    
    vec4 existing = imageLoad(uOutput, outCoord);
    result = mix(existing, result, pc.uWeight);
    
    imageStore(uOutput, outCoord, result);
}
)GLSL";

// ============================================================================
// 着色器组合器 (用于复杂特效)
// ============================================================================

const char* const FragmentShader_Composite = R"GLSL(
#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uBaseLayer;
layout(binding = 1) uniform sampler2D uBloomLayer;
layout(binding = 2) uniform sampler2D uOverlayLayer;

layout(push_constant) uniform PushConstants {
    float uBloomIntensity;
    float uOverlayAlpha;
} pc;

void main() {
    vec4 base = texture(uBaseLayer, vTexCoord);
    vec4 bloom = texture(uBloomLayer, vTexCoord);
    vec4 overlay = texture(uOverlayLayer, vTexCoord);
    
    // Bloom 添加
    vec3 color = base.rgb + bloom.rgb * pc.uBloomIntensity;
    
    // Overlay 混合
    color = mix(color, overlay.rgb, overlay.a * pc.uOverlayAlpha);
    
    outColor = vec4(color, base.a);
}
)GLSL";

} // namespace Shaders
} // namespace Nova

#version 450

// 圆角矩形 SDF 片段着色器

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 uRect;     // xy = position, zw = size
    float uRadius;
    float uStrokeWidth;
    float uStrokeOffset;
} pc;

// SDF for rounded rectangle
float roundedRectSDF(vec2 p, vec2 size, float radius) {
    vec2 d = abs(p) - size + radius;
    float exterior = length(max(d, 0.0)) - radius;
    float interior = min(max(d.x, d.y), 0.0);
    return exterior + interior;
}

void main() {
    vec2 halfSize = pc.uRect.zw * 0.5;
    vec2 center = pc.uRect.xy + halfSize;
    vec2 localPos = gl_FragCoord.xy - center;
    
    float dist = roundedRectSDF(localPos, halfSize, pc.uRadius);
    
    // Anti-aliasing
    float aa = fwidth(dist);
    float alpha = 1.0 - smoothstep(-aa, aa, dist);
    
    // Stroke
    if (pc.uStrokeWidth > 0.0) {
        float strokeDist = abs(dist - pc.uStrokeOffset) - pc.uStrokeWidth * 0.5;
        float strokeAlpha = 1.0 - smoothstep(-aa, aa, strokeDist);
        alpha = max(alpha, strokeAlpha * 0.5);
    }
    
    outColor = vec4(inColor.rgb, inColor.a * alpha);
}

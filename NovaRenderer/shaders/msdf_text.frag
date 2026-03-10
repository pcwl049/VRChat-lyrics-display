#version 450

// MSDF 文本片段着色器

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

#version 450

// 基础顶点着色器
// 用于 2D UI 渲染

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat3 uTransform;
    vec2 uResolution;
} pc;

void main() {
    vec2 pos = pc.uTransform * vec3(inPosition, 1.0);
    vec2 ndc = (pos / pc.uResolution) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    outTexCoord = inTexCoord;
    outColor = inColor;
}

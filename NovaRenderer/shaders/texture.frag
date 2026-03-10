#version 450

// 纹理片段着色器

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

void main() {
    outColor = texture(uTexture, inTexCoord) * inColor;
}

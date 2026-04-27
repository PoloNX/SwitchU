#version 460

layout (location = 0) in vec2 fragUv;
layout (location = 1) in vec4 fragColor;

layout (binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outColor;

void main() {
    outColor = fragColor * texture(tex, fragUv);
}
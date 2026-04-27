#version 460

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUv;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 fragUv;
layout (location = 1) out vec4 fragColor;

layout (std140, binding = 0) uniform VertUbo {
    mat4 projection;
};

void main() {
    gl_Position = projection * vec4(inPos, 0.0, 1.0);
    fragUv = inUv;
    fragColor = inColor;
}
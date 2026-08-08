// Vertex shader for 2D rendering — deko3d / uam
// Compiled by: uam -s vert -o basic_vsh.dksh basic_vsh.glsl
#version 460

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec4 inColor;
layout (location = 3) in vec2 inShapePos;
layout (location = 4) in vec2 inShapeHalf;
layout (location = 5) in vec2 inShapeRound;

layout (std140, binding = 0) uniform VsUniforms {
    mat4 projection;
};

layout (location = 0) out vec2 fragUV;
layout (location = 1) out vec4 fragColor;
layout (location = 2) out vec2 fragShapePos;
layout (location = 3) out vec2 fragShapeHalf;
layout (location = 4) out vec2 fragShapeRound;

void main() {
    gl_Position = projection * vec4(inPos, 0.0, 1.0);
    fragUV    = inUV;
    fragColor = inColor;
    fragShapePos = inShapePos;
    fragShapeHalf = inShapeHalf;
    fragShapeRound = inShapeRound;
}

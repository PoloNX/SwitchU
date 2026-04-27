// Clip-space fullscreen pass vertex shader for iterative blur.
// Matches the LiquidGlass reference BlurPass behavior and avoids depending
// on the 2D orthographic projection state for offscreen blur passes.
#version 460

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec2 fragUV;
layout (location = 1) out vec4 fragColor;

void main() {
    gl_Position = vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
    fragColor = inColor;
}
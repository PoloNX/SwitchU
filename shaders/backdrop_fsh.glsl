// Fragment shader for drawing captured/blurred offscreen backdrop textures.
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;
layout (location = 2) in vec2 fragShapePos;
layout (location = 3) in vec2 fragShapeHalf;
layout (location = 4) in vec2 fragShapeRound;

layout (binding = 0) uniform sampler2D tex;

layout (location = 0) out vec4 outColor;

float sdRoundedBox(vec2 p, vec2 halfExtent, float radius) {
    float r = min(radius, min(halfExtent.x, halfExtent.y));
    vec2 q = abs(p) - (halfExtent - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec4 c = texture(tex, fragUV) * fragColor;
    float radius = fragShapeRound.x;
    if (radius > 0.0) {
        float distanceToEdge = sdRoundedBox(fragShapePos, fragShapeHalf, radius);
        float ramp = max(length(vec2(dFdx(distanceToEdge), dFdy(distanceToEdge))), 1e-5);
        c.a *= 1.0 - smoothstep(-ramp, ramp, distanceToEdge);
    }
    if (c.a <= 0.0) discard;
    outColor = c;
}

// Fragment shader for 2D rendering — deko3d / uam
// Compiled by: uam -s frag -o basic_fsh.dksh basic_fsh.glsl
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;
layout (location = 2) in vec2 fragShapePos;
layout (location = 3) in vec2 fragShapeHalf;
layout (location = 4) in vec2 fragShapeRound;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 1) uniform FsUniforms {
    int useTexture;   // 0 = color only, 1 = texture × color
};

layout (location = 0) out vec4 outColor;

float sdRoundedBox(vec2 p, vec2 halfExtent, float radius) {
    float r = min(radius, min(halfExtent.x, halfExtent.y));
    vec2 q = abs(p) - (halfExtent - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec4 c = (useTexture != 0) ? texture(tex, fragUV) * fragColor : fragColor;
    float radius = fragShapeRound.x;
    if (radius > 0.0) {
        float distanceToEdge = sdRoundedBox(fragShapePos, fragShapeHalf, radius);
        float ramp = max(length(vec2(dFdx(distanceToEdge), dFdy(distanceToEdge))), 1e-5);
        float thickness = fragShapeRound.y;
        if (thickness > 0.0) {
            float halfStroke = max(thickness, 1.0) * 0.5;
            c.a *= 1.0 - smoothstep(-ramp, ramp,
                                    abs(distanceToEdge + halfStroke) - halfStroke);
        } else {
            c.a *= 1.0 - smoothstep(-ramp, ramp, distanceToEdge);
        }
    }
    if (c.a <= 0.0) discard;
    outColor = c;
}

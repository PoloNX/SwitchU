#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;
layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 1) uniform FsUniforms {
    int useTexture;
    float refractionIntensity;
    float blurIntensity;
    float noiseIntensity;
    vec4 material;
    vec4 animation;
    vec4 refractionCurve;
    vec4 glowCurve;
    vec4 tintColor;
    vec4 panelRect;
    vec4 screenSize;
};

layout (location = 0) out vec4 outColor;

float roundedBoxDistance(vec2 point, vec2 halfExtent, float radius) {
    radius = min(radius, min(halfExtent.x, halfExtent.y));
    vec2 q = abs(point) - (halfExtent - vec2(radius));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

vec3 saturateColor(vec3 color, float amount) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), color, amount);
}

vec4 overlappingBlur(vec2 uv, vec2 texel, float spread) {
    vec2 a = texel * spread * 1.25;
    vec2 b = texel * spread * 2.35;
    vec4 color = texture(tex, uv) * 0.30;
    color += texture(tex, uv + vec2(a.x, 0.0)) * 0.13;
    color += texture(tex, uv - vec2(a.x, 0.0)) * 0.13;
    color += texture(tex, uv + vec2(0.0, a.y)) * 0.13;
    color += texture(tex, uv - vec2(0.0, a.y)) * 0.13;
    color += texture(tex, uv + vec2(b.x, b.y)) * 0.045;
    color += texture(tex, uv - vec2(b.x, b.y)) * 0.045;
    color += texture(tex, uv + vec2(b.x, -b.y)) * 0.045;
    color += texture(tex, uv + vec2(-b.x, b.y)) * 0.045;
    return color;
}

void main() {
    vec2 sizePx = max(panelRect.zw, vec2(1.0));
    vec2 localPx = (fragUV - vec2(0.5)) * sizePx;
    vec2 halfExtent = sizePx * 0.5;
    float distance = roundedBoxDistance(localPx, halfExtent, screenSize.w);
    float edgeWidth = max(length(vec2(dFdx(distance), dFdy(distance))), 0.75);
    float coverage = 1.0 - smoothstep(-edgeWidth, edgeWidth, distance);
    if (coverage <= 0.0)
        discard;

    float inside = max(-distance, 0.0);
    float edge = 1.0 - smoothstep(0.0, max(12.0, min(sizePx.x, sizePx.y) * 0.18), inside);
    vec2 normal = normalize(localPx + vec2(0.0001));
    vec2 normalized = localPx / max(halfExtent, vec2(1.0));
    float dome = clamp(1.0 - dot(normalized, normalized) * 0.48, 0.0, 1.0);
    // A subtle inward lens plus a stronger curved rim gives large panels the
    // same convex/perspective impression as the Wii U acrylic shells.
    vec2 lensUv = vec2(0.5) + (fragUV - vec2(0.5))
                * (1.0 - refractionIntensity * 0.030 * dome);
    vec2 displaced = lensUv - normal * edge * refractionIntensity * 0.060;
    vec2 screenUv = (panelRect.xy + displaced * sizePx) / screenSize.xy;
    screenUv = clamp(screenUv, vec2(0.0), vec2(1.0));

    vec4 color = texture(tex, screenUv);
    if (blurIntensity > 0.01) {
        float spread = min(blurIntensity, 2.5);
        color = mix(color,
                    overlappingBlur(screenUv, 1.0 / screenSize.xy, spread),
                    clamp(blurIntensity * 0.12, 0.0, 0.30));
    }

    float topReflection = pow(clamp(1.0 - fragUV.y, 0.0, 1.0), 2.0);
    float diagonalReflection = pow(clamp(1.0 - (fragUV.x + fragUV.y) * 0.58,
                                               0.0, 1.0), 4.0);
    float reflection = edge * (0.055 + material.x * 0.08)
                     + topReflection * 0.045
                     + diagonalReflection * (0.035 + material.x * 0.035);
    color.rgb = mix(color.rgb, vec3(0.96, 0.985, 1.0),
                    clamp(reflection, 0.0, 0.22));
    color.rgb = mix(color.rgb, tintColor.rgb,
                    clamp(tintColor.a * 0.34, 0.0, 0.34));
    color.rgb = saturateColor(color.rgb, material.y);

    float lowerShade = smoothstep(0.52, 1.0, fragUV.y) * (0.025 + edge * 0.025);
    color.rgb *= 1.0 - lowerShade;

    float unavailable = clamp(screenSize.z, 0.0, 1.0);
    float luminance = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    color.rgb = mix(color.rgb, vec3(luminance) * 0.82, unavailable * 0.42);
    color.a = coverage;
    outColor = color * fragColor;
}

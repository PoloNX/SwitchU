// Horizontal Gaussian blur — deko3d / uam
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 1) uniform FsUniforms {
    int   useTexture;
    float blurRadius;   // texel spread
    float texelSizeX;   // 1.0 / textureWidth
    float texelSizeY;   // 1.0 / textureHeight
};

layout (location = 0) out vec4 outColor;

void main() {
    // 9-tap Gaussian (sigma ~ radius/2)
    const float weights[5] = float[](0.227027, 0.194946, 0.121622, 0.054054, 0.016216);

    vec4 result = texture(tex, fragUV) * weights[0];
    for (int i = 1; i < 5; ++i) {
        float off = float(i) * blurRadius;
        vec2 offsetH = vec2(texelSizeX * off, 0.0);
        result += texture(tex, fragUV + offsetH) * weights[i];
        result += texture(tex, fragUV - offsetH) * weights[i];
    }
    outColor = vec4(result.rgb * fragColor.rgb, 1.0);
}

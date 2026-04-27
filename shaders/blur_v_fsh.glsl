// Vertical Gaussian blur — deko3d / uam
#version 460

layout (location = 0) in vec2 fragUV;
layout (location = 1) in vec4 fragColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 1) uniform FsUniforms {
    int   useTexture;
    float blurRadius;
    float texelSizeX;
    float texelSizeY;
};

layout (location = 0) out vec4 outColor;

void main() {
    const float weights[5] = float[](0.227027, 0.194946, 0.121622, 0.054054, 0.016216);

    vec4 result = texture(tex, fragUV) * weights[0];
    for (int i = 1; i < 5; ++i) {
        float off = float(i) * blurRadius;
        vec2 offsetV = vec2(0.0, texelSizeY * off);
        result += texture(tex, fragUV + offsetV) * weights[i];
        result += texture(tex, fragUV - offsetV) * weights[i];
    }
    outColor = vec4(result.rgb * fragColor.rgb, 1.0);
}

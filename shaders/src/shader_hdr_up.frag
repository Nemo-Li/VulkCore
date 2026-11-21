#version 450

// Bloom 金字塔升采样，将低分辨率的宽光晕逐级混入较细的一级。

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D support;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

void main() {
    // 3×3 帐篷核由两个 [1,2,1] 一维核相乘得到，除以 16 保持权重和为 1。
    vec2 pixel = 1.0 / vec2(textureSize(source, 0));
    vec3 blurred = vec3(0.0);
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float weight = float((2 - abs(x)) * (2 - abs(y))) / 16.0;
            blurred += texture(source, uv + vec2(x, y) * pixel).rgb * weight;
        }
    }
    // support 保存较细一级的细节，value.x 控制对低分辨率模糊结果的混合比例。
    outColor = vec4(mix(texture(support, uv).rgb, blurred, parameters.value.x), 1.0);
}

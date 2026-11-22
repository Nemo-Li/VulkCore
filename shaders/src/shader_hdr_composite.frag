#version 450
#extension GL_GOOGLE_include_directive : require

#include "solar_flare.glsl"

// HDR 呈现阶段。先做可选的 SMAA 邻域混合，再在线性空间叠加 Bloom 和镜头光晕。

layout(location = 0) in vec2 uv;
layout(location = 1) flat in vec4 solarFlare;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D source;
layout(binding = 1) uniform sampler2D bloom;
layout(binding = 2) uniform sampler2D weights;
layout(push_constant) uniform Parameters { vec4 value; } parameters;

vec3 neighborhood(vec3 center) {
    vec2 pixel = vec2(1.0, -1.0) / vec2(textureSize(source, 0));
    // 同一条边的两个方向由当前像素与邻居分担。聚合两侧权重以恢复混合范围。
    vec4 here = texture(weights, uv);
    float right = texture(weights, uv + vec2(pixel.x, 0.0)).a;
    float top = texture(weights, uv + vec2(0.0, pixel.y)).g;
    if (right + here.b + top + here.r < 0.00001) return center;

    // 选择权重更强的轴，避免在交叉边处同时模糊两个方向。
    // 亚像素偏移由硬件双线性采样插值，再对两侧结果做归一化加权。
    bool horizontal = max(right, here.b) > max(top, here.r);
    vec2 weight = horizontal ? vec2(right, here.b) : vec2(top, here.r);
    vec2 direction = horizontal ? vec2(pixel.x, 0.0) : vec2(0.0, pixel.y);
    vec3 first = texture(source, uv + direction * weight.x).rgb;
    vec3 second = texture(source, uv - direction * weight.y).rgb;
    return (first * weight.x + second * weight.y) / (weight.x + weight.y);
}

vec3 linearToSrgb(vec3 color) {
    return mix(12.92 * color, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               greaterThan(color, vec3(0.0031308)));
}

void main() {
    vec3 color = texture(source, uv).rgb;
    if (parameters.value.y > 0.5) color = neighborhood(color);
    // 线性域的加法完成后截到显示范围。UNORM 目标由 shader 编码，sRGB 目标由附件格式编码。
    color += texture(bloom, uv).rgb * parameters.value.x + solarFlareRadiance(uv, solarFlare);
    color = clamp(color, 0.0, 1.0);
    if (parameters.value.z > 0.5) color = linearToSrgb(color);
    outColor = vec4(color, 1.0);
}

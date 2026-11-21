#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

// 线框过渡的流动材质。把 UV 噪声与边缘遮罩用于 Alpha，保留网格自身的顶点配色。

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 vertexColor;
layout(location = 3) in vec3 worldPosition;
SUN_SHADOW_OUTPUT

layout(push_constant) uniform Material {
    mat4 model;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 parameters0;
    vec4 parameters1;
} material;

vec2 hash(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

// 二维 Simplex 梯度噪声。斜切坐标落入三角网格，再按三个角点之间的位置选取贡献。
// h⁴ 权重将角点贡献平滑衰减到零，使 UV 平流连续。
float noise2d(vec2 p) {
    const float k1 = 0.366025404;
    const float k2 = 0.211324865;
    vec2 i = floor(p + (p.x + p.y) * k1);
    vec2 a = p - i + (i.x + i.y) * k2;
    vec2 o = a.x > a.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    vec2 b = a - o + k2;
    vec2 c = a - 1.0 + 2.0 * k2;
    vec3 h = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    return dot(h * h * h * h * vec3(dot(a, hash(i)), dot(b, hash(i + o)),
                                     dot(c, hash(i + 1.0))), vec3(70.0));
}

void main() {
    // 用独立的流动时钟平移 U。parameters0.xy 为噪声频率，z 为强度，w 为滚动速度。
    vec4 wave = material.parameters0;
    vec2 point = (uv + vec2(ubo.time.y * wave.w, 0.0)) * wave.xy;
    float noiseMask = noise2d(point) * wave.z;
    // 矩形两轴的软边遮罩相乘，以免条带在起止和侧缘处突然截断。opacity 控制整体显现进度。
    vec2 mask = smoothstep(vec2(0.05), vec2(0.4), uv) *
                (1.0 - smoothstep(vec2(0.6), vec2(0.95), uv));
    vec3 color = clamp(vertexColor.rgb * 2.0, 0.0, 1.0) * mix(0.72, 1.0, sunVisibility(worldPosition));
    SUN_SHADOW_COLOR(vec4(color, clamp(noiseMask * mask.x * mask.y * material.opacity, 0.0, 1.0)));
}

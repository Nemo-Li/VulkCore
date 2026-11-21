#version 450
#extension GL_GOOGLE_include_directive : require

// 在天空背景中合成纬向渐变、方向散射、太阳盘与云雾。

#include "scene_fog.glsl"

// 先做深度测试，可跳过已被近处几何遮挡的天空和椭球相交计算。
layout(early_fragment_tests) in;
layout(location = 0) in vec3 fragWorldRay;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 1) uniform sampler2D skyboxTexture;

const float PI = 3.14159265358979323846;

void main() {
    vec3 ray = normalize(fragWorldRay);
    vec3 sun = normalize(ubo.sunLightDirection.xyz);
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w;
    float sunCosine = clamp(dot(ray, sun), -1.0, 1.0);

    // 无云天空贴图沿经度颜色恒定。固定采样一列保留天顶到下方的纬度渐变，并消除经度接缝。
    float latitude = acos(clamp(ray.y, -1.0, 1.0)) / PI;
    vec3 palette = texture(skyboxTexture, vec2(0.5, latitude)).rgb;
    vec3 color = palette + atmosphereInscatter(ray, sun, sunlight);

    // 单位方向之差的长度是球面弦长，随视线与太阳的夹角单调增加。
    // fwidth 估算一个像素内的弦长变化，以此软化日盘边缘并减轻锯齿。
    float sunDistance = length(ray - sun);
    float edge = max(0.001, fwidth(sunDistance));
    float disk = 1.0 - smoothstep(0.009 - edge, 0.009 + edge, sunDistance);
    float sunVisibility = smoothstep(-0.03, 0.03, sun.y);
    color += sunlight * vec3(1.0, 0.91, 0.73) * sunVisibility *
        (2.8 * disk + 0.18 * miePhase(sunCosine, 0.91));

    // 背景没有实际表面交点，分别给贴地浮雾和高空云设定路径上限。
    // 云雾在日盘和光晕之后混合，因此能同时遮蔽二者。
    const float skyMistDistance = 1600.0;
    const float skyCloudDistance = 16500.0;
    outColor = vec4(applySceneFog(color, sceneMistAt(ray, skyMistDistance, skyCloudDistance)), 1.0);
}

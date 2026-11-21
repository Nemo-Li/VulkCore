#ifndef VULKCORE_SUN_SHADOW_GLSL
#define VULKCORE_SUN_SHADOW_GLSL

#include "scene_frame.glsl"

vec3 sunReceiverNormal(vec3 position) {
    // 插值法线用于着色，深度比较需要当前三角形的切平面。用位置导数恢复实际面法线。
    vec3 dx = dFdx(position);
    vec3 dy = dFdy(position);
    vec3 area = cross(dx / max(length(dx), 1e-15), dy / max(length(dy), 1e-15));
    return area * inversesqrt(max(dot(area, area), 1e-12));
}

#ifdef VULKCORE_SHADOW_PASS
// 投影版本只计算覆盖率，避免在写深度时读取同一张阴影图。
float sunVisibility(vec3 position, vec3 geometricNormal) {
    return 1.0;
}
#else
layout(set = 0, binding = 10) uniform sampler2D sunShadowAtlas;

float sampleSunShadow(vec3 position, vec3 geometricNormal, int cascade) {
    mat4 lightProjection = ubo.sunShadowMatrices[cascade];
    vec3 light = (lightProjection * vec4(position, 1.0)).xyz;
    vec2 uv = light.xy * 0.5 + 0.5;
    if (light.z <= 0.0 || light.z >= 1.0 || any(lessThan(uv, vec2(0.0))) ||
        any(greaterThanEqual(uv, vec2(1.0)))) return 1.0;

    int size = int(ubo.sunShadowParams.z);
    ivec2 tile = ivec2(cascade % 2, cascade / 2) * size;
    vec2 pixelPosition = uv * float(size);
    ivec2 pixel = ivec2(pixelPosition);
    float depth = light.z - ubo.sunShadowBias[cascade];
    // 正交投影的三行给出光空间各轴及其缩放。由切平面 n·Δp=0 解出
    // 每个阴影像素偏移对应的深度变化，抑制斜面上邻域比较产生的自阴影条纹。
    vec2 slope = vec2(0.0);
    vec3 depthAxis = vec3(lightProjection[0][2], lightProjection[1][2], lightProjection[2][2]);
    float facing = dot(geometricNormal, normalize(depthAxis));
    if (abs(facing) > 0.13) {
        vec3 xAxis = vec3(lightProjection[0][0], lightProjection[1][0], lightProjection[2][0]);
        vec3 yAxis = vec3(lightProjection[0][1], lightProjection[1][1], lightProjection[2][1]);
        vec2 plane = vec2(dot(geometricNormal, xAxis) / dot(xAxis, xAxis),
                          dot(geometricNormal, yAxis) / dot(yAxis, yAxis));
        slope = -plane * (2.0 * length(depthAxis) / (float(size) * facing));
    }
    // PCF 对每个采样点做深度比较，再加权平均。采样点限制在本级瓦片内。
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 samplePixel = clamp(pixel + ivec2(x, y), ivec2(0), ivec2(size - 1));
            float blocker = texelFetch(sunShadowAtlas, tile + samplePixel, 0).r;
            float receiver = depth + dot(slope, vec2(samplePixel) + 0.5 - pixelPosition);
            visible += step(receiver, blocker) * float((2 - abs(x)) * (2 - abs(y)));
        }
    }
    return visible / 16.0;
}

float sunVisibility(vec3 position, vec3 geometricNormal) {
    if (ubo.sunShadowParams.w == 0.0) return 1.0;
    float depth = -(ubo.view * vec4(position, 1.0)).z;
    vec2 split = ubo.sunShadowParams.xy;
    int cascade = depth < split.x ? 0 : (depth < split.y ? 1 : 2);
    float visibility = sampleSunShadow(position, geometricNormal, cascade);
    if (cascade < 2) {
        // 分级交界处混合两个深度图，避免清晰度突然跳变。
        float blend = smoothstep(split[cascade] * 0.88, split[cascade], depth);
        if (blend > 0.0) {
            visibility = mix(visibility, sampleSunShadow(position, geometricNormal, cascade + 1), blend);
        }
    }
    return visibility;
}
#endif

float sunVisibility(vec3 position) {
    return sunVisibility(position, vec3(0.0));
}
#endif

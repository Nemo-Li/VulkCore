#ifndef VULKCORE_SCENE_FRAME_GLSL
#define VULKCORE_SCENE_FRAME_GLSL

#ifndef VULKCORE_SCENE_FRAME_BINDING
#define VULKCORE_SCENE_FRAME_BINDING 0
#endif

// 与 CPU 的 UniformBuffer.hpp 保持相同的 std140 布局。
layout(set = 0, binding = VULKCORE_SCENE_FRAME_BINDING) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
    vec4 cameraPosition;
    vec4 playerPosition;
    vec4 playerVelocity;
    vec4 distanceFogParams;
    vec4 distanceFogHorizon;
    vec4 distanceFogUpper;
    vec4 daylight; // x 启用晴天散射，y 为云的时间（秒），zw 保留。
    vec4 floatingMist; // x 数量，y 全局浓度，z 时间，w 保留。
    vec4 mistCenters[16]; // xyz 椭球中心，w 单团密度。
    vec4 mistInverseRadii[16]; // xyz 三轴半径的倒数，w 噪声种子。
    vec4 cloudMist; // x 数量，yz 合并后的最低和最高边界，w 保留。
    vec4 cloudCenters[16];
    vec4 cloudInverseRadii[16];
    mat4 sunShadowMatrices[3];
    vec4 sunShadowParams; // xy 视空间分界，z 单级分辨率，w 启用标记。
    vec4 sunShadowBias;
    vec4 grassBladeWidthRange; // xy 草叶底边宽度的最小值和最大值，采用地形单位。
} ubo;

#endif

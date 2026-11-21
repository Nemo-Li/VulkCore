#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"

// 镜面高光调试视图。单独显示半角向量高光，便于观察粗糙度对光斑宽度的影响。

layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(set = 0, binding = 4) uniform sampler2D specularTexture;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 light = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(inverse(ubo.view)[3].xyz - fragWorldPosition);
    vec3 halfway = normalize(light + viewDirection);
    float specular = pow(max(dot(normal, halfway), 0.0), mix(96.0, 12.0, material.roughness));
    // 固定取贴图中心的 R 通道作为全局倍率，以便单独观察光斑本身的角度分布。
    if ((material.textureFlags & 4u) != 0u) specular *= texture(specularTexture, vec2(0.5)).r;
    specular *= sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition));
    outColor = vec4(vec3(specular) * vec3(1.0, 0.78, 0.35), 1.0);
}

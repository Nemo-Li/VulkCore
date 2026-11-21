#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"

// 漫反射材质。用材质、顶点和贴图调制基础色，再乘以环境光和单一方向光。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;

layout(location = 3) in vec3 fragWorldPosition;
layout(set = 0, binding = 1) uniform sampler2D textureSampler;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = material.baseColor * fragVertexColor;
    if ((material.textureFlags & 1u) != 0u) base *= texture(textureSampler, fragTexCoord);
    vec3 light = normalize(ubo.sunLightDirection.xyz);
    // Lambert 余弦项随表面朝向光源而增大，背光面仅保留环境光。
    float diffuse = max(dot(normalize(fragNormal), light), 0.0);
    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 direct = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    outColor = vec4(base.rgb * (ambient + direct * diffuse * sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition))),
        base.a * material.opacity);
}

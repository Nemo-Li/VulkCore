#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"

// Fresnel 风格材质。掠射角处将底色混入明亮的轮廓色。

layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);
    // View 的逆矩阵把相机原点变回场景坐标。N·V 越小，轮廓色的混合权重越大。
    vec3 viewDirection = normalize(inverse(ubo.view)[3].xyz - fragWorldPosition);
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 4.0);
    vec3 light = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    float sunlightVisibility = sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition));
    float diffuse = max(dot(normal, normalize(ubo.sunLightDirection.xyz)), 0.0);
    vec3 color = material.baseColor.rgb * (0.18 + 0.72 * diffuse * sunlightVisibility);
    color = mix(color, light + vec3(0.2, 0.55, 1.0), fresnel * 0.8 * sunlightVisibility);
    color *= fragVertexColor.rgb;
    outColor = vec4(color, material.baseColor.a * material.opacity);
}

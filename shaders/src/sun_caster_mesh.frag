#version 450
#extension GL_GOOGLE_include_directive : require

#define VULKCORE_SHADOW_PASS 1
#include "sun_shadow_coverage.glsl"

// 普通网格的深度投影。复用基础材质的镂空阈值，并为混合材质近似透光覆盖率。
layout(location = 0) in vec2 uv;
layout(location = 2) in vec4 vertexColor;
layout(set = 0, binding = 1) uniform sampler2D baseTexture;
layout(set = 0, binding = 8) uniform sampler2D alphaMask;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float metallic;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} material;

void main() {
    bool water = (material.textureFlags & (1u << 29)) != 0u;
    if (water) {
        // 水面顶点色可携带流向和水体类型，覆盖率取自水材质而非这组元数据。
        writeSunShadowCoverage(0.78 * clamp(material.opacity, 0.0, 1.0));
        return;
    }
    float alpha = material.baseColor.a;
    if ((material.textureFlags & 1u) != 0u) alpha *= texture(baseTexture, uv).a;
    if ((material.textureFlags & (1u << 28)) != 0u &&
        alpha * texture(alphaMask, uv).r < material.waterFlow.x) discard;
    if ((material.textureFlags & (1u << 30)) != 0u) {
        writeSunShadowCoverage(alpha * vertexColor.a * material.opacity);
    }
}

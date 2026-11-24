#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_fog.glsl"
#include "effect_lighting.glsl"
#include "sun_shadow.glsl"

layout(location = 0) in vec2 fragMapUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec2 fragDetailUv;
layout(location = 5) flat in int fragMapLayer;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 6) uniform sampler2DArray albedoLayers;
layout(set = 0, binding = 7) uniform sampler2DArray materialMaps;
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

vec3 sampleAlbedo(uint layer, vec2 dx, vec2 dy) {
    return textureGrad(albedoLayers, vec3(fragDetailUv, float(layer)), dx, dy).rgb;
}

vec3 sampleMaterial(ivec2 coordinate, vec2 dx, vec2 dy) {
    uvec3 sampleBytes = uvec3(round(texelFetch(materialMaps, ivec3(coordinate, fragMapLayer), 0).rgb * 255.0));
    if (sampleBytes.z == 255u || sampleBytes.x == sampleBytes.y) return sampleAlbedo(sampleBytes.x, dx, dy);
    if (sampleBytes.z == 0u) return sampleAlbedo(sampleBytes.y, dx, dy);
    vec3 first = sampleAlbedo(sampleBytes.x, dx, dy);
    vec3 second = sampleAlbedo(sampleBytes.y, dx, dy);
    return mix(second, first, float(sampleBytes.z) / 255.0);
}

void main() {
    vec2 dx = dFdx(fragDetailUv);
    vec2 dy = dFdy(fragDetailUv);
    ivec2 last = textureSize(materialMaps, 0).xy - 1;
    vec2 grid = clamp(fragMapUv, 0.0, 1.0) * vec2(last);
    ivec2 low = ivec2(grid);
    ivec2 high = min(low + 1, last);
    vec2 fraction = fract(grid);

    // Blend each categorical sample before applying spatial interpolation.
    vec3 row0 = mix(sampleMaterial(low, dx, dy), sampleMaterial(ivec2(high.x, low.y), dx, dy), fraction.x);
    vec3 row1 = mix(sampleMaterial(ivec2(low.x, high.y), dx, dy), sampleMaterial(high, dx, dy), fraction.x);
    vec3 albedo = mix(row0, row1, fraction.y);

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float visibility = sunVisibility(fragWorldPosition, normal);
    vec3 lighting = ubo.ambientLight.rgb * ubo.ambientLight.a +
        ubo.sunLightColor.rgb * ubo.sunLightDirection.w * diffuse * visibility +
        effectPointLighting(fragWorldPosition, normal);
    outColor = vec4(applySceneFog(albedo * lighting * material.baseColor.rgb,
        sceneFogAt(fragWorldPosition, true)), material.opacity);
}

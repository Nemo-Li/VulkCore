#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragBladeUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) flat in vec4 fragTint;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) flat in float fragLodVisibility;
layout(location = 5) flat in vec2 fragRoot;
layout(location = 6) flat in float fragLod;
layout(location = 7) flat in float fragColorVariation;

#include "scene_fog.glsl"
#include "meadow_shading.glsl"

layout(set = 0, binding = 1) uniform sampler2D textureSampler;

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

layout(location = 0) out vec4 outColor;

float bladeDither(vec2 root) {
    return fract(sin(dot(root, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    // Dissolve whole blades instead of pixels. The simultaneous vertical sink
    // in the vertex shader keeps the transition soft without screen-door
    // stipple or alpha-sorting artifacts.
    float dither = bladeDither(fragRoot);
    if (fragLodVisibility <= dither) {
        discard;
    }

    bool nearBlade = fragLod < 0.5;
    float bladeHeight = clamp(nearBlade ? fragBladeUv.y : 1.0 - fragBladeUv.y, 0.0, 1.0);
    float variation = fragColorVariation;
    vec3 albedo;
    if (nearBlade) {
        vec3 tint = kMeadowAlbedo;
        if (fragTint.a > 0.5) {
            // Merge imported tint into the ground palette before the mesh begins to fade.
            vec3 rgb = fragTint.rgb;
            vec3 linearTint = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)),
                                  greaterThan(rgb, vec3(0.04045)));
            float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
            float distanceWorld = distance(fragWorldPosition.xz, ubo.cameraPosition.xz) / sceneScale;
            tint = mix(tint, linearTint, 1.0 - smoothstep(14.0, 29.0, distanceWorld));
        }
        float canopyShade = mix(0.43, 1.27, smoothstep(0.0, 0.88, bladeHeight));
        albedo = tint * canopyShade * mix(0.89, 1.11, variation);
    } else {
        if ((material.textureFlags & 1u) == 0u) discard;
        vec4 flower = texture(textureSampler, fragBladeUv);
        if (flower.a < 0.54) discard;
        albedo = flower.rgb * mix(0.94, 1.04, variation);
    }

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(ubo.cameraPosition.xyz - fragWorldPosition);
    float facing = dot(normal, lightDirection);
    float diffuse = abs(facing);
    float flowerDiffuse = diffuse < 0.32 ? 0.72 : (diffuse < 0.67 ? 0.91 : 1.12);
    float diffuseBand = nearBlade ? meadowDiffuse(facing) : flowerDiffuse;
    float backLight = pow(
        max(dot(viewDirection, -lightDirection), 0.0),
        4.0
    ) * (0.1 + bladeHeight * 0.14);
    float rim = pow(1.0 - abs(dot(normal, viewDirection)), 2.5) * 0.065;

    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) *
        max(ubo.ambientLight.a, 0.0);
    vec3 sunlight = max(ubo.sunLightColor.rgb, vec3(0.0)) *
        max(ubo.sunLightDirection.w, 0.0);
    vec3 lighting = ambient * (nearBlade ? 1.0 : 0.72) +
        sunlight * (diffuseBand + (backLight + rim) * (nearBlade ? 0.35 : 1.0));
    vec3 finalColor = albedo * lighting * material.baseColor.rgb;
    if (!nearBlade) finalColor += vec3(0.045, 0.034, 0.004) * bladeHeight * backLight;
    outColor = vec4(applySceneFog(finalColor, sceneFogAt(fragWorldPosition)), material.opacity);
}

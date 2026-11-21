#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragBladeUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) flat in vec4 fragTint;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) flat in float fragLodVisibility;
layout(location = 5) flat in vec2 fragRoot;
layout(location = 6) flat in float fragLod;
layout(location = 7) flat in vec2 fragPalette;

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

vec3 yellowFlower(vec2 uv) {
    // The same small cup-shaped blossom is drawn on each crossed card.
    vec2 head = (uv - vec2(0.5, 0.924)) * vec2(1.0, 10.2);
    float centerPetal = length((head - vec2(0.015, 0.18)) / vec2(0.19, 0.24));
    vec2 outer = vec2(abs(head.x) - 0.21, head.y - 0.085);
    outer = mat2(0.80, -0.60, 0.60, 0.80) * outer;
    float outerPetals = length(outer / vec2(0.27, 0.165));
    float cup = length((head + vec2(0.0, 0.075)) / vec2(0.27, 0.165));
    if (min(min(centerPetal, outerPetals), cup) < 1.0) {
        float petalLight = smoothstep(-0.18, 0.32, head.y);
        return mix(vec3(0.54, 0.40, 0.035), vec3(0.70, 0.58, 0.11), petalLight);
    }
    float stem = 0.5 + 0.075 * sin(uv.y * 3.14159);
    if (uv.y < 0.922 && abs(uv.x - stem) < mix(0.030, 0.013, uv.y)) {
        return vec3(0.24, 0.39, 0.074);
    }
    vec2 leaf = (uv - vec2(0.61, 0.68)) * vec2(1.0, 4.5);
    leaf = mat2(0.7071, -0.7071, 0.7071, 0.7071) * leaf;
    if (length(leaf / vec2(0.25, 0.045)) < 1.0) return vec3(0.29, 0.45, 0.083);
    discard;
    return vec3(0.0);
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
    bool smallYellowFlower = fragLod > 3.5; // GrassLod::YellowFlowerCard (4)
    float bladeHeight = clamp(fragBladeUv.y, 0.0, 1.0);
    float variation = fragPalette.x;
    vec3 viewRay = ubo.cameraPosition.xyz - fragWorldPosition;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    float distanceWorld = length(viewRay.xz) / sceneScale;
    float nearPaletteWeight = 1.0 - smoothstep(14.0, 29.0, distanceWorld);
    vec3 albedo;
    if (nearBlade) {
        float coolGreen = smoothstep(0.74, 0.97, fragPalette.y);
        vec3 leafTint = vec3(1.23, 1.24, 0.96) * mix(vec3(1.0), vec3(0.83, 1.0, 0.54), coolGreen);
        vec3 tint = kMeadowAlbedo * mix(vec3(1.0), leafTint, nearPaletteWeight);
        if (fragTint.a > 0.5) {
            // Merge imported tint into the ground palette before the mesh begins to fade.
            vec3 rgb = fragTint.rgb;
            vec3 linearTint = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)),
                                  greaterThan(rgb, vec3(0.04045)));
            tint = mix(tint, linearTint, nearPaletteWeight);
        }
        float heightGradient = smoothstep(0.03, 0.94, bladeHeight);
        vec3 canopyTint = mix(vec3(0.75, 0.92, 0.78), vec3(1.10, 1.07, 1.08), heightGradient);
        float widthShade = mix(0.96, 1.02, fragBladeUv.x);
        albedo = tint * canopyTint * widthShade * mix(0.86, 1.13, variation);
        if ((material.textureFlags & 1u) != 0u) {
            vec4 blade = texture(textureSampler, vec2(fragBladeUv.x, 1.0 - bladeHeight));
            if (blade.a < 0.5) discard;
            albedo *= blade.rgb;
        }
    } else if (smallYellowFlower) {
        albedo = yellowFlower(fragBladeUv) * mix(0.97, 1.03, variation);
    } else {
        if ((material.textureFlags & 1u) == 0u) discard;
        vec4 flower = texture(textureSampler, vec2(fragBladeUv.x, 1.0 - bladeHeight));
        if (flower.a < 0.54) discard;
        albedo = flower.rgb * mix(0.94, 1.04, variation);
    }

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(viewRay);
    float facing = dot(normal, lightDirection);
    float flowerFacing = abs(facing);
    float flowerDiffuse = smallYellowFlower
        ? mix(0.63, 0.98, smoothstep(0.20, 0.84, flowerFacing))
        : (flowerFacing < 0.32 ? 0.72 : (flowerFacing < 0.67 ? 0.91 : 1.12));
    float diffuseBand = nearBlade ? mix(0.22, 0.78, meadowDiffuse(facing)) : flowerDiffuse;
    float backLight = pow(
        max(dot(viewDirection, -lightDirection), 0.0),
        4.0
    ) * (0.1 + bladeHeight * 0.14);
    float rim = pow(1.0 - abs(dot(normal, viewDirection)), 2.5) * 0.065;

    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) *
        max(ubo.ambientLight.a, 0.0);
    vec3 sunlight = max(ubo.sunLightColor.rgb, vec3(0.0)) *
        max(ubo.sunLightDirection.w, 0.0) * cloudSunVisibility(fragWorldPosition);
    vec3 lighting = ambient * (nearBlade ? 1.0 : 0.72) +
        sunlight * (diffuseBand + (backLight + rim) * (nearBlade ? 0.35 : 1.0));
    vec3 finalColor = albedo * lighting;
    if (nearBlade) {
        // Scattered fill in the canopy softens the silhouette contrast beyond the near leaves.
        float canopyFill = 0.43 * smoothstep(2.5, 6.8, distanceWorld) * nearPaletteWeight;
        canopyFill *= 1.0 - step(0.5, fragTint.a);
        if ((material.textureFlags & 1u) != 0u) canopyFill = 0.0;
        vec3 scattered = vec3(0.42, 0.58, 0.18) * (ambient + sunlight * 0.42);
        finalColor = mix(finalColor, scattered, canopyFill);
    } else {
        finalColor += vec3(0.045, 0.034, 0.004) * bladeHeight * backLight;
    }
    outColor = vec4(applySceneFog(finalColor * material.baseColor.rgb,
        sceneFogAt(fragWorldPosition)), material.opacity);
}

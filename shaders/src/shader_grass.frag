#version 450
#extension GL_GOOGLE_include_directive : require

// 草叶与花卡的片元阶段。按实例渐隐，用草甸共享色调、透光近似和场景雾为植被着色。

layout(location = 0) in vec2 fragBladeUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPosition;
layout(location = 3) flat in float fragLod;
layout(location = 4) flat in float fragVariation;
layout(location = 5) flat in vec4 fragMapColor;

#include "scene_fog.glsl"
#include "effect_lighting.glsl"
#include "meadow_shading.glsl"
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 4) uniform sampler2D grassSpecular;
layout(set = 0, binding = 6) uniform sampler2DArray grassCrossAlbedo;
layout(set = 0, binding = 9) uniform sampler2D grassBladeAlbedo;

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

SUN_SHADOW_OUTPUT

vec3 yellowFlower(vec2 uv) {
    // 在交叉卡片的局部 UV 中，用拉伸、旋转的椭圆拼出花瓣、花托和叶子，曲线阈值裁出茎。
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
    vec3 shadowNormal = sunReceiverNormal(fragWorldPosition);
    bool nearBlade = fragLod < 0.5;
    bool crossCard = fragLod > 0.5 && fragLod < 1.5;
    bool smallYellowFlower = fragLod > 3.5; // GrassLod::YellowFlowerCard
    float bladeHeight = clamp(fragBladeUv.y, 0.0, 1.0);
    vec2 uv = vec2(fragBladeUv.x, 1.0 - bladeHeight);
    float variation = fragVariation;
    vec3 viewRay = ubo.cameraPosition.xyz - fragWorldPosition;
    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(viewRay);
    float facing = dot(normal, lightDirection);
    // 看向光源时增强透光，且叶尖更亮。掠视两面都获得少量轮廓光。
    float backLight = pow(max(dot(viewDirection, -lightDirection), 0.0), 4.0) * (0.1 + bladeHeight * 0.14);
    float rim = pow(1.0 - abs(dot(normal, viewDirection)), 2.5) * 0.065;
    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 sunlight = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);

    if (nearBlade || crossCard) {
        vec3 albedo;
        if (crossCard) {
            // Each tuft's two cards share the same variety and alpha cutoff.
            vec4 sampleColor = texture(grassCrossAlbedo, vec3(uv, step(0.5, variation)));
            if (sampleColor.a < 0.45) discard;
            albedo = sampleColor.rgb;
        } else {
            albedo = texture(grassBladeAlbedo, uv).rgb;
        }
        if (fragMapColor.a > 0.0) {
            // The map sets the RGB for both LODs. Texture luminance preserves
            // leaf detail and the root-to-tip gradient gives the blades depth.
            float textureLuma = dot(albedo, vec3(0.2126, 0.7152, 0.0722));
            float leafDetail = mix(0.78, 1.22, smoothstep(0.0, 0.55, textureLuma));
            float rootShade = mix(0.78, 1.05, bladeHeight);
            albedo = fragMapColor.rgb * (leafDetail * rootShade);
        }

        vec3 groundFacingNormal = dot(shadowNormal, normal) < 0.0 ? -shadowNormal : shadowNormal;
        sunlight *= sunVisibility(fragWorldPosition, normalize(mix(normal, groundFacingNormal, 0.23)));
        ambient += effectPointLighting(fragWorldPosition, normal);
        float diffuse = mix(0.25, 0.84, meadowDiffuse(facing));
        vec3 halfDirection = normalize(lightDirection + viewDirection);
        float specularMask = texture(grassSpecular, vec2(crossCard ? 0.5 : uv.x, uv.y)).r;
        float specular = specularMask * pow(max(dot(normal, halfDirection), 0.0), 28.0) * 0.052;
        vec3 lighting = ambient + sunlight * (diffuse + (backLight + rim) * 0.35);
        vec3 color = albedo * kMeadowTextureExposure * material.baseColor.rgb * lighting + sunlight * specular;
        SUN_SHADOW_COLOR(vec4(applySceneFog(color, sceneFogAt(fragWorldPosition, true)), material.opacity));
        return;
    }

    vec3 albedo;
    if (smallYellowFlower) {
        albedo = yellowFlower(fragBladeUv) * mix(0.97, 1.03, variation);
    } else {
        if ((material.textureFlags & 1u) == 0u) discard;
        vec4 flower = texture(textureSampler, uv);
        if (flower.a < 0.54) discard;
        albedo = flower.rgb * mix(0.94, 1.04, variation);
    }
    float flowerFacing = abs(facing);
    float flowerDiffuse = smallYellowFlower
        ? mix(0.63, 0.98, smoothstep(0.20, 0.84, flowerFacing))
        : (flowerFacing < 0.32 ? 0.72 : (flowerFacing < 0.67 ? 0.91 : 1.12));
    sunlight *= sunVisibility(fragWorldPosition, shadowNormal);
    vec3 lighting = ambient * 0.72 + sunlight * (flowerDiffuse + backLight + rim) +
        effectPointLighting(fragWorldPosition, normal);
    vec3 finalColor = albedo * lighting + vec3(0.045, 0.034, 0.004) * bladeHeight * backLight;
    SUN_SHADOW_COLOR(vec4(applySceneFog(finalColor * material.baseColor.rgb,
        sceneFogAt(fragWorldPosition)), material.opacity));
}

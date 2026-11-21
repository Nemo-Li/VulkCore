#version 450
#extension GL_GOOGLE_include_directive : require

// 草叶与花卡的片元阶段。按实例渐隐，用草甸共享色调、透光近似和场景雾为植被着色。

layout(location = 0) in vec2 fragBladeUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) flat in vec4 fragTint;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) flat in float fragLodVisibility;
layout(location = 5) flat in vec2 fragRoot;
layout(location = 6) flat in float fragLod;
layout(location = 7) flat in vec2 fragPalette;
layout(location = 8) in vec2 fragGroundUv;
layout(location = 9) in vec3 fragGroundPosition;
layout(location = 10) flat in vec3 fragGroundNormal;

#include "scene_fog.glsl"
#include "meadow_shading.glsl"
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

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

SUN_SHADOW_OUTPUT

float bladeDither(vec2 root) {
    return fract(sin(dot(root, vec2(12.9898, 78.233))) * 43758.5453);
}

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
    // 根位置的哈希为整株提供固定阈值，随相机移动只改变可见株数。
    // 与顶点下沉配合，保持保留下来的草叶完整，并避免逐像素网点和半透明排序。
    float dither = bladeDither(fragRoot);
    if (fragLodVisibility <= dither) {
        discard;
    }

    bool nearBlade = fragLod < 0.5;
    bool smallYellowFlower = fragLod > 3.5; // 对应 GrassLod::YellowFlowerCard（4）。
    float bladeHeight = clamp(fragBladeUv.y, 0.0, 1.0);
    float variation = fragPalette.x;
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

    if (nearBlade) {
        vec3 bladeTexture = vec3(1.0);
        if ((material.textureFlags & 1u) != 0u) {
            vec4 blade = texture(textureSampler, vec2(fragBladeUv.x, 1.0 - bladeHeight));
            if (blade.a < 0.5) discard;
            bladeTexture = blade.rgb;
        }
        float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
        float groundDistance = length((fragGroundPosition - ubo.cameraPosition.xyz).xz) / sceneScale;
        float detail = meadowBladeDetail(groundDistance, material.waterFlow.z);
        vec3 groundNormal = normalize(fragGroundNormal);
        vec3 shadedPosition = mix(fragGroundPosition, fragWorldPosition, detail);
        vec3 alignedShadowNormal = dot(shadowNormal, groundNormal) < 0.0 ? -shadowNormal : shadowNormal;
        vec3 receiverNormal = normalize(mix(groundNormal, alignedShadowNormal, detail));
        sunlight *= sunVisibility(shadedPosition, receiverNormal);
        vec3 color = vec3(0.0);
        if (detail > 0.0) {
            vec3 tint = vec3(0.205, 0.386, 0.058);
            if (fragTint.a > 0.5) {
                // 将导入色解码至线性 RGB，保留地图给草叶指定的局部色相。
                vec3 rgb = fragTint.rgb;
                vec3 linearTint = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)),
                                      greaterThan(rgb, vec3(0.04045)));
                tint = mix(tint, linearTint * vec3(0.68, 0.77, 0.57), 0.65);
            }
            float coolGreen = smoothstep(0.74, 0.97, fragPalette.y);
            tint *= mix(vec3(1.0), vec3(0.81, 0.93, 0.65), coolGreen);
            float tipLight = smoothstep(0.03, 0.94, bladeHeight);
            float shade = mix(0.76, 1.12, tipLight) * mix(0.82, 1.16, variation) * mix(0.96, 1.02, fragBladeUv.x);
            float diffuse = mix(0.22, 0.78, meadowDiffuse(facing));
            vec3 lighting = ambient + sunlight * (diffuse + (backLight + rim) * 0.35);
            color += detail * tint * shade * bladeTexture * material.baseColor.rgb * lighting;
        }
        if (detail < 1.0) {
            vec3 groundAlbedo = (material.textureFlags & kGrassGroundTexture) != 0u
                ? meadowGroundAlbedo(fragGroundUv, groundDistance, material.waterFlow.zw) : kMeadowAlbedo;
            vec3 groundLight = meadowGroundLighting(dot(groundNormal, lightDirection), ambient, sunlight);
            color += (1.0 - detail) * meadowGroundTint(groundAlbedo, detail) * groundLight;
        }
        SUN_SHADOW_COLOR(vec4(applySceneFog(color, sceneFogAt(shadedPosition, true)), material.opacity));
        return;
    }

    vec3 albedo;
    if (smallYellowFlower) {
        albedo = yellowFlower(fragBladeUv) * mix(0.97, 1.03, variation);
    } else {
        if ((material.textureFlags & 1u) == 0u) discard;
        vec4 flower = texture(textureSampler, vec2(fragBladeUv.x, 1.0 - bladeHeight));
        if (flower.a < 0.54) discard;
        albedo = flower.rgb * mix(0.94, 1.04, variation);
    }
    float flowerFacing = abs(facing);
    float flowerDiffuse = smallYellowFlower
        ? mix(0.63, 0.98, smoothstep(0.20, 0.84, flowerFacing))
        : (flowerFacing < 0.32 ? 0.72 : (flowerFacing < 0.67 ? 0.91 : 1.12));
    sunlight *= sunVisibility(fragWorldPosition, shadowNormal);
    vec3 lighting = ambient * 0.72 + sunlight * (flowerDiffuse + backLight + rim);
    vec3 finalColor = albedo * lighting + vec3(0.045, 0.034, 0.004) * bladeHeight * backLight;
    SUN_SHADOW_COLOR(vec4(applySceneFog(finalColor * material.baseColor.rgb,
        sceneFogAt(fragWorldPosition)), material.opacity));
}

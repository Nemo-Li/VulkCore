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

    float bladeHeight = fragLod < 0.5
        ? clamp(fragBladeUv.y, 0.0, 1.0)
        : clamp(1.0 - fragBladeUv.y, 0.0, 1.0);
    float variation = fragColorVariation;
    vec3 rootA = vec3(0.055, 0.17, 0.018);
    vec3 rootB = vec3(0.085, 0.235, 0.026);
    vec3 tipA = vec3(0.27, 0.52, 0.055);
    vec3 tipB = vec3(0.39, 0.63, 0.082);
    vec3 rootColor = mix(rootA, rootB, variation);
    vec3 tipColor = mix(tipA, tipB, variation);
    vec3 albedo = mix(rootColor, tipColor, smoothstep(0.0, 0.92, bladeHeight));
    if (fragLod >= 0.5 && (material.textureFlags & 1u) != 0u) {
        vec4 vegetationSample = texture(textureSampler, fragBladeUv);
        // Generated cutouts intentionally retain soft antialiasing. A firm
        // threshold removes matte/glow pixels and keeps the Opaque depth path.
        if (vegetationSample.a < 0.54) {
            discard;
        }
        if (fragLod < 2.5) {
            vec3 meadowTint = mix(rootColor, tipColor, 0.7);
            albedo = mix(vegetationSample.rgb, meadowTint, 0.42) *
                mix(0.94, 1.08, variation);
        } else {
            albedo = vegetationSample.rgb * mix(0.94, 1.04, variation);
        }
    }

    if (fragTint.a > 0.5) {
        // Use the tile RGB as the mapped blade tint. The card texture supplies
        // shape and neutral detail, so its own green does not override the map.
        vec3 rgb = fragTint.rgb;
        vec3 linearTint = mix(rgb / 12.92, pow((rgb + 0.055) / 1.055, vec3(2.4)),
                              greaterThan(rgb, vec3(0.04045)));
        float detail = smoothstep(0.0, 0.92, bladeHeight) * 0.60 + 0.45;
        albedo = linearTint * detail * mix(0.9, 1.1, variation);
    }

    vec3 normal = normalize(fragNormal);
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(ubo.cameraPosition.xyz - fragWorldPosition);
    float diffuse = abs(dot(normal, lightDirection));
    float diffuseBand = diffuse < 0.32 ? 0.72 : (diffuse < 0.67 ? 0.91 : 1.12);
    float backLight = pow(
        max(dot(viewDirection, -lightDirection), 0.0),
        4.0
    ) * (0.1 + bladeHeight * 0.14);
    float rim = pow(1.0 - abs(dot(normal, viewDirection)), 2.5) * 0.065;

    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) *
        max(ubo.ambientLight.a, 0.0);
    vec3 sunlight = max(ubo.sunLightColor.rgb, vec3(0.0)) *
        max(ubo.sunLightDirection.w, 0.0);
    vec3 lighting = ambient * 0.72 + sunlight * (diffuseBand + backLight + rim);
    vec3 finalColor = albedo * lighting * material.baseColor.rgb;
    // Warm tips are a characteristic part of BOTW's readable grass mass.
    finalColor += mix(vec3(0.045, 0.034, 0.004), albedo * 0.1, fragTint.a) * bladeHeight * backLight;
    outColor = vec4(applySceneFog(finalColor, sceneFogAt(fragWorldPosition)), material.opacity);
}

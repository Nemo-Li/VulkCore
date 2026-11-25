#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_fog.glsl"
#include "effect_lighting.glsl"
#include "sun_shadow.glsl"
#include "terrain_rock.glsl"
#include "meadow_shading.glsl"
#ifndef VULKCORE_OHOS
#include "terrain_road_weights.glsl"
#endif

layout(location = 0) in vec2 fragMapUv;
layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec3 fragTerrainPosition;
layout(location = 5) flat in int fragMapLayer;
layout(location = 6) in vec2 fragMacroUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D macroAlbedo;
layout(set = 0, binding = 3) uniform sampler2D rockCoverage;
layout(set = 0, binding = 6) uniform sampler2DArray albedoLayers;
layout(set = 0, binding = 7) uniform sampler2DArray materialMaps;
layout(set = 0, binding = 9) uniform sampler2D grassGroundCover;
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

#ifdef VULKCORE_OHOS
const bool mobileTerrain = true;
#else
const bool mobileTerrain = false;
#endif

const float detailTileSize = 4.0;

struct TerrainProjection {
    vec3 uv;
    vec3 dx;
    vec3 dy;
    vec3 weights;
};

vec3 sampleAlbedo(uint layer, TerrainProjection projection) {
    float slice = float(layer);
    vec3 color = vec3(0.0);
    if (projection.weights.x > 0.0) {
        color += projection.weights.x *
            textureGrad(albedoLayers, vec3(projection.uv.yz, slice), projection.dx.yz, projection.dy.yz).rgb;
    }
    if (projection.weights.y > 0.0) {
        color += projection.weights.y *
            textureGrad(albedoLayers, vec3(projection.uv.xz, slice), projection.dx.xz, projection.dy.xz).rgb;
    }
    if (projection.weights.z > 0.0) {
        color += projection.weights.z *
            textureGrad(albedoLayers, vec3(projection.uv.xy, slice), projection.dx.xy, projection.dy.xy).rgb;
    }
    return color;
}

vec3 sampleMaterial(ivec2 coordinate, TerrainProjection projection) {
    uvec3 sampleBytes = uvec3(round(texelFetch(materialMaps, ivec3(coordinate, fragMapLayer), 0).rgb * 255.0));
    if (sampleBytes.z == 255u || sampleBytes.x == sampleBytes.y) return sampleAlbedo(sampleBytes.x, projection);
    if (sampleBytes.z == 0u) return sampleAlbedo(sampleBytes.y, projection);
    vec3 first = sampleAlbedo(sampleBytes.x, projection);
    vec3 second = sampleAlbedo(sampleBytes.y, projection);
    return mix(second, first, float(sampleBytes.z) / 255.0);
}

vec3 sampleDetail(TerrainProjection projection) {
    ivec2 interior = textureSize(materialMaps, 0).xy - 2;
    // Base samples are texel centers. At a shared edge both sides interpolate
    // the same two columns, one of which lives in the neighbor gutter.
    vec2 grid = clamp(fragMapUv, 0.0, 1.0) * vec2(interior) + 0.5;
    ivec2 low = ivec2(grid);
    ivec2 high = low + 1;
    vec2 fraction = fract(grid);

    // Blend each categorical sample before applying spatial interpolation.
    vec3 row0 = mix(sampleMaterial(low, projection), sampleMaterial(ivec2(high.x, low.y), projection), fraction.x);
    vec3 row1 = mix(sampleMaterial(ivec2(low.x, high.y), projection), sampleMaterial(high, projection), fraction.x);
    return mix(row0, row1, fraction.y);
}

void main() {
    vec3 dx = dFdx(fragTerrainPosition);
    vec3 dy = dFdy(fragTerrainPosition);
    vec2 macroDx = dFdx(fragMacroUv);
    vec2 macroDy = dFdy(fragMacroUv);
    float worldDistance = distance(fragWorldPosition, ubo.cameraPosition.xyz) / ubo.cameraPosition.w;
    float footprint = max(length(dx.xz), length(dy.xz));
    float macroBlend = max(smoothstep(95.0, 340.0, worldDistance), smoothstep(2.5, 8.0, footprint));
    // A 1x1 white texture marks an unavailable macro map. Coverage is kept
    // in alpha so gaps in a terrain pack do not bleed black through the mips.
    vec4 macro = texture(macroAlbedo, fragMacroUv);
    if (textureSize(macroAlbedo, 0).x < 2 || macro.a < 1.0 / 255.0) macroBlend = 0.0;
    else if (mobileTerrain) macroBlend = 1.0;
    vec3 albedo = macro.rgb / max(macro.a, 1.0 / 255.0);

    float nearBlend = 1.0 - macroBlend;
    float middleBlend = smoothstep(0.2, 0.95, macroBlend);
    float strata = mobileTerrain ? 0.0 : middleBlend * rockBandFade(rockStrataFrequency, dx, dy);
    float chips = mobileTerrain ? 0.0 : middleBlend * rockBandFade(rockChipsFrequency, dx, dy);
    vec4 grass = vec4(0.0);
    if (material.waterSurface.x > 0.0) grass = textureGrad(grassGroundCover, fragMacroUv, macroDx, macroDy);
    float rock = 0.0;
    if (max(grass.a, max(nearBlend, max(strata, chips))) > 0.0 && textureSize(rockCoverage, 0).x >= 2) {
        vec4 coverage = textureGrad(rockCoverage, fragMacroUv, macroDx, macroDy);
        rock = clamp(coverage.r / max(coverage.a, 1.0 / 255.0), 0.0, 1.0);
    }

    vec3 geometricNormal = normalize(fragNormal);
    if (nearBlend > 0.0) {
        TerrainProjection projection = TerrainProjection(
            fragTerrainPosition / detailTileSize, dx / detailTileSize, dy / detailTileSize,
            rockProjectionWeights(geometricNormal, rock));
        albedo = mix(albedo, sampleDetail(projection), nearBlend);
    }

    vec3 gradient = vec3(0.0);
    float ambientVisibility = 1.0;
    if (!mobileTerrain && rock > 0.0) {
        float grain = nearBlend * rockBandFade(vec3(rockGrainFrequency), dx, dy);
        if (grain > 0.0) {
            gradient += 0.32 * grain * rock * rockGrainGradient(fragTerrainPosition * rockGrainFrequency,
                rockProjectionWeights(geometricNormal, 1.0));
        }
        if (strata > 0.0) {
            applyRockBand(rockNoise3D(fragTerrainPosition * rockStrataFrequency),
                rockStrataFrequency, rock * strata, vec3(1.25, 0.34, 0.18), gradient, albedo, ambientVisibility);
        }
        if (chips > 0.0) {
            applyRockBand(rockNoise3D(fragTerrainPosition * rockChipsFrequency + vec3(36.2, -9.3, 15.8)),
                rockChipsFrequency, rock * chips, vec3(0.55, 0.39, 0.10), gradient, albedo, ambientVisibility);
        }
    }

    // Apply the height gradients along the surface. Shadow sampling uses
    // the mesh normal so fine detail cannot displace or bias the shadow.
    vec3 normal = normalize(geometricNormal - (gradient - geometricNormal * dot(gradient, geometricNormal)));
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float visibility = mobileTerrain ? 1.0 : sunVisibility(fragWorldPosition, geometricNormal);
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a;
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * visibility;
    vec3 pointLight = effectPointLighting(fragWorldPosition, normal);
    vec3 groundColor = albedo * (ambient * ambientVisibility + sunlight * diffuse + pointLight);
    if (grass.a > 1.0 / 255.0) {
        // Match the cards as they shrink, then transition to the native ground texture.
        float distanceBlend = smoothstep(material.waterFlow.x * 0.65, material.waterFlow.z, worldDistance);
        float farBlend = smoothstep(material.waterFlow.w, material.waterFlow.w * 2.8, worldDistance);
        float rockMask = smoothstep(0.15, 0.75, rock);
        float canopy = mix(mix(0.20, 0.90, distanceBlend), 0.08, farBlend) * (1.0 - rockMask);
#ifndef VULKCORE_OHOS
        vec3 roads = terrainRoadWeights(fragTerrainPosition.xz);
        float roadMask = clamp(dot(roads, vec3(material.metallic, 1.0, 1.0)), 0.0, 1.0);
        canopy *= 1.0 - roadMask;
#endif
        vec3 canopyLight = ambient + sunlight * meadowCanopyDiffuse(geometricNormal, lightDirection) + pointLight;
        groundColor = groundColor * (1.0 - canopy * grass.a) +
            meadowCanopyAlbedo(grass.rgb) * canopyLight * canopy;
    }
    vec3 cameraRay = fragWorldPosition - ubo.cameraPosition.xyz;
    vec4 fog = mobileTerrain
        ? distanceFogAt(cameraRay / max(length(cameraRay), 1e-8), worldDistance, true)
        : sceneFogAt(fragWorldPosition, true);
    outColor = vec4(applySceneFog(groundColor * material.baseColor.rgb, fog), material.opacity);
}

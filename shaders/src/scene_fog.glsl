// Matches include/vulkcore/Renderer/UniformBuffer.hpp.
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
    vec4 cameraPosition;
    vec4 playerPosition;
    vec4 playerVelocity;
    vec4 distanceFogParams;
    vec4 distanceFogHorizon;
    vec4 distanceFogUpper;
    vec4 daylight; // enabled, cloud time in seconds, reserved, reserved
    vec4 floatingMist; // count, opacity, time, reserved
    vec4 mistCenters[16];
    vec4 mistInverseRadii[16];
} ubo;

#include "daylight_atmosphere.glsl"
#include "daylight_clouds.glsl"

vec4 floatingMistAt(vec3 direction, float distance) {
    int count = int(ubo.floatingMist.x);
    if (count == 0 || distance <= 65.0) return vec4(0.0);
    vec3 camera = daylightWorldPosition(ubo.cameraPosition.xyz);
    vec2 drift = ubo.floatingMist.z * vec2(0.014, -0.006);
    float transmission = 1.0;
    for (int i = 0; i < count; ++i) {
        vec4 center = ubo.mistCenters[i];
        vec4 shape = ubo.mistInverseRadii[i];
        // Transform the camera ray into the puff's unit sphere. The surface
        // distance clips its visible path so hills and models occlude the mist.
        vec3 origin = (camera - center.xyz) * shape.xyz;
        vec3 step = direction * shape.xyz;
        float speedSquared = dot(step, step);
        float middle = -dot(origin, step) / speedSquared;
        if (middle <= 0.0) continue;
        vec3 closest = origin + step * middle;
        float edge = 1.0 - dot(closest, closest);
        if (edge <= 0.0) continue;
        float halfPath = sqrt(edge / speedSquared);
        float fraction = clamp((distance - middle + halfPath) / (2.0 * halfPath), 0.0, 1.0);
        if (fraction <= 0.0) continue;

        vec3 samplePoint = origin + step * min(middle, distance);
        vec2 noisePosition = samplePoint.xz * vec2(2.2, 2.8) + samplePoint.y * vec2(1.9, -1.2) + shape.w;
        float billows = 0.72 * cloudNoise(noisePosition + drift) +
            0.28 * cloudNoise(noisePosition * 2.63 - drift - vec2(4.5, 9.4));
        float profile = smoothstep(0.0, 0.58, edge) * smoothstep(0.22, 0.74, billows);
        float opacity = profile * smoothstep(0.0, 1.0, fraction) * center.w * ubo.floatingMist.y;
        transmission *= 1.0 - opacity;
    }
    vec3 tint = vec3(0.76, 0.84, 0.86);
    float nearFade = smoothstep(65.0, 120.0, distance);
    return vec4(tint, (1.0 - transmission) * nearFade);
}

vec4 distanceFogAt(vec3 direction, float distance, bool terrainFallback) {
    if (terrainFallback && ubo.distanceFogParams.w == 0.0) {
        // Retain the original terrain haze for scenes saved without a fog preset.
        float haze = 1.0 - exp2(-max(distance - 125.0, 0.0) / 1350.0);
        haze *= mix(0.58, 0.34, smoothstep(0.02, 0.48, direction.y));
        return vec4(0.19, 0.34, 0.48, haze);
    }
    float fogDistance = distance - ubo.distanceFogParams.x;
    if (fogDistance <= 0.0 || ubo.distanceFogParams.z <= 0.0) return vec4(0.0);

    // Rays pointing above the horizon pass through less low-altitude haze.
    float upper = smoothstep(0.02, 0.48, direction.y);
    vec3 tint = mix(ubo.distanceFogHorizon.rgb, ubo.distanceFogUpper.rgb, upper);
    float opticalDepth = (fogDistance / ubo.distanceFogParams.y) * mix(1.0, 0.65, upper);
    if (ubo.daylight.x > 0.0) {
        float pathHeight = daylightWorldPosition(ubo.cameraPosition.xyz).y +
            direction.y * (distance + ubo.distanceFogParams.x) * 0.5;
        // Air thins with altitude. Distant valleys stay hazier than peaks.
        opticalDepth *= mix(0.45, 1.2, exp2(-max(pathHeight, 0.0) / 520.0));
        tint += atmosphereInscatter(direction, normalize(ubo.sunLightDirection.xyz),
            ubo.sunLightColor.rgb * ubo.sunLightDirection.w);
    }
    float amount = (1.0 - exp2(-opticalDepth)) * ubo.distanceFogParams.z;
    return vec4(tint, amount);
}

// RGB is the tint in linear light. Alpha is the amount of fog.
vec4 sceneFogAt(vec3 position, bool terrainFallback) {
    // Measure the ray in source-scene units, independent of projection.
    vec3 ray = position - ubo.cameraPosition.xyz;
    float normalizedDistance = length(ray);
    if (normalizedDistance <= 0.0) return vec4(0.0);
    float distance = normalizedDistance / max(ubo.cameraPosition.w, 1e-8);
    vec3 direction = ray / normalizedDistance;
    vec4 haze = distanceFogAt(direction, distance, terrainFallback);
    vec4 mist = floatingMistAt(direction, distance);
    if (mist.a <= 0.0) return haze;

    float transmittedHaze = haze.a * (1.0 - mist.a);
    float amount = transmittedHaze + mist.a;
    return vec4((haze.rgb * transmittedHaze + mist.rgb * mist.a) / amount, amount);
}

vec4 sceneFogAt(vec3 position) {
    return sceneFogAt(position, false);
}

vec3 applySceneFog(vec3 color, vec4 fog) {
    return mix(color, fog.rgb, fog.a);
}

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
} ubo;

#include "daylight_atmosphere.glsl"
#include "daylight_clouds.glsl"

// RGB is the tint in linear light. Alpha is the amount of fog.
vec4 sceneFogAt(vec3 position) {
    if (ubo.distanceFogParams.z <= 0.0) return vec4(0.0);

    // Positions have already been normalized for rendering. Measure the
    // radial distance in source-scene units, independent of the projection.
    vec3 ray = position - ubo.cameraPosition.xyz;
    float normalizedDistance = length(ray);
    float distance = normalizedDistance / max(ubo.cameraPosition.w, 1e-8);
    float fogDistance = distance - ubo.distanceFogParams.x;
    if (fogDistance <= 0.0) return vec4(0.0);

    // Rays pointing above the horizon pass through less low-altitude haze.
    vec3 direction = ray / normalizedDistance;
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

vec3 applySceneFog(vec3 color, vec4 fog) {
    return mix(color, fog.rgb, fog.a);
}

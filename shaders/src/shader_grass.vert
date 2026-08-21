#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inColor;
layout(location = 6) in vec4 inGrassData;

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
} ubo;

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

layout(location = 0) out vec2 fragBladeUv;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec4 fragVariation;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out float fragLodVisibility;
layout(location = 5) out vec2 fragRoot;
layout(location = 6) flat out float fragLod;

float lodVisibility(float lod, float distanceWorld) {
    if (lod < 0.5) {
        return 1.0 - smoothstep(17.0, 22.0, distanceWorld);
    }
    if (lod < 1.5) {
        return smoothstep(15.0, 19.0, distanceWorld) *
            (1.0 - smoothstep(28.0, 34.0, distanceWorld));
    }
    if (lod < 2.5) {
        return smoothstep(29.0, 34.0, distanceWorld) *
            (1.0 - smoothstep(36.0, 40.0, distanceWorld));
    }
    return 1.0 - smoothstep(19.0, 27.0, distanceWorld);
}

void main() {
    float heightWeight = inGrassData.w < 0.5
        ? clamp(inTexCoord.y, 0.0, 1.0)
        : clamp(1.0 - inTexCoord.y, 0.0, 1.0);
    float bendWeight = heightWeight * heightWeight;
    vec3 localPosition = inPosition;
    float phase = inGrassData.z * 6.28318530718;

    // Two differently oriented waves create BOTW's broad gusts plus local
    // blade flutter. Only the upper vertices move, keeping every root fixed.
    float broadWave = sin(
        inGrassData.x * 0.115 + inGrassData.y * 0.073 +
        ubo.time.x * 1.28 + phase
    );
    float detailWave = sin(
        inGrassData.x * -0.31 + inGrassData.y * 0.27 +
        ubo.time.x * 2.15 + phase * 1.73
    );
    vec2 windDirection = normalize(vec2(0.82, 0.57));
    vec2 crossWind = vec2(-windDirection.y, windDirection.x);
    vec2 windOffset = windDirection * (broadWave * 0.12 + detailWave * 0.035) +
        crossWind * detailWave * 0.025;
    localPosition.xz += windOffset * bendWeight;

    mat4 model = ubo.model * material.nodeTransform;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 rootScene = (model * vec4(inGrassData.x, 0.0, inGrassData.y, 1.0)).xyz;
    vec2 playerDeltaScene = rootScene.xz - ubo.playerPosition.xz;
    float playerDistanceScene = length(playerDeltaScene);
    float interaction = ubo.playerPosition.w * (
        1.0 - smoothstep(sceneScale * 0.55, sceneScale * 1.25, playerDistanceScene)
    );
    vec2 radialDirection = playerDistanceScene > sceneScale * 0.02
        ? playerDeltaScene / playerDistanceScene
        : vec2(0.0);
    vec2 movementDirection = length(ubo.playerVelocity.xz) > 0.08
        ? normalize(ubo.playerVelocity.xz)
        : radialDirection;
    vec2 trampleDirection = normalize(
        radialDirection * 0.58 + movementDirection * 0.72 + vec2(0.0001)
    );
    localPosition.xz += trampleDirection * interaction * bendWeight * 0.58;
    localPosition.y -= interaction * bendWeight * 0.27;

    vec3 worldPosition = (model * vec4(localPosition, 1.0)).xyz;
    float distanceWorld = distance(ubo.cameraPosition.xz, rootScene.xz) / sceneScale;
    float visibility = lodVisibility(inGrassData.w, distanceWorld);

    // Sink the outgoing representation as well as dissolving whole blades in
    // the fragment shader. This masks silhouettes during the LOD crossfade.
    localPosition.y -= (1.0 - visibility) * 0.72;
    worldPosition = (model * vec4(localPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(worldPosition, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragBladeUv = inTexCoord;
    fragNormal = normalize(normalMatrix * inNormal);
    fragVariation = inColor;
    fragWorldPosition = worldPosition;
    fragLodVisibility = visibility;
    fragRoot = inGrassData.xy;
    fragLod = inGrassData.w;
}

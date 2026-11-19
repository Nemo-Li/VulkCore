#version 450

layout(location = 0) in vec4 inRootHeight;
layout(location = 1) in vec4 inNormalWidth;
layout(location = 2) in vec4 inMotion;
layout(location = 3) in vec4 inTint;

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
layout(location = 2) flat out vec4 fragTint;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) flat out float fragLodVisibility;
layout(location = 5) flat out vec2 fragRoot;
layout(location = 6) flat out float fragLod;
layout(location = 7) flat out float fragColorVariation;

const float bladeRows[5] = float[](0.0, 0.28, 0.58, 0.82, 1.0);

void main() {
    float lod = material.waterSurface.w;
    bool nearBlade = lod < 0.5;
    vec3 root = inRootHeight.xyz;
    vec3 rootNormal = inNormalWidth.xyz;
    float yaw = inMotion.x;
    vec3 right = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 forward = vec3(-right.z, 0.0, right.x);
    bool tip = nearBlade && gl_VertexIndex == 8;
    float side = tip ? 0.0 : float(gl_VertexIndex & 1) * 2.0 - 1.0;
    int row = gl_VertexIndex / 2;
    float heightWeight = nearBlade ? bladeRows[row] : float(row);
    float bendWeight = heightWeight * heightWeight;
    float width = inNormalWidth.w * (nearBlade ? 1.0 - 0.72 * heightWeight : 1.0);
    vec3 localPosition = root
        + rootNormal * (inRootHeight.w * heightWeight)
        + right * (width * 0.5 * side)
        + forward * (inMotion.y * inRootHeight.w * bendWeight);
    float phase = inMotion.z * 6.28318530718;

    // Two differently oriented waves create BOTW's broad gusts plus local
    // blade flutter. Only the upper vertices move, keeping every root fixed.
    float broadWave = sin(
        root.x * 0.115 + root.z * 0.073 +
        ubo.time.x * 1.28 + phase
    );
    float detailWave = sin(
        root.x * -0.31 + root.z * 0.27 +
        ubo.time.x * 2.15 + phase * 1.73
    );
    vec2 windDirection = normalize(vec2(0.82, 0.57));
    vec2 crossWind = vec2(-windDirection.y, windDirection.x);
    vec2 windOffset = windDirection * (broadWave * 0.12 + detailWave * 0.035) +
        crossWind * detailWave * 0.025;
    localPosition.xz += windOffset * bendWeight;

    mat4 model = ubo.model * material.nodeTransform;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 rootScene = (model * vec4(root, 1.0)).xyz;
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

    float distanceWorld = distance(ubo.cameraPosition.xz, rootScene.xz) / sceneScale;
    vec4 fade = material.waterFlow;
    float fadeIn = fade.y > fade.x ? smoothstep(fade.x, fade.y, distanceWorld) : 1.0;
    float visibility = fadeIn * (1.0 - smoothstep(fade.z, fade.w, distanceWorld));

    // Sink the outgoing representation as well as dissolving whole blades in
    // the fragment shader. This masks silhouettes during the LOD crossfade.
    localPosition.y -= (1.0 - visibility) * inRootHeight.w * 0.65;
    vec3 worldPosition = (model * vec4(localPosition, 1.0)).xyz;
    gl_Position = visibility > 0.0
        ? ubo.projection * ubo.view * vec4(worldPosition, 1.0)
        : vec4(0.0, 0.0, -1.0, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragBladeUv = vec2(side * 0.5 + 0.5, nearBlade ? heightWeight : 1.0 - heightWeight);
    fragNormal = normalize(normalMatrix * mix(forward, rootNormal, nearBlade ? 0.90 : 0.68));
    fragTint = inTint;
    fragWorldPosition = worldPosition;
    fragLodVisibility = visibility;
    fragRoot = root.xz;
    fragLod = lod;
    fragColorVariation = inMotion.w;
}

#version 450
#extension GL_GOOGLE_include_directive : require

// 草叶和花卡的实例化顶点阶段。从少量实例属性重建轮廓、风动和踩踏。

// 与 CPU GrassInstance 对应。xyz 分别保存根位置和地表法线。
// w 分别保存高度和宽度参数，草叶存随机种子，卡片存实际宽度。
// motion 为朝向角、倾斜随机值、风相位和形状变化。
layout(location = 0) in vec4 inRootHeight;
layout(location = 1) in vec4 inNormalWidth;
layout(location = 2) in vec4 inMotion;
layout(location = 3) in vec4 inMapColor;

#include "scene_frame.glsl"
#include "terrain_road_weights.glsl"

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint textureFlags;
    float ochreDirtRatio;
    float roughness;
    float opacity;
    vec4 waterFlow;
    vec4 waterSurface;
} material;

layout(location = 0) out vec2 fragBladeUv;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragWorldPosition;
layout(location = 3) flat out float fragLod;
layout(location = 4) flat out float fragVariation;
layout(location = 5) flat out vec4 fragMapColor;
layout(location = 6) flat out float fragCanopyBlend;

layout(constant_id = 0) const bool legacyRoadBrush = true;

const float bladeRows[5] = float[](0.0, 0.28, 0.58, 0.82, 1.0);

vec2 growthNoiseAt(ivec2 cell) {
    uint seed = (uint(cell.x) * 0x9E3779B9u) ^ (uint(cell.y) * 0x85EBCA6Bu) ^ 0xB07F123u;
    seed = (seed ^ (seed >> 16u)) * 0x7FEB352Du;
    seed = (seed ^ (seed >> 15u)) * 0x846CA68Bu;
    seed ^= seed >> 16u;
    return vec2(uvec2(seed & 0xFFFFu, seed >> 16u)) * (2.0 / 65535.0) - 1.0;
}

vec2 growthNoise(vec2 position) {
    ivec2 cell = ivec2(floor(position));
    vec2 t = fract(position);
    t = t * t * (3.0 - 2.0 * t);
    vec2 lower = mix(growthNoiseAt(cell), growthNoiseAt(cell + ivec2(1, 0)), t.x);
    vec2 upper = mix(growthNoiseAt(cell + ivec2(0, 1)), growthNoiseAt(cell + ivec2(1, 1)), t.x);
    return mix(lower, upper, t.y);
}

float bladeDither(vec2 root) {
    return fract(sin(dot(root, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    // waterSurface 分别保存高度倍率、最小和最大倾斜弧度、GrassLod。
    // LOD1 用第二个分量传入能形成连续覆盖的最小草丛宽度。
    float lod = material.waterSurface.w;
    bool nearBlade = lod < 0.5;
    bool crossCard = lod > 0.5 && lod < 1.5;
    bool vegetation = nearBlade || crossCard;
    vec3 root = inRootHeight.xyz;
    mat4 model = ubo.model * material.nodeTransform;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 rootScene = (model * vec4(root, 1.0)).xyz;
    vec3 cameraDelta = ubo.cameraPosition.xyz - rootScene;
    float distanceWorld = (vegetation ? length(cameraDelta) : length(cameraDelta.xz)) / sceneScale;
    vec4 fade = material.waterFlow;
    float fadeIn = crossCard ? smoothstep(fade.x, fade.y, distanceWorld) : 1.0;
    float fadeOut = 1.0 - smoothstep(fade.z, fade.w, distanceWorld);
    float visibility = crossCard ? fadeIn : fadeOut;
    float dither = bladeDither(root.xz);
    // Reject invisible instances before doing wind and growth work for each vertex.
    gl_Position = vec4(0.0, 0.0, -1.0, 1.0);
    if (fadeOut <= 0.0 || visibility <= dither) return;

    vec3 rootNormal = inNormalWidth.xyz;
    float yaw = inMotion.x;
    vec3 right = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 forward = vec3(-right.z, 0.0, right.x);
    // 近处草叶由四对边缘顶点和一个尖端构成，花卡只用两行。与 CPU 的 kGrassIndices 对应。
    bool tip = nearBlade && gl_VertexIndex == 8;
    float side = tip ? 0.0 : float(gl_VertexIndex & 1) * 2.0 - 1.0;
    int row = gl_VertexIndex / 2;
    float heightWeight = nearBlade ? bladeRows[row] : float(row);
    // 二次弯曲让草根保持固定、叶尖偏移更大。沿高度收窄并扭转宽度轴以塑造叶片轮廓。
    float bendWeight = heightWeight * heightWeight;
    float sourceHeight = inRootHeight.w * material.waterSurface.x;
    float shortGrass = 0.0;
    if (legacyRoadBrush) {
        float dirtRatio = clamp(material.ochreDirtRatio, 0.0, 1.0);
        // Fade the brush's roadside height gradient as the painted dirt fades.
        float ochreVergeStrength = min(1.0, 2.0 * dirtRatio);
        vec3 roads = terrainRoadWeights(root.xz, vegetation, ochreVergeStrength, shortGrass);
        float ochreGrass = 1.0 - dirtRatio;
        if (vegetation && roads.x > 0.0 && dirtRatio > 0.0 && dirtRatio < 1.0) {
            float tuft = smoothstep(0.42, 0.82, roadGrassClusters(root.xz));
            ochreGrass += (tuft - 0.5) * (2.0 * min(dirtRatio, 1.0 - dirtRatio));
        }
        float roadVisibility = 1.0 - clamp(dot(roads, vec3(1.0 - ochreGrass, 1.0, 1.0)), 0.0, 1.0);
        if (visibility * roadVisibility <= dither) return;
    }
    float shortHeight = mix(0.085, 0.22, inMotion.y);
    float bladeHeight = mix(sourceHeight, min(sourceHeight, shortHeight), shortGrass);
    float width = inNormalWidth.w;
    if (nearBlade) {
        float baseWidth = mix(ubo.grassBladeWidthRange.x, ubo.grassBladeWidthRange.y, width);
        baseWidth = mix(baseWidth, min(baseWidth, mix(0.018, 0.036, width)), shortGrass);
        if (inMapColor.a > 0.0) baseWidth *= min(1.0, sourceHeight / 0.70);
        width = baseWidth * (1.0 - 0.62 * heightWeight);
    }
    float canopyBlend = 0.0;
    if (crossCard) {
        width *= mix(1.0, 0.36, shortGrass);
        // Use the uncollapsed canopy footprint so fading does not feed back into the test.
        vec4 rootView = ubo.view * vec4(rootScene, 1.0);
        float heightPixels = bladeHeight * sceneScale * abs(ubo.projection[1][1]) *
            (ubo.viewport.y * 0.5) / max(-rootView.z, 0.000001);
        float widthCoverage = smoothstep(material.waterSurface.y, 0.66, width);
        float screenCoverage = smoothstep(2.5, 6.0, heightPixels);
        float canopy = fadeOut * widthCoverage * screenCoverage;
        if (canopy <= 0.0) return;
        bladeHeight *= canopy;
        canopyBlend = 1.0 - canopy;
    }
    float twist = nearBlade ? (inMotion.w - 0.5) * 0.58 * heightWeight : 0.0;
    vec3 widthAxis = right * cos(twist) + forward * sin(twist);
    vec3 growthOffset = rootNormal * heightWeight;
    if (nearBlade && material.waterSurface.z > 0.0) {
        // 根坐标上的连续噪声决定草丛走向，每叶朝向再增加局部差异。
        vec2 direction = growthNoise(root.xz * material.waterFlow.x) + right.xz * 0.40;
        direction = dot(direction, direction) > 0.000001 ? normalize(direction) : right.xz;
        vec3 tangentX = normalize(vec3(rootNormal.y, -rootNormal.x, 0.0));
        vec3 tangentZ = cross(tangentX, rootNormal);
        vec3 tiltDirection = direction.x * tangentX + direction.y * tangentZ;
        float tilt = mix(material.waterSurface.y, material.waterSurface.z, inMotion.y);
        float leanWeight = mix(heightWeight, bendWeight, 0.80);
        growthOffset = rootNormal * (cos(tilt) * heightWeight)
            + tiltDirection * (sin(tilt) * leanWeight);
    }
    vec3 localPosition = root + growthOffset * bladeHeight + widthAxis * (width * 0.5 * side);

    // 低频阵风让附近草叶一起摆动，高频项使用独立相位以减弱整片草叶的同步感。
    float broadWave = sin(root.x * 0.115 + root.z * 0.073 + ubo.time.w * 1.28);
    float detailWave = sin(root.x * -0.31 + root.z * 0.27 + ubo.time.w * 2.15 +
                           inMotion.z * 6.2831853);
    vec2 windOffset = vec2(0.82, 0.57) * (broadWave * 0.055 + detailWave * 0.016) +
        vec2(-0.57, 0.82) * detailWave * 0.013;
    localPosition.xz += windOffset * (bladeHeight * bendWeight);

    // 玩家与草根都在归一化场景空间。把原始场景的踩踏半径换到同一空间再比较。
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
    // 结合远离脚步的径向和角色移动方向，使静止时草向外倒、移动时沿脚步方向弯折。
    vec2 trampleDirection = normalize(
        radialDirection * 0.58 + movementDirection * 0.72 + vec2(0.0001)
    );
    float trampleStrength = mix(1.0, 0.24, shortGrass);
    if (vegetation) trampleStrength *= min(1.0, bladeHeight / 0.35);
    localPosition.xz += trampleDirection * interaction * bendWeight * (nearBlade ? 0.30 : 0.18) * trampleStrength;
    localPosition.y -= interaction * bendWeight * 0.14 * trampleStrength;

    if (!crossCard) localPosition.y -= (1.0 - fadeOut) * bladeHeight * 0.52;
    fragWorldPosition = (model * vec4(localPosition, 1.0)).xyz;
    gl_Position = ubo.projection * ubo.view * vec4(fragWorldPosition, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragBladeUv = vec2(side * 0.5 + 0.5, heightWeight);
    fragNormal = normalize(normalMatrix * mix(forward, rootNormal, vegetation ? 0.88 : 0.68));
    fragLod = lod;
    fragVariation = inMotion.w;
    fragMapColor = inMapColor;
    fragCanopyBlend = canopyBlend;
}

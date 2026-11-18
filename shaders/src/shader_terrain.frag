#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in float fragTerrainHeight;
layout(location = 5) in vec2 fragTerrainTileSize;

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
} ubo;

layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(set = 0, binding = 6) uniform sampler2DArray albedoLayers;
layout(set = 0, binding = 7) uniform sampler2DArray surfaceLayers;

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

const int kLandformCount = 8;
const uint kAlbedoLayers = 8u;
const uint kSurfaceLayers = 16u;

float hash21(vec2 cell) {
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float valueNoise(vec2 position) {
    vec2 cell = floor(position);
    vec2 f = fract(position);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(cell), hash21(cell + vec2(1, 0)), f.x),
               mix(hash21(cell + vec2(0, 1)), hash21(cell + vec2(1, 1)), f.x), f.y);
}

struct Projection {
    vec2 uv;
    vec2 dx;
    vec2 dy;
};

Projection projectUv(vec2 uv) {
    return Projection(uv, dFdx(uv), dFdy(uv));
}

vec3 sampleLegacyAtlas(int layer, Projection projection) {
    const vec2 cellSize = vec2(0.25, 0.5);
    const vec2 inset = vec2(0.004, 0.006);
    vec2 cellOrigin = vec2(layer % 4, layer / 4) * cellSize;
    vec2 span = cellSize - 2.0 * inset;
    return textureGrad(textureSampler, cellOrigin + inset + fract(projection.uv) * span,
                       projection.dx * span, projection.dy * span).rgb;
}

void main() {
    vec3 geometryNormal = normalize(fragNormal);
    float slope = 1.0 - clamp(geometryNormal.y, 0.0, 1.0);
    float height = fragTerrainHeight;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 terrainPosition = fragWorldPosition / sceneScale;
    float distanceWorld = distance(fragWorldPosition, ubo.cameraPosition.xyz) / sceneScale;
    vec2 textureScale = clamp(fragVertexColor.xy, vec2(0.05), vec2(2.0));
    // Texture scales come from the terrain records. Keep the projected
    // pattern broad enough that it reads as paint at player eye height.
    vec2 frequency = 0.68 * textureScale;
    float distanceFade = smoothstep(140.0, 720.0, distanceWorld);
    float gradientScale = mix(1.0, 5.0, distanceFade);
    Projection projections[3] = Projection[3](
        projectUv(terrainPosition.zy * frequency),
        projectUv(terrainPosition.xz * frequency),
        projectUv(terrainPosition.xy * frequency)
    );

    vec2 atlasFrequency = 12.0 / fragTerrainTileSize;
    float verticalFrequency = sqrt(atlasFrequency.x * atlasFrequency.y);
    vec3 atlasPosition = terrainPosition * vec3(atlasFrequency.x, verticalFrequency, atlasFrequency.y);
    Projection legacy[3] = Projection[3](
        projectUv(atlasPosition.zy * textureScale),
        projectUv(fragTexCoord * textureScale * 12.0),
        projectUv(atlasPosition.xy * textureScale)
    );

    // Independent masks for vegetation cover, shoreline deposits, exposed
    // stone and snow. World-space patches give the transition irregular edges.
    float region = valueNoise(terrainPosition.xz / 190.0);
    float patchNoise = mix(valueNoise(terrainPosition.xz / 18.0), 0.5, distanceFade * 0.7);
    float brush = mix(valueNoise(terrainPosition.xz / 4.8), 0.5, distanceFade);
    float contrast = mix(0.76, 1.22, clamp(fragVertexColor.w / 1.63, 0.0, 1.0));
    float cover = 0.5 + (0.48 * region + 0.38 * patchNoise + 0.14 * brush - 0.5) * contrast +
                  (clamp(fragVertexColor.z, 0.0, 1.0) - 0.5) * 0.12;
    float lowland = 1.0 - smoothstep(0.10, 0.25, height);
    float highland = smoothstep(0.38, 0.64, height);
    float brokenSlope = slope + (patchNoise - 0.5) * 0.105 + (brush - 0.5) * 0.052;
    float snow = smoothstep(0.65, 0.81, height - slope * 0.17 + (region - 0.5) * 0.075);
    float rock = (1.0 - snow) * smoothstep(0.18, 0.56, brokenSlope);
    float ground = max(0.0, 1.0 - snow - rock);
    float ash = ground * highland * smoothstep(0.52, 0.76, region) *
                smoothstep(0.10, 0.25, brokenSlope);
    ground -= ash;
    float sand = ground * lowland * smoothstep(0.54, 0.74, cover);
    ground -= sand;
    float mud = ground * lowland * (1.0 - smoothstep(0.25, 0.47, cover));
    ground -= mud;
    float gravel = ground * smoothstep(0.11, 0.34, brokenSlope) *
                   smoothstep(0.36, 0.63, cover) * 0.78;
    ground -= gravel;
    float dirt = ground * smoothstep(0.48, 0.69, cover + brokenSlope * 0.37);
    float grass = ground - dirt;
    float landforms[kLandformCount] = float[kLandformCount](
        grass, dirt, rock, sand, snow, mud, gravel, ash
    );

    vec3 axes = pow(abs(geometryNormal), vec3(4.0));
    // Negligible axes fall out gradually and do not incur texture reads.
    axes *= smoothstep(vec3(0.0005), vec3(0.008), axes);
    axes /= dot(axes, vec3(1.0));

    bool hasAlbedo = (material.textureFlags & kAlbedoLayers) != 0u;
    float surfaceFade = 1.0 - smoothstep(34.0, 115.0, distanceWorld);
    bool hasSurface = (material.textureFlags & kSurfaceLayers) != 0u && hasAlbedo && surfaceFade > 0.0;
    // Each column is a UV axis expressed in world space. Normal XY is a
    // height gradient on that plane. Reorient gradients before mixing axes,
    // then remove the component parallel to the geometric normal.
    const mat2x3 bases[3] = mat2x3[3](
        mat2x3(vec3(0, 0, 1), vec3(0, 1, 0)),
        mat2x3(vec3(1, 0, 0), vec3(0, 0, 1)),
        mat2x3(vec3(1, 0, 0), vec3(0, 1, 0))
    );
    vec3 albedo = vec3(0.0);
    vec3 normalOffset = vec3(0.0);
    float occlusion = 0.0;
    float sampledWeight = 0.0;
    for (int layer = 0; layer < kLandformCount; ++layer) {
        for (int axis = 0; axis < 3; ++axis) {
            float weight = landforms[layer] * axes[axis];
            weight *= smoothstep(0.001, 0.012, weight);
            if (weight <= 0.0) continue;

            Projection p = projections[axis];
            vec3 coordinates = vec3(p.uv, float(layer));
            vec3 color = hasAlbedo
                ? textureGrad(albedoLayers, coordinates, p.dx * gradientScale, p.dy * gradientScale).rgb
                : sampleLegacyAtlas(layer, legacy[axis]);
            albedo += color * weight;
            sampledWeight += weight;
            if (hasSurface) {
                vec4 surface = textureGrad(surfaceLayers, coordinates, p.dx, p.dy);
                vec3 tangentNormal = surface.xyz * 2.0 - 1.0;
                normalOffset += bases[axis] * (tangentNormal.xy / max(tangentNormal.z, 0.4)) * weight;
                occlusion += surface.a * weight;
            }
        }
    }
    float inverseWeight = 1.0 / max(sampledWeight, 0.0001);
    albedo *= inverseWeight;
    // Low-frequency color variation survives minification without amplifying
    // fine detail or imprinting a grid on distant hills.
    float tint = (0.55 * region + 0.45 * patchNoise - 0.5) * (0.25 + 0.1 * grass);
    albedo *= vec3(1.0 + tint, 1.0 + tint * 0.84, 1.0 + tint * 0.62);

    vec3 offset = normalOffset * inverseWeight;
    offset -= geometryNormal * dot(geometryNormal, offset);
    vec3 normal = normalize(geometryNormal + offset * (surfaceFade * 0.78));
    float ao = hasSurface ? mix(1.0, occlusion * inverseWeight, surfaceFade) : 1.0;

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    // Smooth wrapped diffuse keeps broad painterly light shapes and soft
    // shadow transitions as the small-scale normal detail fades out.
    float diffuse = smoothstep(-0.38, 0.89, dot(normal, lightDirection));
    float skyVisibility = mix(0.60, 1.0, clamp(geometryNormal.y, 0.0, 1.0));
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a * skyVisibility * ao;
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * diffuse;
    outColor = vec4(albedo * (ambient + sunlight) * material.baseColor.rgb, material.opacity);
}

#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec3 fragTerrainPosition;
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
    float up = clamp(geometryNormal.y, 0.0, 1.0);
    float slope = 1.0 - up;
    vec3 terrainPosition = fragTerrainPosition;
    float elevation = terrainPosition.y;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 cameraRay = (fragWorldPosition - ubo.cameraPosition.xyz) / sceneScale;
    float distanceWorld = length(cameraRay);
    vec2 textureScale = clamp(fragVertexColor.xy, vec2(0.05), vec2(2.0));
    // Texture scales come from the terrain records. Keep the projected
    // pattern broad enough that it reads as paint at player eye height.
    vec2 frequency = 0.68 * textureScale;
    float distanceFade = smoothstep(140.0, 720.0, distanceWorld);
    // The gradients already select the mip for the pixel footprint. Add only
    // a small bias at long range to preserve broad rock layers and grass cover.
    float gradientScale = mix(1.0, 1.5, distanceFade);
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

    // Sample the slope face as well as its footprint. Samples along a steep
    // cliff can otherwise share nearly identical XZ and form vertical streaks.
    vec2 groundPosition = terrainPosition.xz;
    float region = valueNoise(groundPosition / 190.0);
    vec2 facePosition = groundPosition + elevation * vec2(0.51, -0.67);
    float ledge = valueNoise(facePosition * vec2(1.0 / 92.0, 1.0 / 65.0));
    float patchNoise = mix(valueNoise(facePosition / 18.0), 0.5, distanceFade * 0.7);
    float brush = mix(valueNoise(facePosition / 4.8), 0.5, distanceFade);
    float contrast = mix(0.76, 1.22, clamp(fragVertexColor.w / 1.63, 0.0, 1.0));
    float cover = 0.5 + (0.43 * region + 0.29 * ledge + 0.20 * patchNoise + 0.08 * brush - 0.5) * contrast +
                  (clamp(fragVertexColor.z, 0.0, 1.0) - 0.5) * 0.12;
    float lowland = 1.0 - smoothstep(80.0, 200.0, elevation);
    float highland = smoothstep(305.0, 510.0, elevation);
    float brokenSlope = slope + (ledge - 0.5) * 0.14 + (patchNoise - 0.5) * 0.09 + (brush - 0.5) * 0.04;
    // Snow collects on upper shelves. Regional variation breaks up the snow
    // line, and the steepest faces retain exposed rock even at high elevation.
    float snowElevation = elevation + (region - 0.5) * 170.0 + (ledge - 0.5) * 80.0;
    float snow = smoothstep(560.0, 735.0, snowElevation) * (1.0 - smoothstep(0.12, 0.45, brokenSlope));
    // Let vegetation run farther down favorable slopes while leaving nearly
    // vertical faces exposed. Transition thresholds vary over broad patches.
    float vegetation = smoothstep(0.36, 0.64, cover);
    float rock = (1.0 - snow) * smoothstep(0.19 + 0.14 * vegetation, 0.57 + 0.14 * vegetation, brokenSlope);
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

    const int grassLayer = 0;
    const int rockLayer = 2;
    // Pigment and horizontal beds are evaluated in terrain units so they
    // survive texture minification. Warp the beds instead of drawing stripes
    // along contours of constant elevation.
    float warmStone = smoothstep(0.29, 0.71, 0.50 * region + 0.28 * ledge + 0.22 * highland);
    vec3 rockTint = mix(vec3(0.90, 1.04, 0.95), vec3(1.58, 0.99, 0.72), warmStone);
    float bedHeight = (elevation + (region - 0.5) * 44.0 + (ledge - 0.5) * 14.0) / 29.0;
    float bed = valueNoise(vec2(bedHeight, (groundPosition.x + groundPosition.y) / 175.0));
    rockTint *= mix(0.80, 1.18, smoothstep(0.24, 0.78, bed));
    vec3 grassTint = mix(vec3(0.96, 1.03, 0.91), vec3(1.09, 1.25, 0.93), vegetation);

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
        vec3 layerTint = layer == rockLayer ? rockTint : layer == grassLayer ? grassTint : vec3(1.0);
        for (int axis = 0; axis < 3; ++axis) {
            float weight = landforms[layer] * axes[axis];
            weight *= smoothstep(0.001, 0.012, weight);
            if (weight <= 0.0) continue;

            Projection p = projections[axis];
            vec3 coordinates = vec3(p.uv, float(layer));
            vec3 color = hasAlbedo
                ? textureGrad(albedoLayers, coordinates, p.dx * gradientScale, p.dy * gradientScale).rgb
                : sampleLegacyAtlas(layer, legacy[axis]);
            albedo += color * layerTint * weight;
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
    float skyVisibility = mix(0.70, 1.0, up);
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a * skyVisibility * ao;
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * diffuse;
    vec3 litTerrain = albedo * (ambient + sunlight) * material.baseColor.rgb;

    // Aerial perspective lifts the shadows of distant ridges toward the sky.
    // Near terrain retains its color and contrast, with less haze on rays
    // that climb out of the denser air near the ground.
    float viewElevation = cameraRay.y / max(distanceWorld, 0.001);
    float haze = 1.0 - exp2(-max(distanceWorld - 125.0, 0.0) / 1350.0);
    haze *= mix(0.58, 0.34, smoothstep(0.02, 0.48, viewElevation));
    vec3 hazeColor = vec3(0.27, 0.46, 0.62);
    outColor = vec4(mix(litTerrain, hazeColor, haze), material.opacity);
}

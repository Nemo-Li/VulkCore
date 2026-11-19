#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec3 fragTerrainPosition;
layout(location = 5) in vec4 fragTerrainMapping;

#include "scene_fog.glsl"

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
const uint kMacroTexture = 32u;
// Linear RGB averages of the generated albedo layers in array order.
const vec3 kLandformAverages[kLandformCount] = vec3[kLandformCount](
    vec3(0.239, 0.358, 0.101), vec3(0.423, 0.276, 0.115),
    vec3(0.246, 0.261, 0.226), vec3(0.634, 0.436, 0.179),
    vec3(0.627, 0.731, 0.765), vec3(0.170, 0.130, 0.063),
    vec3(0.316, 0.283, 0.191), vec3(0.173, 0.125, 0.102)
);

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
    vec2 dx = projection.dx * span;
    vec2 dy = projection.dy * span;
    vec2 texels = vec2(textureSize(textureSampler, 0));
    float footprint = max(length(dx * texels), length(dy * texels));
    // Keep the fallback atlas mip within the cell's narrowest inset.
    float limit = min(1.0, 8.0 / max(footprint, 0.001));
    return textureGrad(textureSampler, cellOrigin + inset + fract(projection.uv) * span,
                       dx * limit, dy * limit).rgb;
}

struct TerrainSample {
    vec3 albedo;
    vec2 gradient;
    float occlusion;
};

TerrainSample sampleTerrain(int layer, Projection p, bool hasAlbedo, bool hasSurface, float mipBias) {
    vec3 coordinates = vec3(p.uv, float(layer));
    vec3 color = hasAlbedo
        ? textureGrad(albedoLayers, coordinates, p.dx * mipBias, p.dy * mipBias).rgb
        : sampleLegacyAtlas(layer, p);
    TerrainSample result = TerrainSample(color, vec2(0.0), 0.0);
    if (hasSurface) {
        vec4 surface = textureGrad(surfaceLayers, coordinates, p.dx, p.dy);
        vec3 tangentNormal = surface.xyz * 2.0 - 1.0;
        result.gradient = tangentNormal.xy / max(tangentNormal.z, 0.4);
        result.occlusion = surface.a;
    }
    return result;
}

struct TerrainPatch {
    Projection projection;
    mat2 transform;
    float weight;
};

void makeTerrainPatches(int axis, Projection p, vec2 gridWarp, out TerrainPatch patches[3]) {
    // The two triangles sharing an edge also share its two weighted samples.
    const mat2 toLattice = mat2(1.0, 0.0, -0.57735027, 1.15470054);
    vec2 grid = toLattice * (p.uv * 0.9 + gridWarp);
    vec2 cell = floor(grid);
    vec2 f = fract(grid);
    float diagonal = 1.0 - f.x - f.y;
    bool lowerTriangle = diagonal >= 0.0;
    vec2 vertices[3] = vec2[3](cell + vec2(lowerTriangle ? 0.0 : 1.0),
                                cell + vec2(1.0, 0.0), cell + vec2(0.0, 1.0));
    vec3 weights = lowerTriangle ? vec3(diagonal, f) : vec3(-diagonal, 1.0 - f.yx);
    weights = max(weights * weights - 0.01, vec3(0.0));
    weights /= dot(weights, vec3(1.0));

    // Reuse the same transformed projections for every landform on this axis.
    for (int corner = 0; corner < 3; ++corner) {
        vec2 seed = vertices[corner] + float(axis) * vec2(37.0, -19.0);
        vec2 offset = vec2(hash21(seed + vec2(5.4, 13.7)), hash21(seed + vec2(-9.8, 3.1)));
        float angle = mix(-0.68, 0.68, hash21(seed + vec2(8.2, -12.3)));
        float scale = mix(0.75, 1.35, hash21(seed + vec2(19.8, 2.6)));
        float c = cos(angle);
        float s = sin(angle);
        mat2 transform = scale * mat2(c, s, -s, c);
        // Transform gradients explicitly so mip selection stays continuous
        // when neighboring pixels select different triangles.
        Projection projection = Projection(transform * p.uv + offset, transform * p.dx, transform * p.dy);
        patches[corner] = TerrainPatch(projection, transform, weights[corner]);
    }
}

TerrainSample sampleStochasticTerrain(int layer, TerrainPatch patches[3],
                                      bool hasAlbedo, bool hasSurface, float mipBias) {
    TerrainSample result = TerrainSample(vec3(0.0), vec2(0.0), 0.0);
    for (int corner = 0; corner < 3; ++corner) {
        TerrainPatch region = patches[corner];
        if (region.weight == 0.0) continue;
        TerrainSample surface = sampleTerrain(layer, region.projection, hasAlbedo, hasSurface, mipBias);
        result.albedo += surface.albedo * region.weight;
        result.gradient += transpose(region.transform) * surface.gradient * region.weight;
        result.occlusion += surface.occlusion * region.weight;
    }
    return result;
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
    float distanceFade = smoothstep(150.0, 520.0, distanceWorld);
    float footprint = length(fwidth(terrainPosition));
    float detailWeight = (1.0 - distanceFade) * (1.0 - smoothstep(0.7, 3.0, footprint));
    float gradientScale = mix(1.0, 2.2, distanceFade);
    bool hasAlbedo = (material.textureFlags & kAlbedoLayers) != 0u;
    // Binding 1 is the traditional atlas when arrays are unavailable. When
    // the layers are present it can carry the terrain-wide control map.
    bool hasMacro = (material.textureFlags & kMacroTexture) != 0u;
    Projection projections[3] = Projection[3](
        projectUv(terrainPosition.zy * frequency),
        projectUv(terrainPosition.xz * frequency),
        projectUv(terrainPosition.xy * frequency)
    );

    vec2 atlasFrequency = 12.0 / fragTerrainMapping.xy;
    float verticalFrequency = sqrt(atlasFrequency.x * atlasFrequency.y);
    vec3 atlasPosition = terrainPosition * vec3(atlasFrequency.x, verticalFrequency, atlasFrequency.y);
    Projection legacy[3] = Projection[3](
        projectUv(atlasPosition.zy * textureScale),
        projectUv(atlasPosition.xz * textureScale),
        projectUv(atlasPosition.xy * textureScale)
    );

    // Sample the slope face as well as its footprint. Samples along a steep
    // cliff can otherwise share nearly identical XZ and form vertical streaks.
    vec2 groundPosition = terrainPosition.xz;
    float region = valueNoise(groundPosition / 180.0);
    vec4 controls = vec4(region, valueNoise(groundPosition / 760.0 + 2.7), region, 0.5);
    if (hasMacro) {
        controls = texture(textureSampler, fragTerrainMapping.zw);
    }
    vec2 facePosition = groundPosition + elevation * vec2(0.51, -0.67);
    float ledge = valueNoise(facePosition * vec2(1.0 / 92.0, 1.0 / 65.0));
    float patchNoise = mix(valueNoise(facePosition / 23.0), ledge, distanceFade * 0.86);
    float brush = mix(0.5, valueNoise(facePosition / 5.3), detailWeight);
    float contrast = mix(0.92, 1.10, clamp(fragVertexColor.w / 1.63, 0.0, 1.0));
    float cover = 0.5 + (0.46 * controls.r + 0.21 * controls.b + 0.24 * patchNoise + 0.09 * brush - 0.5) *
                  contrast + (clamp(fragVertexColor.z, 0.0, 1.0) - 0.5) * 0.05;
    float lowland = 1.0 - smoothstep(80.0, 200.0, elevation);
    float highland = smoothstep(305.0, 510.0, elevation);
    float brokenSlope = slope + (ledge - 0.5) * 0.14 + (patchNoise - 0.5) * 0.09 + (brush - 0.5) * 0.04;
    // Snow collects on upper shelves. Regional variation breaks up the snow
    // line, and the steepest faces retain exposed rock even at high elevation.
    float snowElevation = elevation + (controls.r - 0.5) * 120.0 + (ledge - 0.5) * 88.0;
    float snow = smoothstep(600.0, 760.0, snowElevation) * (1.0 - smoothstep(0.10, 0.37, brokenSlope));
    // Moist slopes hold grass farther down while nearly vertical faces expose stone.
    float vegetation = smoothstep(0.31, 0.61, cover);
    float rock = (1.0 - snow) * smoothstep(0.18 + 0.15 * vegetation, 0.52 + 0.15 * vegetation, brokenSlope);
    float ground = max(0.0, 1.0 - snow - rock);
    float ash = ground * highland * smoothstep(0.65, 0.84, controls.g) *
                smoothstep(0.10, 0.25, brokenSlope) * 0.66;
    ground -= ash;
    float sand = ground * lowland * smoothstep(0.64, 0.85, controls.b) * (1.0 - 0.65 * vegetation);
    ground -= sand;
    float mud = ground * lowland * smoothstep(0.65, 0.88, controls.r) * (1.0 - vegetation * 0.65);
    ground -= mud;
    float gravel = ground * smoothstep(0.13, 0.39, brokenSlope) *
                   smoothstep(0.25, 0.64, controls.b) * (1.0 - 0.52 * vegetation);
    ground -= gravel;
    float erosion = 0.27 * controls.b + 0.24 * patchNoise + 0.27 * (1.0 - controls.r) + 0.54 * brokenSlope;
    float dirt = ground * smoothstep(0.40, 0.66, erosion) * 0.86;
    float grass = ground - dirt;
    float landforms[kLandformCount] = float[kLandformCount](
        grass, dirt, rock, sand, snow, mud, gravel, ash
    );

    const int grassLayer = 0;
    const int rockLayer = 2;
    // Medium-scale deposits follow the slope and extend beyond the close-up textures.
    const mat2 turnRock = mat2(0.8, 0.6, -0.6, 0.8);
    vec2 mineralPosition = turnRock * facePosition;
    vec2 mineralWarp = vec2(ledge, valueNoise(mineralPosition / 53.0 + vec2(13.6, 24.8))) - 0.5;
    float mineral = valueNoise(mineralPosition / 34.0 + mineralWarp * 1.8);
    float warmStone = smoothstep(0.36, 0.71, 0.72 * controls.g + 0.15 * mineral + 0.13 * highland);
    vec3 rockTint = mix(vec3(0.88, 0.99, 1.06), vec3(1.72, 0.78, 0.69), warmStone);
    float bedHeight = (elevation + (region - 0.5) * 50.0 + mineralWarp.y * 37.0) / 32.0;
    float bed = valueNoise(vec2(bedHeight, mineralPosition.x / 130.0 + mineralWarp.x));
    float weathering = 0.22 * (mineral - 0.5) + 0.13 * (brush - 0.5);
    rockTint *= mix(0.91, 1.10, smoothstep(0.24, 0.78, bed)) * (1.0 + weathering);
    vec3 grassTint = mix(vec3(1.08, 1.01, 0.89), vec3(0.99, 1.16, 0.97), vegetation);

    vec3 tints[kLandformCount];
    vec3 macroAlbedo = vec3(0.0);
    for (int layer = 0; layer < kLandformCount; ++layer) {
        tints[layer] = layer == rockLayer ? rockTint : layer == grassLayer ? grassTint : vec3(1.0);
        macroAlbedo += landforms[layer] * tints[layer] * kLandformAverages[layer];
    }

    float surfaceFade = (1.0 - smoothstep(28.0, 110.0, distanceWorld)) * detailWeight;
    bool hasSurface = (material.textureFlags & kSurfaceLayers) != 0u && hasAlbedo && surfaceFade > 0.0;
    vec3 albedo = macroAlbedo;
    vec3 normalOffset = vec3(0.0);
    float ao = 1.0;
    if (detailWeight > 0.0) {
        vec3 axes = pow(abs(geometryNormal), vec3(4.0));
        axes *= smoothstep(vec3(0.001), vec3(0.017), axes);
        axes /= dot(axes, vec3(1.0));
        const mat2x3 bases[3] = mat2x3[3](
            mat2x3(vec3(0, 0, 1), vec3(0, 1, 0)),
            mat2x3(vec3(1, 0, 0), vec3(0, 0, 1)),
            mat2x3(vec3(1, 0, 0), vec3(0, 1, 0))
        );
        vec3 detailAlbedo = vec3(0.0);
        float occlusion = 0.0;
        float sampledWeight = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            if (axes[axis] <= 0.006) continue;
            Projection p = hasAlbedo ? projections[axis] : legacy[axis];
            TerrainPatch patches[3];
            makeTerrainPatches(axis, p, mineralWarp * 1.1, patches);
            for (int layer = 0; layer < kLandformCount; ++layer) {
                float weight = landforms[layer] * axes[axis];
                weight *= smoothstep(0.006, 0.028, weight);
                if (weight <= 0.0) continue;
                TerrainSample surface = sampleStochasticTerrain(layer, patches, hasAlbedo, hasSurface, gradientScale);
                detailAlbedo += surface.albedo * tints[layer] * weight;
                normalOffset += bases[axis] * surface.gradient * weight;
                occlusion += surface.occlusion * weight;
                sampledWeight += weight;
            }
        }
        float inverseWeight = 1.0 / max(sampledWeight, 0.0001);
        albedo = mix(macroAlbedo, detailAlbedo * inverseWeight, detailWeight);
        normalOffset *= inverseWeight;
        if (hasSurface) ao = mix(1.0, occlusion * inverseWeight, surfaceFade);
    }
    albedo *= mix(0.85, 1.15, controls.a);
    normalOffset -= geometryNormal * dot(geometryNormal, normalOffset);
    vec3 normal = normalize(geometryNormal + normalOffset * (surfaceFade * 0.78));

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    // Smooth wrapped diffuse keeps broad painterly light shapes and soft
    // shadow transitions as the small-scale normal detail fades out.
    float diffuse = smoothstep(-0.38, 0.89, dot(normal, lightDirection));
    float skyVisibility = mix(0.66, 1.0, up);
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a * skyVisibility * ao;
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * diffuse * mix(0.85, 1.04, controls.a);
    vec3 litTerrain = albedo * (ambient + sunlight) * material.baseColor.rgb;

    vec4 fog = sceneFogAt(fragWorldPosition);
    if (ubo.distanceFogParams.w == 0.0) {
        // Retain the original terrain haze for scenes saved without a fog preset.
        float viewElevation = cameraRay.y / max(distanceWorld, 0.001);
        float haze = 1.0 - exp2(-max(distanceWorld - 125.0, 0.0) / 1350.0);
        haze *= mix(0.58, 0.34, smoothstep(0.02, 0.48, viewElevation));
        fog = vec4(0.19, 0.34, 0.48, haze);
    }
    outColor = vec4(applySceneFog(litTerrain, fog), material.opacity);
}

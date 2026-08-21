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
layout(set = 0, binding = 6) uniform sampler2D terrainDetailTextures[8];

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

const vec2 kAtlasCellSize = vec2(0.25, 0.5);
const vec2 kAtlasInset = vec2(0.004, 0.006);

vec3 sampleLandform(int index, vec2 uv) {
    int column = index % 4;
    int row = index / 4;
    vec2 cellOrigin = vec2(float(column), float(row)) * kAtlasCellSize;
    vec2 cellUv = cellOrigin + kAtlasInset +
        fract(uv) * (kAtlasCellSize - 2.0 * kAtlasInset);
    return texture(textureSampler, cellUv).rgb;
}

vec3 sampleDetailLandform(int index, vec2 uv) {
    return texture(terrainDetailTextures[index], fract(uv)).rgb;
}

vec3 sampleLandformTriplanar(
    int index,
    vec2 uvX,
    vec2 uvY,
    vec2 uvZ,
    vec3 weights
) {
    return sampleLandform(index, uvX) * weights.x +
        sampleLandform(index, uvY) * weights.y +
        sampleLandform(index, uvZ) * weights.z;
}

vec3 sampleDetailLandformTriplanar(
    int index,
    vec2 uvX,
    vec2 uvY,
    vec2 uvZ,
    vec3 weights
) {
    return sampleDetailLandform(index, uvX) * weights.x +
        sampleDetailLandform(index, uvY) * weights.y +
        sampleDetailLandform(index, uvZ) * weights.z;
}

float waveNoise(vec2 position) {
    float first = sin(position.x * 0.0017 + sin(position.y * 0.0021));
    float second = sin(position.y * 0.0031 + sin(position.x * 0.0013));
    return clamp(first * 0.25 + second * 0.25 + 0.5, 0.0, 1.0);
}

void main() {
    vec3 normal = normalize(fragNormal);
    float height = clamp(fragTerrainHeight, 0.0, 1.0);
    float slope = 1.0 - clamp(normal.y, 0.0, 1.0);

    // The first two vertex-color channels are the TSCB per-material U/V
    // scales. The last two carry the preserved, currently unnamed material
    // tuning values from the TSCB record.
    vec2 textureScale = clamp(fragVertexColor.xy, vec2(0.05), vec2(2.0));
    // The mesh UV is local to one terrain tile. It is intentionally kept for
    // the broad Atlas LOD, but using it for close detail makes a large tile
    // stretch one grass sample over too much world space. Build the close
    // UV from world position instead, so texture density is independent of
    // tile/mesh extent and follows the TSCB scale in terrain world units.
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec2 atlasUv = fragTexCoord * textureScale * 12.0;
    vec3 terrainPosition = fragWorldPosition / sceneScale;
    vec2 detailUv = terrainPosition.xz * textureScale;

    // A heightfield's XZ UV is degenerate on a vertical face: movement along
    // Y changes the geometry but does not change the UV. Blend projections
    // from all three axes so cliff faces get stable texel density as well.
    vec3 projectionWeights = pow(abs(normal), vec3(4.0));
    projectionWeights /= max(
        dot(projectionWeights, vec3(1.0)),
        0.0001
    );
    vec2 detailUvX = terrainPosition.zy * textureScale;
    vec2 detailUvY = detailUv;
    vec2 detailUvZ = terrainPosition.xy * textureScale;

    // The atlas UV remains continuous with the existing top-down mapping.
    // Its side projections use the actual tile size so the lower-resolution
    // LOD does not stretch vertically or change density between tile sizes.
    vec2 atlasWorldFrequency = 12.0 / max(fragTerrainTileSize, vec2(0.0001));
    float atlasVerticalFrequency = sqrt(
        max(atlasWorldFrequency.x * atlasWorldFrequency.y, 0.0001)
    );
    vec3 atlasProjectionPosition = terrainPosition * vec3(
        atlasWorldFrequency.x,
        atlasVerticalFrequency,
        atlasWorldFrequency.y
    );
    vec2 atlasUvX = atlasProjectionPosition.zy * textureScale;
    vec2 atlasUvY = atlasUv;
    vec2 atlasUvZ = atlasProjectionPosition.xy * textureScale;
    float materialBias = clamp(fragVertexColor.z, 0.0, 1.0);
    float materialContrast = mix(0.7, 1.35, clamp(fragVertexColor.w / 1.63, 0.0, 1.0));
    float regionalNoise = waveNoise(fragWorldPosition.xz * (0.7 + materialContrast * 0.3));
    regionalNoise = clamp(
        0.5 + (regionalNoise - 0.5) * materialContrast + (materialBias - 0.5) * 0.18,
        0.0,
        1.0
    );

    float snow = smoothstep(0.67, 0.9, height + slope * 0.18);
    float rock = smoothstep(0.18, 0.62, slope) * (1.0 - snow);
    float sand = smoothstep(0.02, 0.36, 1.0 - height) *
        smoothstep(0.42, 0.78, regionalNoise) * (1.0 - rock) * (1.0 - snow);
    float mud = smoothstep(0.1, 0.6, 1.0 - height) *
        (1.0 - regionalNoise) * 0.65 * (1.0 - rock) * (1.0 - snow);
    float gravel = smoothstep(0.1, 0.42, slope) *
        smoothstep(0.2, 0.75, 1.0 - height) * 0.42 * (1.0 - snow);
    float ash = smoothstep(0.35, 0.82, height) * regionalNoise *
        0.28 * (1.0 - snow) * (1.0 - rock);
    float dirt = smoothstep(0.22, 0.72, 1.0 - height) *
        (1.0 - rock) * (1.0 - snow) * 0.52;

    float usedWeight = snow + rock + sand + mud + gravel + ash + dirt;
    float grass = max(0.08, 1.0 - usedWeight);
    float totalWeight = grass + snow + rock + sand + mud + gravel + ash + dirt;
    grass /= totalWeight;
    snow /= totalWeight;
    rock /= totalWeight;
    sand /= totalWeight;
    mud /= totalWeight;
    gravel /= totalWeight;
    ash /= totalWeight;
    dirt /= totalWeight;

    vec3 atlasAlbedo =
        sampleLandformTriplanar(0, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * grass +
        sampleLandformTriplanar(1, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * dirt +
        sampleLandformTriplanar(2, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * rock +
        sampleLandformTriplanar(3, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * sand +
        sampleLandformTriplanar(4, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * snow +
        sampleLandformTriplanar(5, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * mud +
        sampleLandformTriplanar(6, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * gravel +
        sampleLandformTriplanar(7, atlasUvX, atlasUvY, atlasUvZ, projectionWeights) * ash;

    vec3 albedo = atlasAlbedo;
    if ((material.textureFlags & 8u) != 0u) {
        // The camera position is normalized together with the scene. Divide
        // by the same scale to make these thresholds meaningful in terrain
        // world units for both player and editor cameras.
        float distanceWorld = distance(
            ubo.cameraPosition.xyz,
            fragWorldPosition
        ) / sceneScale;
        const float kDetailStartDistance = 10.0;
        const float kDetailEndDistance = 36.0;
        float detailBlend = 1.0 - smoothstep(
            kDetailStartDistance,
            kDetailEndDistance,
            distanceWorld
        );
        // Avoid fetching the eight high-resolution layers once the fragment
        // is fully in the broad-distance LOD. The transition band blends both
        // representations to avoid a visible texture pop.
        if (detailBlend > 0.001) {
            vec3 detailAlbedo =
                sampleDetailLandformTriplanar(0, detailUvX, detailUvY, detailUvZ, projectionWeights) * grass +
                sampleDetailLandformTriplanar(1, detailUvX, detailUvY, detailUvZ, projectionWeights) * dirt +
                sampleDetailLandformTriplanar(2, detailUvX, detailUvY, detailUvZ, projectionWeights) * rock +
                sampleDetailLandformTriplanar(3, detailUvX, detailUvY, detailUvZ, projectionWeights) * sand +
                sampleDetailLandformTriplanar(4, detailUvX, detailUvY, detailUvZ, projectionWeights) * snow +
                sampleDetailLandformTriplanar(5, detailUvX, detailUvY, detailUvZ, projectionWeights) * mud +
                sampleDetailLandformTriplanar(6, detailUvX, detailUvY, detailUvZ, projectionWeights) * gravel +
                sampleDetailLandformTriplanar(7, detailUvX, detailUvY, detailUvZ, projectionWeights) * ash;
            albedo = mix(atlasAlbedo, detailAlbedo, detailBlend);
        }
    }

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    // Quantizing diffuse light into three broad bands makes every geometric
    // triangle read as a hard triangular patch, especially on distant hills.
    // Keep the terrain lighting continuous so the remaining geometry is not
    // visually amplified by the stylized light bands.
    float lightBand = diffuseFactor;
    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) *
        max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) *
        max(ubo.sunLightDirection.w, 0.0);
    vec3 litColor = albedo * (ambientLight + diffuseLight * (0.78 + lightBand * 0.34));
    outColor = vec4(litColor * material.baseColor.rgb, material.opacity);
}

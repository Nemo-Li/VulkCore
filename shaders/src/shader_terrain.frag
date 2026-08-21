#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in float fragTerrainHeight;

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
    vec2 detailUv = (fragWorldPosition.xz / sceneScale) * textureScale;
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
        sampleLandform(0, atlasUv) * grass +
        sampleLandform(1, atlasUv) * dirt +
        sampleLandform(2, atlasUv) * rock +
        sampleLandform(3, atlasUv) * sand +
        sampleLandform(4, atlasUv) * snow +
        sampleLandform(5, atlasUv) * mud +
        sampleLandform(6, atlasUv) * gravel +
        sampleLandform(7, atlasUv) * ash;

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
                sampleDetailLandform(0, detailUv) * grass +
                sampleDetailLandform(1, detailUv) * dirt +
                sampleDetailLandform(2, detailUv) * rock +
                sampleDetailLandform(3, detailUv) * sand +
                sampleDetailLandform(4, detailUv) * snow +
                sampleDetailLandform(5, detailUv) * mud +
                sampleDetailLandform(6, detailUv) * gravel +
                sampleDetailLandform(7, detailUv) * ash;
            albedo = mix(atlasAlbedo, detailAlbedo, detailBlend);
        }
    }

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    float lightBand = floor(diffuseFactor * 3.0) / 3.0;
    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) *
        max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) *
        max(ubo.sunLightDirection.w, 0.0);
    vec3 litColor = albedo * (ambientLight + diffuseLight * (0.78 + lightBand * 0.34));
    outColor = vec4(litColor * material.baseColor.rgb, material.opacity);
}

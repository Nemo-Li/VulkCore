#version 450
#extension GL_GOOGLE_include_directive : require

// 地形着色。根据海拔、坡度和区域控制图分配地貌权重，再用三平面投影采样细节。
// 由近到远逐步减少高频采样，用平均颜色和较大尺度的地貌起伏维持远景层次。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec3 fragTerrainPosition;
layout(location = 5) in vec4 fragTerrainMapping;

#include "scene_fog.glsl"
#include "meadow_shading.glsl"
#include "sun_shadow.glsl"

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
const int kGrassLayer = 0;
const int kRockLayer = 2;
// 三个投影分别丢弃 X、Y、Z 分量。基向量把二维纹理梯度放回对应的三维平面。
const mat2x3 kProjectionBases[3] = mat2x3[3](
    mat2x3(vec3(0, 0, 1), vec3(0, 1, 0)),
    mat2x3(vec3(1, 0, 0), vec3(0, 0, 1)),
    mat2x3(vec3(1, 0, 0), vec3(0, 1, 0))
);
// 生成贴图的线性 RGB 均值，顺序与纹理数组和 landforms 一致。
const vec3 kLandformAverages[kLandformCount] = vec3[kLandformCount](
    vec3(0.239, 0.358, 0.101), vec3(0.423, 0.276, 0.115),
    vec3(0.246, 0.261, 0.226), vec3(0.634, 0.436, 0.179),
    vec3(0.627, 0.731, 0.765), vec3(0.170, 0.130, 0.063),
    vec3(0.316, 0.283, 0.191), vec3(0.173, 0.125, 0.102)
);
// 远景草色取自线性色样，并按场景光照和雾调节强度。
const vec3 kDistantGrassAlbedoScale = vec3(0.88, 0.91, 0.84);
const vec3 kDistantGrassDark = vec3(0.366, 0.527, 0.254) * kDistantGrassAlbedoScale;
const vec3 kDistantGrassLight = vec3(0.413, 0.584, 0.279) * kDistantGrassAlbedoScale;
const vec3 kFarMeadowAlbedo = vec3(0.32, 0.36, 0.25);
const vec3 kLuminance = vec3(0.2126, 0.7152, 0.0722);

float hash21(vec2 cell) {
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// 值噪声同时输出解析梯度。对插值式求导，用平滑因子 3f²−2f³ 的导数 6f(1−f)。
// 因此同一组格点可同时驱动颜色和凹凸，省去额外噪声采样。
vec3 valueNoiseWithGradient(vec2 position) {
    vec2 cell = floor(position);
    vec2 f = fract(position);
    vec2 t = f * f * (3.0 - 2.0 * f);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1, 0));
    float c = hash21(cell + vec2(0, 1));
    float d = hash21(cell + vec2(1, 1));
    float bottom = mix(a, b, t.x);
    float top = mix(c, d, t.x);
    vec2 gradient = vec2(mix(b - a, d - c, t.y), top - bottom) * (6.0 * f * (1.0 - f));
    return vec3(mix(bottom, top, t.y), gradient);
}

float valueNoise(vec2 position) {
    return valueNoiseWithGradient(position).x;
}

vec3 distantGrassNoise(vec2 groundPosition, float footprint, float cellSize) {
    // 用屏幕像素的地形足迹估计纹理频率。图案趋近亚像素时一起淡出色差和法线扰动。
    float visibility = 1.0 - smoothstep(0.30, 0.85, footprint / cellSize);
    if (visibility <= 0.0) return vec3(0.0);
    vec3 noise = valueNoiseWithGradient(groundPosition / cellSize);
    return (noise - vec3(0.5, 0.0, 0.0)) * visibility;
}

// 在逐像素分支和 UV 折返之前保存梯度，使 textureGrad 获得连续的像素足迹。
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
    // 对未折返的 UV 求梯度，并限制 mip 足迹落在留边宽度内，减轻图集相邻格的颜色渗漏。
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
        // 把切线空间法线转为二维斜率以便混合。限制分母以免接近水平的法线产生尖峰。
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
    // 将 UV 转到三角格，在所在三角形的三个角点各生成一个随机采样。
    // 相邻三角形在共享边上有相同的两个非零候选，避免出现拼块接缝。
    const mat2 toLattice = mat2(1.0, 0.0, -0.57735027, 1.15470054);
    vec2 grid = toLattice * (p.uv * 0.9 + gridWarp);
    vec2 cell = floor(grid);
    vec2 f = fract(grid);
    float diagonal = 1.0 - f.x - f.y;
    bool lowerTriangle = diagonal >= 0.0;
    vec2 vertices[3] = vec2[3](cell + vec2(lowerTriangle ? 0.0 : 1.0),
                                cell + vec2(1.0, 0.0), cell + vec2(0.0, 1.0));
    vec3 weights = lowerTriangle ? vec3(diagonal, f) : vec3(-diagonal, 1.0 - f.yx);
    // 加强主导采样并去掉极低权重，再归一化以保持混合亮度。
    weights = max(weights * weights - 0.01, vec3(0.0));
    weights /= dot(weights, vec3(1.0));

    // 每轴只生成一组稳定的旋转、缩放和平移，供所有地貌层复用以打散平铺规律。
    for (int corner = 0; corner < 3; ++corner) {
        vec2 seed = vertices[corner] + float(axis) * vec2(37.0, -19.0);
        vec2 offset = vec2(hash21(seed + vec2(5.4, 13.7)), hash21(seed + vec2(-9.8, 3.1)));
        float angle = mix(-0.68, 0.68, hash21(seed + vec2(8.2, -12.3)));
        float scale = mix(0.75, 1.35, hash21(seed + vec2(19.8, 2.6)));
        float c = cos(angle);
        float s = sin(angle);
        mat2 transform = scale * mat2(c, s, -s, c);
        // 只变换原投影梯度，避免对跨随机块跳变的 UV 求导而选错 mip。
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
        // 若采样坐标 q = A·p + b，则梯度由链式法则以 A 的转置拉回原坐标。
        result.gradient += transpose(region.transform) * surface.gradient * region.weight;
        result.occlusion += surface.occlusion * region.weight;
    }
    return result;
}

void main() {
    vec3 geometryNormal = normalize(fragNormal);
    vec3 shadowNormal = sunReceiverNormal(fragWorldPosition);
    float up = clamp(geometryNormal.y, 0.0, 1.0);
    float slope = 1.0 - up;
    vec3 terrainPosition = fragTerrainPosition;
    float elevation = terrainPosition.y;
    float sceneScale = max(abs(ubo.cameraPosition.w), 0.000001);
    vec3 cameraRay = (fragWorldPosition - ubo.cameraPosition.xyz) / sceneScale;
    float distanceWorld = length(cameraRay);
    vec2 textureScale = clamp(fragVertexColor.xy, vec2(0.05), vec2(2.0));
    // 投影尺度来自地形记录。保留较宽的纹样，使近处仍像大块手绘笔触。
    vec2 frequency = 0.68 * textureScale;
    // 同时按距离和像素覆盖范围淡出细节。后者能处理斜视地面时快速增大的采样足迹。
    float distanceFade = smoothstep(150.0, 520.0, distanceWorld);
    float footprint = length(fwidth(terrainPosition));
    float detailWeight = (1.0 - distanceFade) * (1.0 - smoothstep(0.7, 3.0, footprint));
    float gradientScale = mix(1.0, 2.2, distanceFade);
    bool hasAlbedo = (material.textureFlags & kAlbedoLayers) != 0u;
    bool hasGrassGround = (material.textureFlags & kGrassGroundTexture) != 0u;
    // binding 1 为兼容路径提供图集，也可在数组层可用时承担整片地形的控制图。
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

    // 宏观区域仍按 XZ 定位，局部地貌噪声加入高度，避免陡崖不同高度共享采样而拉出竖纹。
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
    // 积雪优先覆盖高处缓坡。区域控制和噪声改变雪线，陡峭崖面保留裸岩。
    float snowElevation = elevation + (controls.r - 0.5) * 120.0 + (ledge - 0.5) * 88.0;
    float snow = smoothstep(600.0, 760.0, snowElevation) * (1.0 - smoothstep(0.10, 0.37, brokenSlope));
    // 植被密集时提高露岩所需的坡度。剩余覆盖按优先级依次分给灰、沙、泥、砾石、土和草。
    // 每次只从剩余份额中分配，确保地貌权重总和为 1。
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

    // 中远景向裸岩转移部分草地和土壤权重，使山体轮廓在近处细节淡出后仍清晰。
    float mountainSlope = smoothstep(0.035, 0.19, brokenSlope);
    float mountain = max(mountainSlope, smoothstep(135.0, 395.0, elevation));
    float distantMountain = smoothstep(60.0, 375.0, distanceWorld) * mountain;
    float cliff = smoothstep(0.095, 0.32, brokenSlope);
    float exposure = max(distantMountain * mix(0.96, 0.78, vegetation), cliff * 0.59);
    float exposedGrass = grass * exposure;
    float exposedDirt = dirt * exposure * mountainSlope * 0.79;
    float stoneShare = mix(0.72, 0.96, mountainSlope);
    grass -= exposedGrass;
    rock += exposedGrass * stoneShare + exposedDirt;
    dirt += exposedGrass * (1.0 - stoneShare) - exposedDirt;
    float landforms[kLandformCount] = float[kLandformCount](
        grass, dirt, rock, sand, snow, mud, gravel, ash
    );

    // 用旋转、扭曲后的中尺度噪声模拟矿物色带和风化，超出近景贴图范围后仍有地貌变化。
    const mat2 turnRock = mat2(0.8, 0.6, -0.6, 0.8);
    vec2 mineralPosition = turnRock * facePosition;
    vec2 mineralWarp = vec2(ledge, valueNoise(mineralPosition / 53.0 + vec2(13.6, 24.8))) - 0.5;
    float mineral = valueNoise(mineralPosition / 34.0 + mineralWarp * 1.8);
    float warmStone = smoothstep(0.36, 0.71, 0.72 * controls.g + 0.15 * mineral + 0.13 * highland);
    vec3 rockTint = mix(vec3(0.88, 0.99, 1.06), vec3(1.72, 0.78, 0.69), warmStone);
    float bedHeight = (elevation + (region - 0.5) * 50.0 + mineralWarp.y * 37.0) / 32.0;
    float bed = valueNoise(vec2(bedHeight, mineralPosition.x / 130.0 + mineralWarp.x));
    float weathering = 0.22 * (mineral - 0.5) + 0.13 * (brush - 0.5);
    rockTint *= mix(0.82, 1.13, smoothstep(0.24, 0.78, bed)) * (1.0 + weathering);

    // 归一化 |N|⁴ 后按轴混合投影，突出最正对表面的投影。平滑压低弱轴以少采样并减轻拉伸。
    vec3 axes = pow(abs(geometryNormal), vec3(4.0));
    axes *= smoothstep(vec3(0.006), vec3(0.022), axes);
    axes /= dot(axes, vec3(1.0));
    bool hasSurfaceLayers = (material.textureFlags & kSurfaceLayers) != 0u && hasAlbedo;

    // 岩板使用更大的投影尺度，竖向周期较短以表现层理。用区域扰动打散规则裂纹。
    vec3 rockPosition = terrainPosition / vec3(82.0, 42.0, 82.0) +
                        0.27 * vec3(mineralWarp.x, mineral - 0.5, mineralWarp.y);
    Projection rockProjections[3] = Projection[3](
        projectUv(rockPosition.zy), projectUv(rockPosition.xz), projectUv(rockPosition.xy)
    );
    float rockDetail = (1.0 - smoothstep(4.0, 24.0, footprint)) *
                       mix(0.65, 1.0, smoothstep(15.0, 100.0, distanceWorld));
    vec3 slabBump = vec3(0.0);
    float rockVisibility = 1.0;
    if (rock > 0.01 && rockDetail > 0.0) {
        vec3 slabColor = vec3(0.0);
        vec3 slabGradient = vec3(0.0);
        float cavity = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            if (axes[axis] == 0.0) continue;
            TerrainSample slab = sampleTerrain(kRockLayer, rockProjections[axis],
                                               hasAlbedo, hasSurfaceLayers, 1.0);
            slabColor += slab.albedo * axes[axis];
            slabGradient += kProjectionBases[axis] * slab.gradient * axes[axis];
            if (hasSurfaceLayers) cavity += (1.0 - slab.occlusion) * axes[axis];
        }
        float visibility = rockDetail * smoothstep(0.01, 0.10, rock);
        // 用贴图均值把岩板色转为相对倍率，保留由区域控制图计算出的岩石底色。
        vec3 stoneTexture = slabColor / kLandformAverages[kRockLayer];
        rockTint *= mix(vec3(1.0), clamp((stoneTexture - 1.0) * 1.55 + 1.0, 0.42, 1.54), visibility);
        slabBump = slabGradient * (2.6 * rock * visibility);
        rockVisibility = 1.0 - rock * visibility * min(0.55, cavity * 2.5);
    }

    // 程序草提供地表草贴图淡出后的底色和中尺度凹凸，并在最远处收敛到低对比颜色。
    float grassGroundDistance = length(cameraRay.xz);
    float paleMeadow = smoothstep(45.0, 235.0, distanceWorld);
    float farMeadow = smoothstep(245.0, 980.0, hasGrassGround ? grassGroundDistance : distanceWorld);
    vec3 grassAlbedo = kMeadowAlbedo;
    vec3 distantGrassBump = vec3(0.0);
    if (grass > 0.0 && paleMeadow > 0.0) {
        vec3 patches = distantGrassNoise(groundPosition, footprint, 24.0);
        vec3 grain = distantGrassNoise(groundPosition + vec2(37.5, -16.0), footprint, 7.5);
        float shade = clamp(0.5 + 0.36 * (vegetation - 0.5) + patches.x + 0.55 * grain.x, 0.0, 1.0);
        vec3 paleGrass = mix(kDistantGrassDark, kDistantGrassLight, shade);
        vec3 distantGrass = mix(paleGrass, kFarMeadowAlbedo, farMeadow) * (1.0 + 0.14 * grain.x);
        grassAlbedo = mix(kMeadowAlbedo, distantGrass, paleMeadow);
        vec2 bump = -(0.05 * patches.yz + 0.11 * grain.yz);
        distantGrassBump = vec3(bump.x, 0.0, bump.y) * (grass * paleMeadow);
    }

    vec2 grassFadeRange = material.waterFlow.zw;
    if (hasGrassGround && grass > 0.0 && farMeadow < 1.0) {
        vec3 authoredGrass = meadowGroundAlbedo(groundPosition, grassGroundDistance, grassFadeRange);
        grassAlbedo = mix(authoredGrass, grassAlbedo, farMeadow);
        distantGrassBump *= farMeadow;
    }

    // 先混合宏观底色，再按距离和像素尺寸决定是否为各材质和投影轴补采细节。
    vec3 macroAlbedo = vec3(0.0);
    for (int layer = 0; layer < kLandformCount; ++layer) {
        vec3 layerAlbedo = layer == kGrassLayer ? grassAlbedo : kLandformAverages[layer];
        if (layer == kRockLayer) layerAlbedo *= rockTint;
        macroAlbedo += landforms[layer] * layerAlbedo;
    }

    float surfaceFade = (1.0 - smoothstep(28.0, 110.0, distanceWorld)) * detailWeight;
    float rockSurfaceFade = (1.0 - smoothstep(64.0, 280.0, distanceWorld)) * detailWeight;
    vec3 albedo = macroAlbedo;
    vec3 normalOffset = vec3(0.0);
    float ao = 1.0;
    if (detailWeight > 0.0) {
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
                sampledWeight += weight;
                if (layer == kGrassLayer && hasGrassGround) {
                    // 保留原始地表草纹理的色相与纹样，使远近两条路径过渡一致。
                    detailAlbedo += grassAlbedo * weight;
                    continue;
                }
                bool isRock = layer == kRockLayer;
                float layerSurfaceFade = isRock ? rockSurfaceFade : surfaceFade;
                bool sampleSurface = hasSurfaceLayers && layerSurfaceFade > 0.0;
                TerrainSample surface = sampleStochasticTerrain(layer, patches, hasAlbedo, sampleSurface, gradientScale);
                vec3 layerAlbedo = surface.albedo;
                if (layer == kGrassLayer) {
                    // 复用细节贴图的明暗变化，保留草叶与地表共享的色相。
                    float relativeLuminance = dot(surface.albedo, kLuminance) /
                                              dot(kLandformAverages[kGrassLayer], kLuminance);
                    layerAlbedo = grassAlbedo * mix(1.0, relativeLuminance, 0.65);
                } else if (isRock) {
                    layerAlbedo *= rockTint;
                }
                detailAlbedo += layerAlbedo * weight;
                if (sampleSurface) {
                    float bumpStrength = layerSurfaceFade * (isRock ? 1.95 : 0.78);
                    normalOffset += kProjectionBases[axis] * surface.gradient * (weight * bumpStrength);
                    occlusion += (1.0 - surface.occlusion) * (weight * layerSurfaceFade);
                }
            }
        }
        // 低权重层已被跳过或衰减。按实际权重重新归一化，防止剩余项整体变暗。
        float inverseWeight = 1.0 / max(sampledWeight, 0.0001);
        albedo = mix(macroAlbedo, detailAlbedo * inverseWeight, detailWeight);
        normalOffset *= inverseWeight;
        ao = 1.0 - occlusion * inverseWeight;
    }
    albedo *= mix(0.85, 1.15, controls.a);
    vec3 bump = normalOffset + slabBump + distantGrassBump;
    // 将累加的扰动投影到切平面，避免沿原法线的分量改变凹凸强度。
    bump -= geometryNormal * dot(geometryNormal, bump);
    vec3 normal = normalize(geometryNormal + bump);

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    float facing = dot(normal, lightDirection);
    // 草地沿用共享的柔和过渡，山体使用更清晰的明暗交界与偏冷的背光填充。
    float relief = clamp(0.68 * (1.0 - grass) + 0.66 * mountain, 0.0, 1.0);
    float diffuse = mix(meadowDiffuse(facing), smoothstep(0.04, 0.82, facing), relief);
    // 岩石使用略压暗的余弦漫反射。凹凸法线影响直射光，表面遮蔽另行调节环境光。
    float stoneDiffuse = max(facing, 0.0);
    stoneDiffuse *= 0.78 + 0.22 * stoneDiffuse;
    diffuse = mix(diffuse, stoneDiffuse, rock);
    float shadedFace = 1.0 - smoothstep(-0.18, 0.49, dot(geometryNormal, lightDirection));
    float skyVisibility = mix(1.0, mix(0.70, 0.96, up), relief);
    vec3 shadeTint = mix(vec3(1.0), vec3(0.53, 0.64, 0.80), shadedFace * relief);
    vec3 ambient = ubo.ambientLight.rgb * ubo.ambientLight.a * skyVisibility * ao * shadeTint * rockVisibility;
    vec3 sunlight = ubo.sunLightColor.rgb * ubo.sunLightDirection.w * diffuse *
                    mix(0.85, 1.04, controls.a) * mix(0.50, 1.0, rockVisibility);
    float visibility = sunVisibility(fragWorldPosition, shadowNormal);
    vec3 litTerrain = albedo * (ambient + sunlight * visibility);
    if (grassFadeRange.y > grassFadeRange.x && grass > 0.58 && grassGroundDistance < 1.75 * grassFadeRange.y) {
        // 草地占主要权重时使用草叶淡出的目标色。保留混合地貌上的过渡。
        float meadowWeight = smoothstep(0.58, 0.82, grass) *
            (1.0 - smoothstep(grassFadeRange.y, 1.75 * grassFadeRange.y, grassGroundDistance));
        float detail = meadowBladeDetail(grassGroundDistance, grassFadeRange.x);
        vec3 groundAlbedo = meadowGroundTint(hasGrassGround ? grassAlbedo : kMeadowAlbedo, detail);
        vec3 groundLight = meadowGroundLighting(dot(geometryNormal, lightDirection),
            ubo.ambientLight.rgb * ubo.ambientLight.a,
            ubo.sunLightColor.rgb * ubo.sunLightDirection.w * visibility);
        litTerrain = mix(litTerrain, groundAlbedo * groundLight, meadowWeight);
    }

    outColor = vec4(applySceneFog(litTerrain * material.baseColor.rgb,
        sceneFogAt(fragWorldPosition, true)), material.opacity);
}

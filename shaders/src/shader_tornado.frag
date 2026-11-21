#version 450
#extension GL_GOOGLE_include_directive : require
#include "sun_shadow.glsl"
#include "sun_shadow_coverage.glsl"

// 龙卷风的发光片元材质。用网格 UV 重建各层密度与高亮纹样，供加法混合累积。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec3 fragLocalPosition;
layout(location = 5) in vec3 fragLayerMotion;

layout(set = 0, binding = 1) uniform sampler2D tornadoTexture;
layout(set = 0, binding = 3) uniform sampler2D distortionTexture;

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

SUN_SHADOW_OUTPUT

const float TAU = 6.28318530718;

float hash11(float value) {
    return fract(sin(value * 127.1 + 311.7) * 43758.5453123);
}

// 用到中心的距离生成软带。带宽和羽化分别控制亮芯与外缘过渡。
float softBand(float value, float center, float halfWidth, float feather) {
    float distanceFromCenter = abs(value - center);
    return 1.0 - smoothstep(halfWidth, halfWidth + feather, distanceFromCenter);
}

float stripEndFade(float along) {
    return smoothstep(0.0, 0.075, along) *
        (1.0 - smoothstep(0.88, 1.0, along));
}

float stripSideFade(float across) {
    return smoothstep(0.0, 0.10, across) *
        (1.0 - smoothstep(0.90, 1.0, across));
}

vec2 fallbackNoise(vec2 point) {
    return vec2(
        0.5 + 0.5 * sin(point.x * TAU + sin(point.y * TAU)),
        0.5 + 0.5 * cos(point.y * TAU - cos(point.x * TAU))
    );
}

vec2 sampleDistortion(vec2 coordinate) {
    // binding 3 复用法线贴图的槽位，但这里的 RG 保存扭曲噪声。
    if ((material.textureFlags & 2u) == 0u) {
        return fallbackNoise(coordinate);
    }
    return texture(distortionTexture, fract(coordinate)).rg;
}

// 用错开半周期的两个相位限制平流偏移，按三角波权重交叉混合。
// 交换坐标轴、改变频率与偏移，使两次纹理读取带来不同尺度的流动细节。
vec2 flowMappedDistortion(
    vec2 coordinate,
    float seed,
    float elapsed,
    vec2 direction,
    float speed,
    float frequency
) {
    float phaseA = fract(elapsed * speed);
    float phaseB = fract(elapsed * speed + 0.5);
    float phaseBlend = abs(phaseA * 2.0 - 1.0);
    vec2 offset = vec2(seed * 0.071, seed * 0.113);
    vec2 sampleA = sampleDistortion(
        coordinate * frequency - direction * phaseA + offset
    );
    vec2 sampleB = sampleDistortion(
        coordinate.yx * (frequency * 1.87) - direction.yx * phaseB +
        offset.yx * vec2(1.31, -0.83)
    ).gr;
    return mix(sampleB, sampleA, phaseBlend) * 2.0 - 1.0;
}

// 域扭曲将粗流场作为第二次采样的坐标偏移。局部纹理会卷曲和折叠，形成涡流的外观。
vec2 domainWarp(
    vec2 coordinate,
    float seed,
    float elapsed,
    vec2 direction,
    float speed,
    float frequency
) {
    vec2 coarse = flowMappedDistortion(
        coordinate,
        seed,
        elapsed,
        direction,
        speed,
        frequency
    );
    vec2 detail = flowMappedDistortion(
        coordinate + coarse * 0.18,
        seed + 17.0,
        elapsed,
        vec2(-direction.y, direction.x) * 1.37,
        speed * 1.71,
        frequency * 2.23
    );
    return coarse * 0.64 + detail * 0.36;
}

void main() {
    float elapsed = ubo.time.x;
    // 与顶点阶段解码同样的 UV 区段，保证几何动画与发光纹样选择同一类部件。
    int kind = int(floor(fragTexCoord.y * 0.5 + 0.0001));
    float seed = floor(fragTexCoord.x * 0.5 + 0.0001);
    float along = fract(fragTexCoord.x);
    float across = clamp(fragTexCoord.y - float(kind) * 2.0, 0.0, 1.0);
    float randomPhase = hash11(seed) * TAU;

    vec3 viewDirection = normalize(ubo.cameraPosition.xyz - fragWorldPosition);
    // 部件为双面材质。用 |N·V| 使正反两侧都能在掠视角获得边缘光。
    float fresnel = pow(
        1.0 - abs(dot(normalize(fragNormal), viewDirection)),
        1.55
    );

    vec3 deepBlue = vec3(0.025, 0.075, 0.42);
    vec3 electricBlue = vec3(0.075, 0.36, 1.00);
    vec3 iceBlue = vec3(0.42, 0.82, 1.00);
    vec3 whiteCore = vec3(0.94, 0.99, 1.00);
    vec3 violetEdge = vec3(0.38, 0.16, 1.00);

    vec3 color = electricBlue;
    float alpha = 0.0;
    float emissiveEdge = 0.0;

    if (kind == 0) {
        // 角度与高度的线性相位在圆柱壳上形成螺旋。叠加域扭曲和顶点流场使内部光带随外形摆动。
        float heightRatio = across;
        float shell = floor(fragTexCoord.x * 0.5 + 0.0001);
        float angular = fract(fragTexCoord.x);
        vec2 warped = domainWarp(
            vec2(angular, heightRatio),
            seed,
            elapsed,
            vec2(0.31 + shell * 0.07, -0.68),
            0.083 + shell * 0.011,
            1.28 + shell * 0.19
        );
        warped = warped * 0.78 + fragLayerMotion.xy * 0.22;
        float phaseBreath = sin(
            elapsed * 0.71 + heightRatio * TAU * 1.4 + randomPhase
        );
        float spiralPhaseA = fract(
            angular * (2.0 + shell * 0.31) -
            heightRatio * (3.05 + phaseBreath * 0.24) +
            warped.x * 0.31 +
            sin(elapsed * 0.43 + heightRatio * 7.0) * 0.055
        );
        float spiralPhaseB = fract(
            angular * 3.47 +
            heightRatio * 2.18 +
            warped.y * 0.27 -
            sin(elapsed * 0.59 - heightRatio * 9.0 + shell) * 0.07
        );
        // 在两个不同的相位方向上生成宽亮带和细丝。靠近顶部时用噪声打散渐隐边沿。
        float broadBlade = softBand(
            spiralPhaseA,
            0.50 + warped.y * 0.035,
            0.19 + warped.x * 0.025,
            0.17
        );
        float innerFilament = softBand(
            spiralPhaseB,
            0.50,
            0.045 + abs(warped.y) * 0.018,
            0.09
        );
        float turbulentDensity = smoothstep(
            -0.42,
            0.34,
            warped.x * 0.65 + warped.y * 0.35 + broadBlade * 0.38
        );
        float densityEdge = 1.0 - smoothstep(
            0.055,
            0.19,
            abs(turbulentDensity - 0.48)
        );
        float verticalFade = smoothstep(0.0, 0.075, heightRatio) *
            (1.0 - smoothstep(0.74, 0.99, heightRatio));
        float topBreakup = smoothstep(
            -0.28,
            0.38,
            warped.y + sin(angular * TAU * 3.0 + warped.x * 2.5) * 0.34
        );
        verticalFade *= mix(
            1.0,
            topBreakup,
            smoothstep(0.54, 0.95, heightRatio)
        );
        float coreGlow = pow(1.0 - heightRatio, 1.65);

        color = mix(
            deepBlue,
            electricBlue,
            broadBlade * 0.69 + fresnel * 0.22 + turbulentDensity * 0.16
        );
        color = mix(color, iceBlue, innerFilament * 0.68 + coreGlow * 0.18);
        color = mix(color, whiteCore, innerFilament * coreGlow * 0.44);
        color = mix(color, violetEdge, densityEdge * 0.12);
        alpha = (
            0.008 + broadBlade * 0.072 + innerFilament * 0.105 +
            fresnel * 0.050 + turbulentDensity * 0.026 + coreGlow * 0.030
        ) * verticalFade;
        alpha *= shell < 0.5 ? 1.0 : 0.48;
        emissiveEdge = densityEdge * 0.23;
    } else if (kind == 1) {
        // 相邻主条带反向传播。先扭曲亮带中心，再从噪声的消融阈值附近提取发亮边缘。
        float direction = mod(seed, 2.0) < 0.5 ? -1.0 : 1.0;
        vec2 warped = domainWarp(
            vec2(along, across),
            seed,
            elapsed,
            vec2(direction * 0.72, 0.34 + hash11(seed) * 0.21),
            0.105 + hash11(seed + 4.0) * 0.035,
            1.13 + mod(seed, 4.0) * 0.16
        );
        warped = warped * 0.74 + fragLayerMotion.xy * 0.26;
        float warpedAlong = along + warped.x * 0.052;
        float warpedAcross = across + warped.y * 0.105;
        float travelingFold = sin(
            warpedAlong * TAU * (1.45 + mod(seed, 3.0) * 0.29) -
            elapsed * direction * (1.15 + hash11(seed) * 0.64) +
            randomPhase
        );
        float movingCenter =
            0.50 +
            travelingFold * 0.095 +
            warped.x * 0.07;
        float luminousVein = softBand(
            warpedAcross,
            movingCenter,
            0.045 + abs(warped.y) * 0.025,
            0.13
        );
        float secondaryVein = softBand(
            warpedAcross,
            0.27 + 0.10 * sin(
                warpedAlong * 12.0 + elapsed * direction * 1.7 + seed
            ),
            0.020,
            0.088
        );
        float dissolveField =
            warped.x * 0.58 +
            warped.y * 0.34 +
            sin(warpedAlong * TAU * 4.0 - elapsed * 0.83 + seed) * 0.18;
        float dissolveMask = smoothstep(-0.53, 0.18, dissolveField);
        float erosionEdge = 1.0 - smoothstep(
            0.035,
            0.17,
            abs(dissolveField + 0.22)
        );
        float sideFade = stripSideFade(warpedAcross);
        float endFade = stripEndFade(warpedAlong);
        float edgeTint = pow(abs(warpedAcross * 2.0 - 1.0), 2.1);

        color = mix(deepBlue, electricBlue, 0.46 + warped.x * 0.19);
        color = mix(color, iceBlue, luminousVein * 0.76 + secondaryVein * 0.25);
        color = mix(color, whiteCore, luminousVein * luminousVein * 0.64);
        color = mix(color, violetEdge, edgeTint * 0.24 + erosionEdge * 0.13);
        alpha = sideFade * endFade *
            mix(0.66, 1.0, dissolveMask) *
            (0.14 + luminousVein * 0.48 + secondaryVein * 0.15 + fresnel * 0.14);
        emissiveEdge = erosionEdge * 0.18;
    } else if (kind == 2) {
        // 狭窄的蛇形亮带上叠加移动的虚线和短促脉冲，使细风丝的亮度沿长度变化。
        float direction = mod(seed, 3.0) < 1.0 ? -1.0 : 1.0;
        vec2 warped = domainWarp(
            vec2(along * 1.65, across * 0.72),
            seed,
            elapsed,
            vec2(direction * 1.15, -0.47),
            0.17 + hash11(seed + 2.0) * 0.055,
            1.58 + mod(seed, 5.0) * 0.12
        );
        warped = warped * 0.71 + fragLayerMotion.xy * 0.29;
        float snakeCenter =
            0.50 +
            sin(
                along * TAU * (2.9 + mod(seed, 4.0) * 0.31) -
                elapsed * direction * 2.55 +
                warped.x * 2.8 +
                randomPhase
            ) * 0.105 +
            warped.y * 0.055;
        float centerLine = softBand(
            across,
            snakeCenter,
            0.032 + abs(warped.x) * 0.018,
            0.21
        );
        float dashWave = 0.5 + 0.5 * sin(
            along * TAU * (6.0 + mod(seed, 5.0)) -
            elapsed * direction * (3.4 + hash11(seed) * 1.8) +
            warped.y * 3.6
        );
        float dashMask = smoothstep(0.04, 0.72, dashWave + warped.x * 0.25);
        float pulse = pow(
            0.5 + 0.5 * sin(elapsed * 3.1 + seed * 2.07 + along * 5.0),
            3.0
        );
        color = mix(violetEdge, electricBlue, warped.x * 0.24 + 0.56);
        color = mix(color, iceBlue, centerLine * 0.62 + pulse * 0.21);
        color = mix(color, whiteCore, centerLine * (0.52 + pulse * 0.36));
        alpha = stripEndFade(along) * stripSideFade(across) *
            mix(0.58, 1.0, dashMask) *
            (0.14 + centerLine * 0.67 + pulse * 0.13);
        emissiveEdge = pulse * centerLine * 0.20;
    } else if (kind == 3) {
        // 沿冲击环周向移动压力前缘，并用扇区遮罩弱化部分圆弧。顶点脉冲同步驱动闪光。
        vec2 warped = domainWarp(
            vec2(along, across),
            seed,
            elapsed,
            vec2(0.28, -1.0),
            0.125 + mod(seed, 3.0) * 0.018,
            1.37 + mod(seed, 4.0) * 0.16
        );
        warped = warped * 0.77 + fragLayerMotion.xy * 0.23;
        float shockCenter =
            0.50 +
            sin(
                along * TAU * (5.0 + mod(seed, 3.0)) -
                elapsed * (2.3 + mod(seed, 2.0) * 0.47) +
                warped.x * 2.4
            ) * 0.095 +
            warped.y * 0.045;
        float ringCore = softBand(
            across,
            shockCenter,
            0.042,
            0.22
        );
        float sectorField =
            sin(
                along * TAU * (4.0 + mod(seed, 4.0)) +
                elapsed * (0.73 + mod(seed, 2.0) * 0.31) +
                seed
            ) * 0.55 +
            warped.x * 0.45;
        float broken = smoothstep(-0.42, 0.14, sectorField);
        float reconnectEdge = 1.0 - smoothstep(
            0.04,
            0.20,
            abs(sectorField + 0.16)
        );
        float pressureFlash = pow(
            clamp(fragLayerMotion.z, 0.0, 1.0),
            3.0
        );
        color = mix(electricBlue, iceBlue, 0.43 + ringCore * 0.37);
        color = mix(color, whiteCore, ringCore * (0.68 + pressureFlash * 0.26));
        color = mix(color, violetEdge, reconnectEdge * 0.17);
        float ringLayerStrength = seed < 22.5
            ? 1.0
            : (seed < 23.5 ? 0.58 : 0.72);
        alpha = stripEndFade(along) * stripSideFade(across) *
            (0.13 + ringCore * 0.74 + pressureFlash * 0.12) *
            mix(0.74, 1.0, broken) *
            ringLayerStrength;
        emissiveEdge = reconnectEdge * 0.16;
    } else {
        // 用加权曼哈顿距离裁出菱形碎屑。中心加热到白色，外晕跟随噪声短促闪动。
        vec2 warped = domainWarp(
            vec2(along, across),
            seed,
            elapsed,
            vec2(0.81, -0.63),
            0.21 + hash11(seed) * 0.09,
            2.05
        );
        warped = warped * 0.68 + fragLayerMotion.xy * 0.32;
        vec2 centered = vec2(along, across) * 2.0 - 1.0;
        centered += warped * 0.055;
        float diamond = 1.0 - smoothstep(
            0.43,
            1.0,
            abs(centered.x) * 0.62 + abs(centered.y)
        );
        float shardCore = 1.0 - smoothstep(0.0, 0.39, abs(centered.x));
        float flicker = pow(
            0.5 + 0.5 * sin(
                elapsed * (3.7 + hash11(seed) * 4.1) +
                seed * 2.13 +
                warped.x * 3.0
            ),
            2.2
        );
        color = mix(violetEdge, iceBlue, warped.x * 0.21 + shardCore * 0.55);
        color = mix(color, whiteCore, shardCore * (0.58 + flicker * 0.35));
        alpha = diamond * (0.22 + shardCore * 0.56 + flicker * 0.22);
        emissiveEdge = flicker * diamond * 0.34;
    }

    // 强化消融边缘和流场产生的边界。发光颜色可超出 1，在 HDR 合成中进一步产生 Bloom。
    // Alpha 在加法混合时调节每一层的贡献，极低权重的片元直接丢弃。
    color = mix(color, whiteCore, clamp(emissiveEdge, 0.0, 0.48));
    color *= material.baseColor.rgb * fragVertexColor.rgb * mix(0.72, 1.0, sunVisibility(fragWorldPosition));
    color += material.waterSurface.rgb * material.waterSurface.a *
        (0.085 + emissiveEdge * 0.12);
    alpha *= material.baseColor.a * fragVertexColor.a * material.opacity;
    if (alpha <= 0.004) {
        discard;
    }
    SUN_SHADOW_COLOR(vec4(color, clamp(alpha, 0.0, 1.0)));
}

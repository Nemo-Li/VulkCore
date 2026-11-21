#version 450
#extension GL_GOOGLE_include_directive : require

// 常规水面。组合程序波、可选法线贴图、地形水流数据与不透明场景颜色快照。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

#include "scene_fog.glsl"
#include "sun_shadow.glsl"

layout(set = 0, binding = 1) uniform sampler2D textureSampler;
// 渲染器在画水面前提供不透明颜色快照，使采样来源与水面正在写入的颜色附件分离。
layout(set = 0, binding = 2) uniform sampler2D sceneColorTexture;
layout(set = 0, binding = 3) uniform sampler2D normalTexture;
layout(set = 0, binding = 4) uniform sampler2D specularTexture;

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

// UV 在流向上随时间偏移。不同方向的正弦分量叠加出主波纹，额外分量控制扰动。
float waveHeight(
    vec2 uv,
    float time,
    vec2 flowDirection,
    float flowSpeed,
    float flowTurbulence,
    float waveScale
) {
    vec2 movingUv = uv * waveScale + flowDirection * flowSpeed * time * 0.12;
    float waveA = sin(dot(movingUv, vec2(8.0, 5.0)) + time * 0.15);
    float waveB = sin(dot(movingUv, vec2(-4.0, 7.0)) - time * 0.1 + 1.4);
    float waveC = cos(dot(movingUv, vec2(13.0, -3.0)) + time * 0.08);
    float turbulenceWave = sin(dot(movingUv, vec2(-6.0, 11.0)) + time * 0.18);
    return waveA * 0.48 + waveB * 0.32 + waveC * 0.20 +
        turbulenceWave * flowTurbulence * 0.16;
}

// 与基础材质一致，用 UV 雅可比和位置差分构建场景空间的切线基。
// UV 退化时保留几何法线，以免求逆放大误差。
vec3 applyNormalMap(vec3 geometricNormal, vec3 worldPosition, vec2 uv) {
    vec3 positionDerivativeX = dFdx(worldPosition);
    vec3 positionDerivativeY = dFdy(worldPosition);
    vec2 uvDerivativeX = dFdx(uv);
    vec2 uvDerivativeY = dFdy(uv);
    float determinant = uvDerivativeX.x * uvDerivativeY.y -
        uvDerivativeX.y * uvDerivativeY.x;
    if (abs(determinant) < 0.0001) {
        return normalize(geometricNormal);
    }

    vec3 tangent = normalize(
        (positionDerivativeX * uvDerivativeY.y - positionDerivativeY * uvDerivativeX.y) /
        determinant
    );
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    vec3 bitangent = normalize(cross(geometricNormal, tangent));
    vec3 tangentNormal = texture(normalTexture, uv).xyz * 2.0 - 1.0;
    return normalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        geometricNormal * tangentNormal.z
    );
}

void main() {
    float time = ubo.time.x;
    float directionLength = length(material.waterFlow.xy);
    vec2 flowDirection = directionLength > 0.001
        ? material.waterFlow.xy / directionLength
        : vec2(0.0, 1.0);
    float flowSpeed = max(material.waterFlow.z, 0.0);
    float flowStrength = max(material.waterFlow.w, 0.0);
    float flowTurbulence = clamp(material.waterSurface.x, 0.0, 1.0);
    float waveScale = max(material.waterSurface.y, 0.1);
    vec2 flowOffset = flowDirection * flowSpeed * time * 0.12;

    // terrain_water 网格把有符号流速编码到顶点色 RG，A 较低表示地形水数据。
    // 有足够的局部流速才覆盖材质方向，静水区域保留材质默认流向。
    vec2 extmFlow = fragVertexColor.rg * 2.0 - 1.0;
    float extmFlowLength = length(extmFlow);
    if (fragVertexColor.a < 0.75 && extmFlowLength > 0.025) {
        flowDirection = normalize(extmFlow);
        flowSpeed = max(flowSpeed, min(extmFlowLength, 1.0) * 0.55);
        flowOffset = flowDirection * flowSpeed * time * 0.12;
    }
    float height = waveHeight(
        fragTexCoord,
        time,
        flowDirection,
        flowSpeed,
        flowTurbulence,
        waveScale
    );
    // 在 UV 的两个方向取前向有限差分。epsilon 固定，因此差值直接作为可调强度的斜率近似。
    float epsilon = 0.012;
    float slopeX = waveHeight(
        fragTexCoord + vec2(epsilon, 0.0), time, flowDirection,
        flowSpeed, flowTurbulence, waveScale
    ) - height;
    float slopeY = waveHeight(
        fragTexCoord + vec2(0.0, epsilon), time, flowDirection,
        flowSpeed, flowTurbulence, waveScale
    ) - height;

    // Y-up 水面用高度斜率的负值扰动场景法线的 XZ 分量。这样没有法线贴图也有细波。
    vec3 normal = normalize(fragNormal);
    if ((material.textureFlags & 2u) != 0u) {
        normal = applyNormalMap(normal, fragWorldPosition, fragTexCoord);
    }
    normal = normalize(normal + vec3(
        -slopeX * (0.75 + flowTurbulence * 0.7) * flowStrength,
        0.0,
        -slopeY * (0.75 + flowTurbulence * 0.7) * flowStrength
    ));
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));

    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    vec3 specularLight = mix(vec3(0.72, 0.9, 1.0), diffuseLight, 0.35);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    // 经验 Fresnel 权重使用固定视线方向。掠射角提高反射比重，粗糙度另外控制高光宽度。
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    vec3 halfwayDirection = normalize(lightDirection + viewDirection);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    float opacity = clamp(material.opacity, 0.0, 1.0);
    float sparkle = pow(max(dot(normal, halfwayDirection), 0.0), mix(128.0, 24.0, roughness));

    float specularMapFactor = 1.0;
    if ((material.textureFlags & 4u) != 0u) {
        specularMapFactor = texture(specularTexture, fragTexCoord).r;
    }

    // 用波面高度在深浅色之间过渡，提供随波起伏的颜色变化。
    vec3 deepWater = vec3(0.015, 0.16, 0.24);
    vec3 shallowWater = vec3(0.04, 0.48, 0.58);
    vec3 waterColor = mix(deepWater, shallowWater, height * 0.18 + 0.48);
    if ((material.textureFlags & 1u) != 0u) {
        // 滚动材质贴图补充局部色调，基础波纹由程序函数提供。
        vec3 materialColor = texture(textureSampler, fragTexCoord + flowOffset).rgb;
        waterColor *= mix(vec3(0.72), materialColor, 0.22);
    }
    // 顶点色 B 存放量化的水材质编号，舍入到最近的 0 至 7 档选择色调。
    float waterMaterial = clamp(floor(fragVertexColor.b * 7.0 + 0.5), 0.0, 7.0);
    vec3 materialTint = vec3(1.0);
    if (waterMaterial == 1.0) {
        materialTint = vec3(0.8, 0.96, 1.08);
    } else if (waterMaterial == 2.0) {
        materialTint = vec3(0.78, 0.9, 1.02);
    } else if (waterMaterial == 3.0) {
        materialTint = vec3(0.92, 1.02, 0.9);
    } else if (waterMaterial == 4.0) {
        materialTint = vec3(0.72, 0.9, 0.98);
    } else if (waterMaterial == 5.0) {
        materialTint = vec3(0.68, 0.84, 0.94);
    } else if (waterMaterial >= 6.0) {
        materialTint = vec3(0.88, 0.96, 0.78);
    }
    waterColor *= material.baseColor.rgb * materialTint;

    // 用片元坐标和视口倒数定位场景快照。法线扰动采样点近似折射，翻转屏幕 Y 模拟反射。
    // 将 UV 限在视口内。两次采样只查询颜色，反射范围受当前快照可见内容限制。
    vec2 screenUv = gl_FragCoord.xy * ubo.viewport.zw;
    vec2 refractionUv = clamp(
        screenUv + vec2(normal.x, normal.z) * 0.035,
        vec2(0.001),
        vec2(0.999)
    );
    vec2 reflectionUv = clamp(
        vec2(screenUv.x + normal.x * 0.025, 1.0 - screenUv.y + normal.z * 0.025),
        vec2(0.001),
        vec2(0.999)
    );
    vec3 sceneRefraction = texture(sceneColorTexture, refractionUv).rgb;
    vec3 sceneReflection = texture(sceneColorTexture, reflectionUv).rgb;
    vec3 skyReflection = mix(vec3(0.08, 0.22, 0.32), vec3(0.42, 0.72, 0.82), fresnel);
    // 场景快照已包含雾。雾只混入水面自身的颜色、反射底色，并按透射率衰减高光。
    vec4 fog = sceneFogAt(fragWorldPosition);
    float sunlightVisibility = sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition));
    vec3 litWater = applySceneFog(waterColor * (ambientLight + diffuseLight * diffuseFactor * sunlightVisibility), fog);
    vec3 refractedColor = mix(litWater, sceneRefraction, 0.42);
    vec3 reflectedColor = mix(applySceneFog(skyReflection, fog), sceneReflection, 0.72);
    vec3 color = mix(refractedColor, reflectedColor, 0.22 + fresnel * 0.68);
    color += specularLight * sparkle * mix(0.7, 1.1, metallic) * specularMapFactor *
        (1.0 - fog.a) * sunlightVisibility;

    outColor = vec4(color, (0.76 + fresnel * 0.18) * opacity);
}

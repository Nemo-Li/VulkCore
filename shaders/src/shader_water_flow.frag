#version 450
#extension GL_GOOGLE_include_directive : require

// 流动水面预设。用空间变化的程序流场推动波形和贴图，再混合屏幕折射与伪反射。

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

#include "scene_fog.glsl"
#include "sun_shadow.glsl"

layout(set = 0, binding = 1) uniform sampler2D textureSampler;
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

// 用互相扭曲的正弦和余弦构造平滑的 UV 流向，幅度受限以保留配置的主流向。
vec2 proceduralFlow(vec2 uv) {
    float x = sin(uv.y * 6.0 + sin(uv.x * 2.0) * 1.8);
    float y = cos(uv.x * 4.0 - sin(uv.y * 3.0) * 1.2);
    return normalize(vec2(x * 0.8, y * 0.8) + vec2(0.001)) * 0.34;
}

// 对屏幕空间的 UV 雅可比求逆，从位置导数恢复场景空间的切线。退化 UV 时保留原法线。
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

vec2 configuredFlow(
    vec2 uv,
    vec2 flowDirection,
    float flowSpeed,
    float flowStrength,
    float flowTurbulence
) {
    vec2 proceduralVariation = proceduralFlow(uv);
    return flowDirection * flowSpeed * flowStrength +
        proceduralVariation * flowSpeed * flowTurbulence;
}

float flowWave(
    vec2 uv,
    float time,
    vec2 flowDirection,
    float flowSpeed,
    float flowStrength,
    float flowTurbulence,
    float waveScale
) {
    vec2 scaledUv = uv * waveScale;
    vec2 flow = configuredFlow(scaledUv, flowDirection, flowSpeed, flowStrength, flowTurbulence);
    // 让两组相位沿局部流场相向推进，形成变化的波峰和干涉纹样。
    float primary = sin(dot(scaledUv + flow * time, vec2(15.0, 6.0)));
    float secondary = cos(dot(scaledUv - flow * time * 0.72, vec2(-7.0, 17.0)) + 2.0);
    return primary * 0.55 + secondary * 0.45;
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
    vec2 flow = configuredFlow(
        fragTexCoord * waveScale,
        flowDirection,
        flowSpeed,
        flowStrength,
        flowTurbulence
    );
    float wave = flowWave(
        fragTexCoord,
        time,
        flowDirection,
        flowSpeed,
        flowStrength,
        flowTurbulence,
        waveScale
    );
    // 用固定间距的前向差分取得波形斜率，在此预设中用于扰动法线的 XY 分量。
    float epsilon = 0.01;
    float slopeX = flowWave(
        fragTexCoord + vec2(epsilon, 0.0), time, flowDirection,
        flowSpeed, flowStrength, flowTurbulence, waveScale
    ) - wave;
    float slopeY = flowWave(
        fragTexCoord + vec2(0.0, epsilon), time, flowDirection,
        flowSpeed, flowStrength, flowTurbulence, waveScale
    ) - wave;
    vec3 normal = normalize(fragNormal);
    if ((material.textureFlags & 2u) != 0u) {
        normal = applyNormalMap(normal, fragWorldPosition, fragTexCoord);
    }
    normal = normalize(normal + vec3(
        -slopeX * (0.9 + flowTurbulence * 0.8) * flowStrength,
        -slopeY * (0.9 + flowTurbulence * 0.8) * flowStrength,
        0.0
    ));

    // 将材质贴图 RG 从 [0,1] 解码为有符号流速，供贴图平流使用。缺少贴图时仍有程序流场。
    vec2 sampledFlow = vec2(0.0);
    if ((material.textureFlags & 1u) != 0u) {
        sampledFlow = texture(textureSampler, fragTexCoord * 0.35).rg * 2.0 - 1.0;
        sampledFlow = clamp(sampledFlow, vec2(-1.0), vec2(1.0));
    }
    // RG 只按较小权重加入贴图采样的流速，使主流向仍由材质参数掌控。
    flow += sampledFlow * flowSpeed * flowTurbulence * 0.12;

    // 对两个错相滚动的 UV 做周期性交叉混合，减弱单层纹理匀速平移的观感。
    float phase = time * 0.08;
    vec2 uvA = fragTexCoord + flow * phase;
    vec2 uvB = fragTexCoord + flow * (phase - 0.5);
    float blendFactor = 0.5 + 0.5 * sin(time * max(flowSpeed, 0.01) * 2.4);
    vec3 movingTexture = vec3(1.0);
    if ((material.textureFlags & 1u) != 0u) {
        vec3 sampleA = texture(textureSampler, uvA).rgb;
        vec3 sampleB = texture(textureSampler, uvB).rgb;
        movingTexture = mix(sampleA, sampleB, blendFactor);
    }

    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));
    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    vec3 specularLight = mix(vec3(0.78, 0.94, 1.0), diffuseLight, 0.35);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
    // 用固定视线估计 Fresnel 权重，视线越贴近水面，反射成分越强。
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    vec3 halfwayDirection = normalize(lightDirection + viewDirection);
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float metallic = clamp(material.metallic, 0.0, 1.0);
    float opacity = clamp(material.opacity, 0.0, 1.0);
    float specular = pow(max(dot(normal, halfwayDirection), 0.0), mix(96.0, 16.0, roughness));
    float specularMapFactor = 1.0;
    if ((material.textureFlags & 4u) != 0u) {
        specularMapFactor = texture(specularTexture, fragTexCoord).r;
    }

    vec3 waterColor = mix(
        vec3(0.01, 0.12, 0.2),
        vec3(0.02, 0.55, 0.62),
        wave * 0.18 + 0.5
    );
    waterColor *= mix(vec3(0.9), movingTexture, 0.28);
    waterColor *= material.baseColor.rgb * fragVertexColor.rgb;
    float sunlightVisibility = sunVisibility(fragWorldPosition, sunReceiverNormal(fragWorldPosition));
    waterColor *= ambientLight + diffuseLight * diffuseFactor * sunlightVisibility;

    // 法线 XY 偏移不透明快照的采样点。反射以屏幕 Y 镜像近似，折射直接偏移当前坐标。
    vec2 screenUv = gl_FragCoord.xy * ubo.viewport.zw;
    vec2 refractionUv = clamp(screenUv + normal.xy * 0.045, vec2(0.001), vec2(0.999));
    vec2 reflectionUv = clamp(
        vec2(screenUv.x + normal.x * 0.03, 1.0 - screenUv.y + normal.y * 0.03),
        vec2(0.001),
        vec2(0.999)
    );
    vec3 sceneRefraction = texture(sceneColorTexture, refractionUv).rgb;
    vec3 sceneReflection = texture(sceneColorTexture, reflectionUv).rgb;
    // 已含雾的快照直接参与混合。对本地水色施雾，并用透射率降低高光贡献。
    vec4 fog = sceneFogAt(fragWorldPosition);
    vec3 refractedColor = mix(applySceneFog(waterColor, fog), sceneRefraction, 0.48);
    vec3 reflectedColor = mix(applySceneFog(vec3(0.08, 0.3, 0.42), fog), sceneReflection, 0.78);
    waterColor = mix(refractedColor, reflectedColor, 0.2 + fresnel * 0.7);
    waterColor += specularLight * specular * mix(0.75, 1.15, metallic) * specularMapFactor *
        (1.0 - fog.a) * sunlightVisibility;

    outColor = vec4(waterColor, (0.72 + fresnel * 0.2) * opacity);
}

#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
    vec4 viewport;
    vec4 ambientLight;
    vec4 sunLightDirection;
    vec4 sunLightColor;
} ubo;

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
    float height = waveHeight(
        fragTexCoord,
        time,
        flowDirection,
        flowSpeed,
        flowTurbulence,
        waveScale
    );
    float epsilon = 0.012;
    float slopeX = waveHeight(
        fragTexCoord + vec2(epsilon, 0.0), time, flowDirection,
        flowSpeed, flowTurbulence, waveScale
    ) - height;
    float slopeY = waveHeight(
        fragTexCoord + vec2(0.0, epsilon), time, flowDirection,
        flowSpeed, flowTurbulence, waveScale
    ) - height;

    // The imported demo scene uses Z-up water. Perturbing the view-space
    // normal this way also works when the water mesh has no normal map.
    vec3 normal = normalize(fragNormal);
    if ((material.textureFlags & 2u) != 0u) {
        normal = applyNormalMap(normal, fragWorldPosition, fragTexCoord);
    }
    normal = normalize(normal + vec3(
        -slopeX * (0.75 + flowTurbulence * 0.7) * flowStrength,
        -slopeY * (0.75 + flowTurbulence * 0.7) * flowStrength,
        0.0
    ));
    vec3 lightDirection = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));

    vec3 ambientLight = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 diffuseLight = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    vec3 specularLight = mix(vec3(0.72, 0.9, 1.0), diffuseLight, 0.35);
    float diffuseFactor = max(dot(normal, lightDirection), 0.0);
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

    vec3 deepWater = vec3(0.015, 0.16, 0.24);
    vec3 shallowWater = vec3(0.04, 0.48, 0.58);
    vec3 waterColor = mix(deepWater, shallowWater, height * 0.18 + 0.48);
    if ((material.textureFlags & 1u) != 0u) {
        // The material texture supplies the local tint while the procedural
        // waves keep the effect visible even without a dedicated flow map.
        vec3 materialColor = texture(textureSampler, fragTexCoord + flowOffset).rgb;
        waterColor *= mix(vec3(0.72), materialColor, 0.22);
    }
    waterColor *= material.baseColor.rgb * fragVertexColor.rgb;

    vec2 screenUv = gl_FragCoord.xy * ubo.viewport.zw;
    vec2 refractionUv = clamp(screenUv + normal.xy * 0.035, vec2(0.001), vec2(0.999));
    vec2 reflectionUv = clamp(
        vec2(screenUv.x + normal.x * 0.025, 1.0 - screenUv.y + normal.y * 0.025),
        vec2(0.001),
        vec2(0.999)
    );
    vec3 sceneRefraction = texture(sceneColorTexture, refractionUv).rgb;
    vec3 sceneReflection = texture(sceneColorTexture, reflectionUv).rgb;
    vec3 skyReflection = mix(vec3(0.08, 0.22, 0.32), vec3(0.42, 0.72, 0.82), fresnel);
    vec3 refractedColor = mix(waterColor * (ambientLight + diffuseLight * diffuseFactor), sceneRefraction, 0.42);
    vec3 reflectedColor = mix(skyReflection, sceneReflection, 0.72);
    vec3 color = mix(refractedColor, reflectedColor, 0.22 + fresnel * 0.68);
    color += specularLight * sparkle * mix(0.7, 1.1, metallic) * specularMapFactor;

    outColor = vec4(color, (0.76 + fresnel * 0.18) * opacity);
}

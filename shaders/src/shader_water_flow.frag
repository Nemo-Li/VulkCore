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

// This is the reference project's flow-map operation expressed as a stable
// procedural flow field. A sampled material texture is blended in when one is
// available, so a future RG flow map can be used without changing the layout.
vec2 proceduralFlow(vec2 uv) {
    float x = sin(uv.y * 6.0 + sin(uv.x * 2.0) * 1.8);
    float y = cos(uv.x * 4.0 - sin(uv.y * 3.0) * 1.2);
    return normalize(vec2(x * 0.8, y * 0.8) + vec2(0.001)) * 0.34;
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

    // Convert an RG flow value from [0, 1] to a signed velocity, just like
    // the reference shader. The procedural field acts as the fallback when
    // the node is using its regular diffuse texture rather than a flow map.
    vec2 sampledFlow = vec2(0.0);
    if ((material.textureFlags & 1u) != 0u) {
        sampledFlow = texture(textureSampler, fragTexCoord * 0.35).rg * 2.0 - 1.0;
        sampledFlow = clamp(sampledFlow, vec2(-1.0), vec2(1.0));
    }
    // Keep the configured direction as the main velocity. A material texture
    // can only add a small local disturbance, so a diffuse texture cannot
    // unexpectedly reverse the whole water surface.
    flow += sampledFlow * flowSpeed * flowTurbulence * 0.12;

    // Two phase-shifted samples hide the hard loop when the UV offset wraps.
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
    waterColor *= ambientLight + diffuseLight * diffuseFactor;

    vec2 screenUv = gl_FragCoord.xy * ubo.viewport.zw;
    vec2 refractionUv = clamp(screenUv + normal.xy * 0.045, vec2(0.001), vec2(0.999));
    vec2 reflectionUv = clamp(
        vec2(screenUv.x + normal.x * 0.03, 1.0 - screenUv.y + normal.y * 0.03),
        vec2(0.001),
        vec2(0.999)
    );
    vec3 sceneRefraction = texture(sceneColorTexture, refractionUv).rgb;
    vec3 sceneReflection = texture(sceneColorTexture, reflectionUv).rgb;
    vec3 refractedColor = mix(waterColor, sceneRefraction, 0.48);
    vec3 reflectedColor = mix(vec3(0.08, 0.3, 0.42), sceneReflection, 0.78);
    waterColor = mix(refractedColor, reflectedColor, 0.2 + fresnel * 0.7);
    waterColor += specularLight * specular * mix(0.75, 1.15, metallic) * specularMapFactor;

    outColor = vec4(waterColor, (0.72 + fresnel * 0.2) * opacity);
}

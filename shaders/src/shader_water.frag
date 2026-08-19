#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 time;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D textureSampler;

layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform;
    vec4 baseColor;
    uint hasTexture;
} material;

layout(location = 0) out vec4 outColor;

float waveHeight(vec2 uv, float time) {
    float waveA = sin(dot(uv, vec2(8.0, 5.0)) + time * 1.25);
    float waveB = sin(dot(uv, vec2(-4.0, 7.0)) - time * 0.85 + 1.4);
    float waveC = cos(dot(uv, vec2(13.0, -3.0)) + time * 0.55);
    return waveA * 0.48 + waveB * 0.32 + waveC * 0.20;
}

void main() {
    float time = ubo.time.x;
    float height = waveHeight(fragTexCoord, time);
    float epsilon = 0.012;
    float slopeX = waveHeight(fragTexCoord + vec2(epsilon, 0.0), time) - height;
    float slopeY = waveHeight(fragTexCoord + vec2(0.0, epsilon), time) - height;

    // The imported demo scene uses Z-up water. Perturbing the view-space
    // normal this way also works when the water mesh has no normal map.
    vec3 normal = normalize(fragNormal + vec3(-slopeX * 0.75, -slopeY * 0.75, 0.0));
    vec3 lightDirection = normalize(vec3(0.45, 0.55, 0.75));
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));

    float diffuse = 0.25 + 0.75 * max(dot(normal, lightDirection), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    float sparkle = pow(max(dot(reflect(-lightDirection, normal), viewDirection), 0.0), 80.0);

    vec3 deepWater = vec3(0.015, 0.16, 0.24);
    vec3 shallowWater = vec3(0.04, 0.48, 0.58);
    vec3 waterColor = mix(deepWater, shallowWater, height * 0.18 + 0.48);
    if (material.hasTexture != 0u) {
        // The material texture supplies the local tint while the procedural
        // waves keep the effect visible even without a dedicated flow map.
        vec3 materialColor = texture(textureSampler, fragTexCoord).rgb;
        waterColor *= mix(vec3(0.72), materialColor, 0.22);
    }
    waterColor *= material.baseColor.rgb * fragVertexColor.rgb;

    vec3 skyReflection = mix(vec3(0.08, 0.22, 0.32), vec3(0.42, 0.72, 0.82), fresnel);
    vec3 color = waterColor * diffuse;
    color = mix(color, skyReflection, 0.28 + fresnel * 0.52);
    color += vec3(0.75, 0.9, 1.0) * sparkle * 0.7;

    outColor = vec4(color, 0.76 + fresnel * 0.18);
}

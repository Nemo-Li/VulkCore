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

// This is the reference project's flow-map operation expressed as a stable
// procedural flow field. A sampled material texture is blended in when one is
// available, so a future RG flow map can be used without changing the layout.
vec2 proceduralFlow(vec2 uv) {
    float x = sin(uv.y * 6.0 + sin(uv.x * 2.0) * 1.8);
    float y = cos(uv.x * 4.0 - sin(uv.y * 3.0) * 1.2);
    return normalize(vec2(x * 0.8, y * 0.8) + vec2(0.001)) * 0.34;
}

float flowWave(vec2 uv, float time) {
    vec2 flow = proceduralFlow(uv);
    float primary = sin(dot(uv + flow * time, vec2(15.0, 6.0)));
    float secondary = cos(dot(uv - flow * time * 0.72, vec2(-7.0, 17.0)) + 2.0);
    return primary * 0.55 + secondary * 0.45;
}

void main() {
    float time = ubo.time.x;
    vec2 flow = proceduralFlow(fragTexCoord);
    float wave = flowWave(fragTexCoord, time);
    float epsilon = 0.01;
    float slopeX = flowWave(fragTexCoord + vec2(epsilon, 0.0), time) - wave;
    float slopeY = flowWave(fragTexCoord + vec2(0.0, epsilon), time) - wave;
    vec3 normal = normalize(fragNormal + vec3(-slopeX * 0.9, -slopeY * 0.9, 0.0));

    // Convert an RG flow value from [0, 1] to a signed velocity, just like
    // the reference shader. The procedural field acts as the fallback when
    // the node is using its regular diffuse texture rather than a flow map.
    vec2 sampledFlow = vec2(0.0);
    if (material.hasTexture != 0u) {
        sampledFlow = texture(textureSampler, fragTexCoord * 0.35).rg * 2.0 - 1.0;
        sampledFlow = clamp(sampledFlow, vec2(-1.0), vec2(1.0));
    }
    flow = mix(flow, sampledFlow * 0.22, 0.2);

    // Two phase-shifted samples hide the hard loop when the UV offset wraps.
    float phase = fract(time * 0.08);
    vec2 uvA = fragTexCoord + flow * phase;
    vec2 uvB = fragTexCoord + flow * fract(phase + 0.5);
    float blendFactor = abs(phase * 2.0 - 1.0);
    vec3 movingTexture = vec3(1.0);
    if (material.hasTexture != 0u) {
        vec3 sampleA = texture(textureSampler, uvA).rgb;
        vec3 sampleB = texture(textureSampler, uvB).rgb;
        movingTexture = mix(sampleA, sampleB, blendFactor);
    }

    vec3 lightDirection = normalize(vec3(0.45, 0.55, 0.75));
    vec3 viewDirection = normalize(vec3(0.42, 0.48, 0.77));
    float diffuse = 0.22 + 0.78 * max(dot(normal, lightDirection), 0.0);
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    float specular = pow(max(dot(reflect(-lightDirection, normal), viewDirection), 0.0), 64.0);

    vec3 waterColor = mix(
        vec3(0.01, 0.12, 0.2),
        vec3(0.02, 0.55, 0.62),
        wave * 0.18 + 0.5
    );
    waterColor *= mix(vec3(0.9), movingTexture, 0.28);
    waterColor *= material.baseColor.rgb * fragVertexColor.rgb;
    waterColor *= diffuse;
    waterColor = mix(waterColor, vec3(0.46, 0.78, 0.86), 0.24 + fresnel * 0.58);
    waterColor += vec3(0.8, 0.95, 1.0) * specular * 0.9;

    outColor = vec4(waterColor, 0.72 + fresnel * 0.2);
}

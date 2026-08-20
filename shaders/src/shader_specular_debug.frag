#version 450

layout(location = 1) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 projection; vec4 time; vec4 viewport;
    vec4 ambientLight; vec4 sunLightDirection; vec4 sunLightColor;
} ubo;
layout(set = 0, binding = 4) uniform sampler2D specularTexture;
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 light = normalize(ubo.sunLightDirection.xyz);
    vec3 viewDirection = normalize(inverse(ubo.view)[3].xyz - fragWorldPosition);
    vec3 halfway = normalize(light + viewDirection);
    float specular = pow(max(dot(normal, halfway), 0.0), mix(96.0, 12.0, material.roughness));
    if ((material.textureFlags & 4u) != 0u) specular *= texture(specularTexture, vec2(0.5)).r;
    outColor = vec4(vec3(specular) * vec3(1.0, 0.78, 0.35), 1.0);
}

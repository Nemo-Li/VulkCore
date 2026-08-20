#version 450

layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(location = 3) in vec3 fragWorldPosition;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 projection; vec4 time; vec4 viewport;
    vec4 ambientLight; vec4 sunLightDirection; vec4 sunLightColor;
} ubo;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 viewDirection = normalize(inverse(ubo.view)[3].xyz - fragWorldPosition);
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 4.0);
    vec3 light = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    vec3 color = material.baseColor.rgb * (0.18 + 0.72 * max(dot(normal, normalize(ubo.sunLightDirection.xyz)), 0.0));
    color = mix(color, light + vec3(0.2, 0.55, 1.0), fresnel * 0.8);
    color *= fragVertexColor.rgb;
    outColor = vec4(color, material.baseColor.a * material.opacity);
}

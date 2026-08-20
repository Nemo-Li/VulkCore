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
    float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    float diffuse = max(dot(normal, normalize(ubo.sunLightDirection.xyz)), 0.0);
    vec3 base = material.baseColor.rgb * (0.2 + diffuse * 0.75) * fragVertexColor.rgb;
    vec3 color = base + vec3(0.16, 0.62, 1.0) * rim * (0.55 + 0.45 * sin(ubo.time.x * 1.5));
    outColor = vec4(color, material.baseColor.a * material.opacity);
}

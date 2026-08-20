#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec4 fragVertexColor;
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 projection; vec4 time; vec4 viewport;
    vec4 ambientLight; vec4 sunLightDirection; vec4 sunLightColor;
} ubo;
layout(set = 0, binding = 1) uniform sampler2D textureSampler;
layout(push_constant) uniform MaterialPushConstants {
    mat4 nodeTransform; vec4 baseColor; uint textureFlags;
    float metallic; float roughness; float opacity; vec4 waterFlow; vec4 waterSurface;
} material;
layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = material.baseColor * fragVertexColor;
    if ((material.textureFlags & 1u) != 0u) base *= texture(textureSampler, fragTexCoord);
    float diffuse = max(dot(normalize(fragNormal), normalize(ubo.sunLightDirection.xyz)), 0.0);
    float band = floor(diffuse * 4.0) / 3.0;
    vec3 halfway = normalize(normalize(ubo.sunLightDirection.xyz) + vec3(0.42, 0.48, 0.77));
    float specular = pow(max(dot(normalize(fragNormal), halfway), 0.0), mix(96.0, 12.0, material.roughness));
    specular = step(0.5, specular);
    vec3 ambient = max(ubo.ambientLight.rgb, vec3(0.0)) * max(ubo.ambientLight.a, 0.0);
    vec3 direct = max(ubo.sunLightColor.rgb, vec3(0.0)) * max(ubo.sunLightDirection.w, 0.0);
    outColor = vec4(base.rgb * (ambient + direct * band) + vec3(specular), base.a * material.opacity);
}
